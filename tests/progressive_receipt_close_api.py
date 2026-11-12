"""Owned ordinary players: stop/cancel/window-close under a private registry lock."""
import json
import os
from pathlib import Path
import threading
import time
import sys
import uuid

from progressive_continuous import ContinuousRemux, AAC_MODE
from progressive_continuous_test_media import source_fixture, source_snapshot, source_cuts, audible_decoder_ready
from progressive_player_crash_recovery import wait
from progressive_continuous_shutdown_conformance import normal_close


class RegistryHold:
    def __init__(self, registry):
        self.file = (registry/'.guard').open('r+b', buffering=0)
        self.stop = threading.Event()
        self.error = None
        self.lock(True)
        self.began = time.monotonic()
        self.unlocked = None
        self.thread = threading.Thread(target=self.release)
        self.thread.start()

    def lock(self, take):
        if os.name == 'nt':
            import msvcrt
            self.file.seek(65536)
            msvcrt.locking(self.file.fileno(), msvcrt.LK_NBLCK if take else msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            fcntl.flock(self.file, fcntl.LOCK_EX | fcntl.LOCK_NB if take else fcntl.LOCK_UN)

    def release(self):
        try:
            self.stop.wait(3)
            self.lock(False)
            self.unlocked = time.monotonic()
        except BaseException as error:
            self.error = repr(error)

    def finish(self):
        self.thread.join(4)
        assert not self.thread.is_alive() and self.error is None, self.error

    def close(self):
        self.stop.set()
        self.finish()
        self.file.close()


def exercise(peers, root, report, expected_audio, growing_source=False):
    report = Path(report)
    rows, failures = [], []
    original = source_fixture(root, 'reference', AAC_MODE)
    raw = original.read_bytes()
    model = source_snapshot(original, raw, complete=True, mode=AAC_MODE)
    active = []
    current_row = None

    def create(peer, title):
        sdk = ContinuousRemux(peer.client, str(root), mode=AAC_MODE)
        active.append(sdk)
        source = model
        if growing_source and peer is peers[0]:
            folder = root/('source-'+uuid.uuid4().hex)
            folder.mkdir()
            source = source_snapshot(folder/'input.part', raw, complete=False,
                                     prefix=source_cuts(original, AAC_MODE)[20], mode=AAC_MODE)
            # The source path is private and physically incomplete, not just a
            # completed producer with output readers left attached.
            sdk = ContinuousRemux(peer.client, str(folder), mode=AAC_MODE)
            active[-1] = sdk
        sdk.prepare(source, 'clip')
        wait(lambda: sdk.status()['playbackReady'] if growing_source and peer is peers[0] else sdk.status()['outputComplete'],
             'producer before registry contention', 30)
        sdk.play(title=title)
        def playing():
            status = sdk.playback_status()
            return status if status['state']=='playing' and status['positionSeconds']>.2 and audible_decoder_ready(status, peer.edition, AAC_MODE) else None
        wait(playing, 'owned audible playback')
        return sdk

    control = create(peers[1], 'Independent control')
    try:
        for operation in ('stop', 'cancel', 'window-close'):
            peer = peers[0]
            sdk = create(peer, 'Registry contention: '+operation)
            sdk.control('pause')
            before = wait(lambda: (lambda s: s if s['state']=='paused' else None)(sdk.playback_status()), 'pause')
            if peer.edition=='qt': assert before['audioOutput']==expected_audio, before
            ids = before['readerIds']
            assert ids
            producer_before = sdk.status()
            if growing_source:
                assert not producer_before['sourceFilesClosed'] and not producer_before['outputComplete'], producer_before
            control_before = control.playback_status()['positionSeconds']
            held = RegistryHold(peer.registry)
            row = dict(operation=operation, readerIds=ids, failures=[])
            current_row = row
            try:
                at = time.monotonic()
                if operation=='window-close':
                    row['normalWindowClose'] = normal_close(peer)
                    row['requestSeconds'] = time.monotonic()-at
                else:
                    if operation=='stop': sdk.control('stop')
                    else: peer.client.request(dict(version=1, action='continuousRemuxCancel', jobId=sdk.job_id, **sdk.identity))
                    row['requestSeconds'] = time.monotonic()-at
                    time.sleep(.1)
                    calls = []
                    for _ in range(3):
                        for name, request in [('capabilities', lambda: peer.client.request(dict(version=1, action='capabilities'))),
                                              ('playbackStatus', sdk.playback_status), ('producerStatus', sdk.status)]:
                            at = time.monotonic()
                            entry = dict(action=name)
                            calls.append(entry)
                            row['callsDuringLock'] = calls
                            try: entry['reply'] = request()
                            finally: entry['seconds'] = time.monotonic()-at
                    row['callsDuringLock'] = calls
                    if row['requestSeconds']>=.5 or any(v['seconds']>=.5 for v in calls):
                        row['failures'].append('request blocked behind registry lock')
                    # Inspect the public cancel acknowledgement directly. A
                    # deliberately tiny SDK polling timeout would test pipe
                    # deadline granularity instead of the cache close path.
                    at = time.monotonic()
                    pending = peer.client.request(dict(version=1, action='continuousRemuxCancel', jobId=sdk.job_id, **sdk.identity))
                    row['pendingCancelSeconds'] = time.monotonic()-at
                    row['pendingClose'] = pending
                    if growing_source and pending.get('producerCode')!='cancelled':
                        row['failures'].append('producer cancellation status not published during receipt wait')
                    if held.unlocked is None and pending['released']: row['failures'].append('release acknowledged while registry lock held')
                    if row['pendingCancelSeconds']>=.5: row['failures'].append('cancel acknowledgement blocked')
                    if os.name!='nt':
                        media = []
                        pending_cache_writers = []
                        for fd in (Path('/proc')/str(peer.process.pid)/'fd').iterdir():
                            try:
                                path = os.readlink(fd)
                                if path.startswith(str(peer.registry)+'.continuous-cache/') and path.endswith('/output.fmp4'):
                                    # The cache producer retains its writer until
                                    # the original's durable receipt is known.
                                    # Separate it from actual decoder readers.
                                    flags = int(next(line.split()[1] for line in (fd.parent.parent/'fdinfo'/fd.name).read_text().splitlines() if line.startswith('flags:')), 8)
                                    if growing_source and (flags & os.O_ACCMODE)==os.O_RDWR:
                                        pending_cache_writers.append(dict(fd=int(fd.name), path=path))
                                    else: media.append(dict(fd=int(fd.name), path=path))
                                elif growing_source and path.startswith(str(root)) and path.endswith('/input.part'):
                                    media.append(dict(fd=int(fd.name), path=path))
                            except FileNotFoundError: pass
                        row['nativeOutputFdsDuringLock'] = media
                        row['pendingCacheWriterFdsDuringLock'] = pending_cache_writers
                        if media: row['failures'].append('native output handles still open')
                    if time.monotonic()-held.began>=2: row['failures'].append('observation missed external lock window')
                if held.unlocked is None:
                    receipts = [json.loads((peer.registry/(rid+'.lease')).read_text()) for rid in ids]
                    row['rawOutputReceiptsDuringLock'] = receipts
                    if any(v['filesClosed'] for v in receipts): row['failures'].append('premature durable output receipt')
                held.finish()
                row['lockSeconds'] = held.unlocked-held.began
            finally:
                held.close()
            if operation=='window-close':
                receipts = [peer.native('readerStatus', readerId=rid)['reader'] for rid in ids]
                row['afterExitReceipts'] = receipts
                assert all(v['released'] and v['reason'] in ('files-closed','process-exited') for v in receipts), receipts
                active.remove(sdk)
                # Recover the retained cache in the same private profile. The
                # previous process exit is proof, never a fabricated receipt.
                peer.launch()
                recovery = create(peer, 'Owned profile cache recovery')
                assert recovery.close(timeout=2)['released']
                assert recovery.forget(timeout=2)['forgotten']
                active.remove(recovery)
                for rid in ids: peer.native('readerForget', readerId=rid)
            else:
                at = time.monotonic()
                receipt = sdk.close(timeout=2)
                row['releaseAfterUnlockSeconds'] = time.monotonic()-at
                assert receipt['released'] and receipt['sourceFilesClosed'], receipt
                assert sdk.forget(timeout=2)['forgotten']
                active.remove(sdk)
            after_control = control.playback_status()
            assert after_control['positionSeconds']>control_before+.5 and after_control['state']=='playing', after_control
            row['independentControlContinued'] = True
            rows.append(row)
            failures.extend(dict(operation=operation, reason=reason) for reason in row['failures'])
            (report/'case-result.json').write_text(json.dumps(dict(result='FAIL' if failures else 'PASS', rows=rows, failures=failures), indent=2)+'\n')
        assert control.close(timeout=2)['released']
        assert control.forget(timeout=2)['forgotten']
        active.remove(control)
        assert not failures, failures
        return dict(result='PASS', count=len(rows), rows=rows, failures=[],
                    growingOriginalSource=growing_source,
                    controlledExternalLock=True, historicalFailureCauseEstablished=False,
                    durableReleaseWithinTwoSecondsUnderThreeSecondExternalLock=False)
    finally:
        primary = sys.exc_info()[0] is not None
        if primary:
            partial = rows + ([current_row] if current_row is not None and current_row not in rows else [])
            (report/'partial-trace.json').write_text(json.dumps(dict(rows=partial, error=repr(sys.exc_info()[1])), indent=2)+'\n')
        cleanup_errors = []
        for sdk in active:
            try:
                proof = sdk.close(timeout=3)
                assert proof.get('released'), proof
                sdk.forget(timeout=3)
            except Exception as error: cleanup_errors.append(repr(error))
        if cleanup_errors:
            (report/'cleanup-errors.json').write_text(json.dumps(cleanup_errors, indent=2)+'\n')
            if not primary: raise AssertionError(cleanup_errors)
