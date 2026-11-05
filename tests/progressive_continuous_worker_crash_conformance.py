"""Kill an identity-checked worker of an owned test player, never a user process.

Public SDK/IPC checks: preparation, playback, manual pause and real tail wait;
automatic release, independent playback, writer append and explicit retry.
"""
import argparse
import copy
import ctypes
import json
import os
from pathlib import Path
import signal
import time
import traceback

from progressive_continuous_api_test import run
from progressive_continuous_heartbeat_conformance import WorkerExit
from progressive_player_crash_recovery import ApiError, sha, wait
from progressive_stream_remux_worker_test import fixture, probe
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_continuous import ContinuousRemux


class OwnedWorker(WorkerExit):
    def __init__(self, peer, pid, expected=None):
        self.pid = pid
        self.fd = self.handle = None
        expected = Path(expected) if expected is not None else Path(peer.player).parent/('foxicmp-remux-stream-worker'+('.exe' if os.name == 'nt' else ''))
        if os.name == 'nt':
            from ctypes import wintypes as w
            self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            self.kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
            self.kernel.OpenProcess.restype = w.HANDLE
            self.kernel.CloseHandle.argtypes = [w.HANDLE]
            self.kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
            self.kernel.GetExitCodeProcess.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD)]
            self.kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
            self.kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, ctypes.POINTER(w.DWORD)]
            self.handle = self.kernel.OpenProcess(0x101001, False, pid)
            if not self.handle:
                raise ctypes.WinError(ctypes.get_last_error())
            # Query the same held handle used by TerminateProcess; no PID-only kill.
            class Basic(ctypes.Structure):
                _fields_ = [('exit', ctypes.c_long), ('peb', ctypes.c_void_p),
                    ('affinity', ctypes.c_size_t), ('priority', ctypes.c_long),
                    ('pid', ctypes.c_size_t), ('parent', ctypes.c_size_t)]
            native = ctypes.WinDLL('ntdll')
            native.NtQueryInformationProcess.argtypes = [w.HANDLE, w.ULONG, ctypes.c_void_p, w.ULONG, ctypes.POINTER(w.ULONG)]
            try:
                info = Basic(); size = w.ULONG()
                assert native.NtQueryInformationProcess(self.handle, 0, ctypes.byref(info), ctypes.sizeof(info), ctypes.byref(size)) == 0
                assert info.pid == pid and info.parent == peer.process.pid, (info.pid, info.parent)
                name = ctypes.create_unicode_buffer(32768); length = w.DWORD(len(name))
                assert self.kernel.QueryFullProcessImageNameW(self.handle, 0, name, ctypes.byref(length))
                assert Path(name.value).samefile(expected), name.value
            except Exception:
                self.close()
                raise
            self.parent = info.parent
        else:
            self.fd = os.pidfd_open(pid)
            try:
                assert Path('/proc/%d/exe' % pid).samefile(expected)
                fields = Path('/proc/%d/status' % pid).read_text().splitlines()
                self.parent = int(next(line.split()[1] for line in fields if line.startswith('PPid:')))
                assert self.parent == peer.process.pid, self.parent
            except Exception:
                self.close()
                raise
        assert peer.process.poll() is None and not self.exited()
        self.executable_sha256 = sha(expected)

    def terminate(self):
        assert not self.exited()
        began = time.monotonic()
        if self.handle is not None:
            assert self.kernel.TerminateProcess(self.handle, 197), ctypes.get_last_error()
        else:
            signal.pidfd_send_signal(self.fd, signal.SIGKILL)
        wait(self.exited, 'owned worker OS exit', 2)
        return dict(**self.proof(), parentPid=self.parent,
            executableSha256=self.executable_sha256, terminationSeconds=time.monotonic()-began,
            injectedTermination='TerminateProcess-197' if os.name == 'nt' else 'pidfd-SIGKILL')


def exercise(peers, root, report):
    raw_path = fixture(root, 'reference'); raw = raw_path.read_bytes()
    packets = probe(raw_path)['packets']
    cuts = {s: int(next(p['pos'] for p in packets if float(p['pts_time']) >= s))+13 for s in (12, 20)}
    active, watches, rows = [], [], []
    player_pid = peers[0].process.pid

    def model_for(name, prefix, complete=False):
        directory = root/name; directory.mkdir(); path = directory/'input.part'
        model = snapshot(path, raw, complete=complete, prefix=prefix)
        model['timeline']['duration'] = '240000000'; path.write_bytes(raw[:prefix])
        return directory, path, model

    def create(peer, directory, model):
        sdk = ContinuousRemux(peer.client, str(directory)); active.append(sdk)
        sdk.prepare(model, 'clip')
        return sdk

    def ready(sdk):
        def poll():
            s = sdk.status(); assert not s['errorCode'], s
            if s['playbackReady']: return s
            sdk.heartbeat()
        return wait(poll, 'worker-crash preparation')

    def playing(sdk):
        def poll():
            s = sdk.playback_status(); assert s['state'] not in ('error', 'closed', 'finished'), s
            return s if s['state'] == 'playing' and s['positionSeconds'] > .2 and s['decoderReaders'] else None
        return wait(poll, 'worker-crash actual playback')

    def finish(sdk):
        proof = sdk.close(timeout=2); assert proof['released'] and proof['sourceFilesClosed'], proof
        forgotten = sdk.forget(timeout=2); assert forgotten['forgotten'], forgotten
        return dict(proof=proof, forgotten=forgotten)

    try:
        directory, other_path, other_model = model_for('independent', len(raw), True)
        other = create(peers[1], directory, other_model); ready(other)
        wait(lambda: other.status()['outputComplete'], 'independent output complete')
        other.play(title='Independent worker-crash control'); other_initial = playing(other); other.control('pause')
        for phase in ('preparation', 'playing', 'paused', 'buffering'):
            (report/'current.json').write_text(json.dumps(dict(profile=phase))+'\n')
            directory, path, model = model_for(phase, 4 if phase == 'preparation' else cuts[12])
            original = sha(path); sdk = create(peers[0], directory, model); actual = None
            if phase == 'preparation':
                initial = wait(lambda: (lambda s: s if s['producerCode'] == 'waiting' else None)(sdk.status()), 'worker waiting for MP4 header')
                assert initial['maximumReadEnd'] == '4' and not initial['playbackReady'], initial
            else:
                ready(sdk); sdk.play(title='Worker crash '+phase); actual = playing(sdk)
                if phase == 'paused':
                    sdk.control('pause')
                    actual = wait(lambda: (lambda s: s if s['state'] == 'paused' else None)(sdk.playback_status()), 'manual pause before worker crash')
                if phase == 'buffering':
                    def starving():
                        sdk.heartbeat(); s = sdk.playback_status()
                        assert s['state'] not in ('error', 'closed', 'finished'), s
                        return s if s['state'] == 'buffering' and s['positionSeconds'] > .2 and s['decoderReaders'] else None
                    actual = wait(starving, 'real tail wait before worker crash', 20)
                initial = sdk.status()
            assert not initial['sourceComplete'] and not initial['outputComplete']
            watch = OwnedWorker(peers[0], initial['workerId']); watches.append(watch)
            other.control('seek', seconds=0); other.control('play'); control_before = playing(other)
            killed = watch.terminate(); began = time.monotonic(); trace = []
            def released():
                s = sdk.status(); trace.append(dict(seconds=time.monotonic()-began, producer=s))
                assert peers[0].process.poll() is None
                if s['errorCode']:
                    assert s['errorCode'] == 'backend-worker-exited', s
                    # Error discovery and asynchronous decoder/cache retirement
                    # can publish a transient cancelling state before release.
                    if s['released'] and s['sourceFilesClosed']:
                        assert s['state'] == 'error', s
                        return s
            try:
                terminal = wait(released, 'automatic worker-death release without Stop', 2)
            finally:
                (report/('release-trace-'+phase+'.json')).write_text(json.dumps(trace, indent=2)+'\n')
            release_seconds = time.monotonic()-began
            assert release_seconds < 2 and not terminal['playbackReady'] and not terminal['outputComplete'], terminal
            assert killed['terminationSeconds']+release_seconds < 2
            if actual:
                stopped = wait(lambda: (lambda s: s if s['state'] == 'error' and s['decoderReaders'] == 0 else None)(sdk.playback_status()), 'worker death closes decoder', 2)
                assert stopped['error'] == 'backend-worker-exited', stopped
            else:
                stopped = dict(playbackNeverRequested=True)
            receipts = peers[0].client.readers(model['sessionId'], 1)
            assert receipts and all(r['released'] and r['filesClosed'] and r['reason'] == 'files-closed' for r in receipts), receipts
            assert sha(path) == original
            cache = Path(str(peers[0].registry)+'.continuous-cache')
            assert not list(cache.glob('remux-*'))
            def advances():
                s = other.playback_status()
                assert s['playbackId'] == other_initial['playbackId'] and s['readerIds'] == other_initial['readerIds'] and s['state'] == 'playing', s
                return s if s['positionSeconds'] > control_before['positionSeconds']+.1 else None
            control_after = wait(advances, 'independent player advances after worker crash', 2); other.control('pause')
            for action in (sdk.heartbeat, lambda: sdk.publish(model)):
                try: action()
                except ApiError as error: assert error.code == 'continuous-closed', error.reply
                else: raise AssertionError('Dead worker attempt accepted a late writer event')
            final = finish(sdk)
            # A real partial file remains writable after producer/decoder release.
            offset = path.stat().st_size
            with path.open('ab') as file:
                assert file.write(raw[offset:cuts[20]]) == cuts[20]-offset
                file.flush(); os.fsync(file.fileno())
            assert path.read_bytes() == raw[:cuts[20]]
            previous = model; model = copy.deepcopy(model); model['revision'] = '2'
            model['resources'][0]['ranges'] = [dict(offset='0', length=str(cuts[20]))]
            model = protect_snapshot(model, directory, previous=previous); appended = sha(path)
            replacement = create(peers[0], directory, model); retried = ready(replacement)
            assert retried['jobId'] != initial['jobId'] and len(retried['readerIds']) == 1
            if actual: assert not sdk.playback_status()['active'], 'Preparation implicitly resumed playback'
            replacement.play(title='Explicit retry after worker crash'); restarted = playing(replacement)
            assert restarted['playbackId'] != sdk.playback_id and not set(restarted['readerIds']) & set(initial['readerIds'])
            retry_final = finish(replacement)
            assert peers[0].process.pid == player_pid and peers[0].process.poll() is None
            assert sha(path) == appended and not list(cache.glob('remux-*'))
            rows.append(dict(profile=phase, result='PASS', initialProducer=initial, initialPlayback=actual,
                killedWorker=killed, automaticReleaseSeconds=release_seconds, terminalProducer=terminal,
                releaseAfterKillSeconds=killed['terminationSeconds']+release_seconds,
                terminalPlayback=stopped, readerReceipts=receipts, independentBefore=control_before,
                independentAfter=control_after, lateWriterEventsRejected=True, originalUnchanged=True,
                writerAppendAfterRelease=True, appendedOriginalSha256=appended, final=final,
                explicitRetry=dict(producer=retried, playback=restarted, **retry_final),
                samePlayerProcess=True, automaticPlaybackRestart=False))
            (report/'progress.json').write_text(json.dumps(dict(completed=len(rows), rows=rows), indent=2)+'\n')
            print(json.dumps(dict(profile=phase, result='PASS')), flush=True)
        other_final = finish(other); assert sha(other_path) == sha(raw_path)
        return dict(result='PASS', count=len(rows), rows=rows, otherFinal=other_final,
            independentEndpointPreserved=True, onlyOwnedWorkerTerminated=True,
            automaticReleaseMeasuredBeforeCancel=True, sourceOnlyTest=True)
    finally:
        errors = []
        for sdk in reversed(active):
            if not sdk.forgotten:
                try: finish(sdk)
                except Exception: errors.append(traceback.format_exc())
        for watch in watches: watch.close()
        if errors:
            (report/'cleanup-errors.txt').write_text('\n'.join(errors))
            raise AssertionError('Worker-crash test cleanup failed')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--edition', choices=('qt', 'classic'), required=True)
    p.add_argument('--player', action='append', required=True)
    p.add_argument('--reader-runner', required=True)
    p.add_argument('--report-directory', required=True)
    args = p.parse_args(); report = Path(args.report_directory).resolve(); report.mkdir(parents=True, exist_ok=True)
    try:
        result = run(args, lambda peers, root: exercise(peers, root, report), __file__)
        print(json.dumps(dict(result=result['result'], count=result['count'])))
    except Exception:
        (report/'error.txt').write_text(traceback.format_exc(), encoding='utf-8')
        raise
