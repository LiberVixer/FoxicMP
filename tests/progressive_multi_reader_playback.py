"""Two playback endpoints and independent I/O leases over the same growing file."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'qt/tools'))
from progressive_client import ApiError, ProgressiveClient
from progressive_integrity import protect_snapshot
from progressive_resources import ResourceProbe
from progressive_v2 import V2PreviewSession
from progressive_rf64_limits_fixture import geometry, header, pcm
from progressive_process_metrics import sample


def exercise(clients, work, control, decoded=None, registries=None):
    assert len(clients) == 2 and clients[0].endpoint != clients[1].endpoint
    assert registries is not None and len(registries)==2
    root = Path(work).resolve(); root.mkdir(parents=True, exist_ok=True)
    pids = [c.request(dict(version=1, action='capabilities'))['pid'] for c in clients]
    results = []; began = time.monotonic(); max_stop = 0.; all_ids = set()
    previews = [None, None]; probes = [None, None]
    def wait(check, label, timeout=15):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            answer = check()
            if answer: return answer
            time.sleep(.025)
        raise AssertionError(('Multi-reader deadline', label))
    def reject(client, action, identifier, code):
        try: client.request(dict(version=1, action=action, readerId=identifier))
        except ApiError as error: assert error.code == code, error.reply
        else: raise AssertionError(('Accepted invalid reader command', action, identifier))
    try:
        for bits, sparse in ((16, False), (24, False), (16, True), (24, True)):
            directory = root / ('pcm%d-%s' % (bits, 'sparse' if sparse else 'prefix')); directory.mkdir()
            g = geometry(bits, 2, 48000, 20*48000)
            data = header(g) + pcm(g, 0, 20*48000*g['align'])
            head = g['offset'] + 4*48000*g['align']; ready = g['offset'] + 12*48000*g['align']
            path = directory / 'shared.part'; path.write_bytes(data[:head]); playback_sets=[set(),set()]
            value = dict(schemaVersion=2, sessionId=uuid.uuid4().hex, generation='1', revision='1',
                writer=dict(instanceId=uuid.uuid4().hex, state='downloading'),
                timeline=dict(timebase=dict(numerator=1, denominator=10000000), live=False, duration='200000000', timestampOffset='0'),
                resources=[dict(id='media', name=path.name, length=str(len(data)), ranges=[dict(offset='0', length=str(head))], state='downloading')],
                tracks=[dict(id='main', kind='audio', required=True, codecHint='pcm-s%dle' % bits,
                    containerHint='rf64-'+('sparse-' if sparse else '')+'pcm%d' % bits, state='downloading', initializationReady=True, source=dict(resourceId='media'))])
            protected = protect_snapshot(value, directory)
            def active(index):
                return [r for r in clients[index].readers(value['sessionId'], 1) if not r['released']]
            def held():
                # Registries are endpoint-scoped: retirement needs their union.
                return [r for i in range(2) for r in active(i)]
            def playing(index, minimum=.25):
                s = previews[index].heartbeat()
                assert s.get('ok') and not s.get('error') and s.get('readerId') == previews[index].reader_id, s
                return s if s.get('state') == 'playing' and s.get('positionSeconds', 0) >= minimum and int(s['decoderIoBytes']['audio']) > 0 else None
            def assert_live(index):
                s = previews[index].heartbeat()
                assert s['readerId'] == previews[index].reader_id and s['active'] and s['state'] in ('playing', 'buffering'), s
                assert not clients[index].reader_status(previews[index].reader_id)['released']
                return s
            def start_probe(index, contend=False):
                probe = ResourceProbe(clients[index]); probes[index] = probe
                def begin():
                    probe.start(protected, str(directory), [dict(resourceId='media', offset='0', length='16'),
                        dict(resourceId='media', offset=str(g['offset']+18*48000*g['align']), length='16')], wait_milliseconds=10000)
                if contend:
                    from progressive_registry_lock import held_guard
                    with held_guard(registries[index]):
                        begin(); time.sleep(.12); blocked=probe.status()
                        assert blocked['state']=='running' and not blocked['readerIds'], blocked
                        t=time.monotonic(); assert clients[index].request(dict(version=1,action='capabilities'))['pid']==pids[index]
                        assert time.monotonic()-t<1
                else: begin()
                def attached():
                    rows = active(index); state=probe.status()
                    assert state['state'] not in ('error','finished','cancelled'), state
                    return rows if len(rows) == len(playback_sets[index])+1 else None
                rows = wait(attached, 'playback and probe leases')
                ids = [r['readerId'] for r in rows if r['readerId'] not in playback_sets[index]]; assert len(ids) == 1
                identifier = ids[0]; assert identifier not in all_ids; all_ids.add(identifier)
                assert probe.status()['state'] == 'running'
                reject(clients[index], 'readerForget', identifier, 'reader_in_use')
                reply = clients[index].request(dict(version=1, action='readerRelease', readerId=identifier))['reader']
                assert not reply['released'] and not reply['filesClosed'], reply
                assert probe.status()['state'] == 'running'
                return identifier, rows
            def cancel_probe(index, identifier):
                t = time.monotonic(); probes[index].cancel(); reply = probes[index].wait(timeout=2)
                assert reply['state'] == 'cancelled' and reply['released'] and identifier in reply['readerIds'], reply
                receipt = clients[index].reader_status(identifier); assert receipt['released'] and receipt['filesClosed'], receipt
                assert probes[index].forget()['state'] == 'forgotten'; probes[index] = None
                return time.monotonic()-t
            def stop(index):
                nonlocal max_stop
                identifier = previews[index].reader_id; t = time.monotonic(); control(index, 'stop')
                def all_closed():
                    # The public group receipt positively acknowledges its bound
                    # files; internal bridge receipts may already be auto-forgotten.
                    receipt=clients[index].reader_status(identifier)
                    if not receipt['released'] or not receipt['filesClosed']: return None
                    return receipt if not playback_sets[index].intersection(r['readerId'] for r in active(index)) else None
                receipt=wait(all_closed, 'group filesClosed and no active member', 2)
                duration = time.monotonic()-t; assert duration < 2 and receipt['filesClosed']; max_stop = max(max_stop, duration)
                assert previews[index].close(timeout=2)['released']; previews[index] = None
                for row in clients[index].readers(value['sessionId'],1):
                    if row['readerId'] in playback_sets[index]:
                        assert row['released'] and row['filesClosed']; clients[index].forget_reader(row['readerId'])
                playback_sets[index]=set(); control(index, 'settle')
                return duration
            for i in range(2):
                previews[i] = V2PreviewSession(clients[i], str(directory), title='Shared reader test')
                previews[i].publish(protected); identifier = previews[i].reader_id
                assert identifier and identifier not in all_ids; all_ids.add(identifier)
                wait(lambda: previews[i].heartbeat()['state'] == 'buffering', 'initial short prefix')
            with path.open('ab') as f: f.write(data[head:ready]); f.flush()
            value['revision'] = '2'; value['resources'][0]['ranges'][0]['length'] = str(ready)
            protected = protect_snapshot(value, directory, previous=protected)
            for preview in previews: preview.publish(protected)
            states = [wait(lambda i=i: playing(i), 'two actual decoders') for i in range(2)]
            if decoded:
                for i in range(2): decoded(i, value['sessionId'], bits)
            ids = [p.reader_id for p in previews]
            for i in range(2): reject(clients[i], 'readerRelease', ids[1-i], 'unknown_reader')
            initial = held(); assert len(initial) == 4, initial
            for i in range(2):
                playback_sets[i]={r['readerId'] for r in active(i)}
                assert len(playback_sets[i])==2 and ids[i] in playback_sets[i]
                for identifier in playback_sets[i]-{ids[i]}:
                    assert identifier not in all_ids; all_ids.add(identifier)
            bindings = {(r['resources'][0]['device'], r['resources'][0]['fileId']) for r in initial}; assert len(bindings) == 1
            contention_boundaries=[]
            if bits==16 and not sparse:
                from progressive_registry_lock import held_guard
                for mode, budget in (('zero-wait',0), ('deadline',80), ('cancel',10000)):
                    boundary=ResourceProbe(clients[0]);probes[0]=boundary
                    with held_guard(registries[0]):
                        t=time.monotonic();boundary.start(protected,str(directory),[dict(resourceId='media',offset='0',length='16')],wait_milliseconds=budget)
                        if mode=='cancel':
                            time.sleep(.08);blocked=boundary.status();assert blocked['state']=='running' and not blocked['readerIds'],blocked
                            boundary.cancel()
                        reply=boundary.wait(timeout=2);elapsed=time.monotonic()-t
                        assert reply['released'] and not reply['readerIds'] and elapsed<1,reply
                        if mode=='cancel':assert reply['state']=='cancelled',reply
                        else:assert reply['state']=='error' and reply['errorCode']=='reader_registry_busy',reply
                        assert boundary.forget()['state']=='forgotten';probes[0]=None
                    contention_boundaries.append(dict(mode=mode,budgetMilliseconds=budget,elapsedSeconds=elapsed,state=reply['state'],readerIds=reply['readerIds']))
                assert len(held())==4;assert_live(0);assert_live(1)
            # Cancel an auxiliary read without replacing either decoder.
            first, same_profile = start_probe(0, contend=True); assert len(held()) == 5
            cancel_times = [cancel_probe(0, first)]
            assert len(held()) == 4; before = assert_live(1)['positionSeconds']; assert_live(0)
            wait(lambda: playing(1, before+.15), 'other decoder advances after probe cancel')
            # Stop one decoder: its auxiliary reader and the other decoder survive.
            second, _ = start_probe(0); stops = [stop(0)]
            remaining = held(); assert len(remaining) == 3 and {r['readerId'] for r in remaining} == {second}|playback_sets[1]
            assert_live(1); assert path.exists()
            cancel_times.append(cancel_probe(0, second)); assert len(held()) == 2; assert_live(1)
            # Reverse the order: stop the other decoder, reopen the first while
            # the outstanding probe still owns the same physical file.
            third, _ = start_probe(1); stops.append(stop(1)); assert [r['readerId'] for r in held()] == [third]
            previews[0] = V2PreviewSession(clients[0], str(directory), title='Reopened shared reader')
            previews[0].publish(protected); reopened = previews[0].reader_id
            assert reopened not in all_ids; all_ids.add(reopened)
            resumed = wait(lambda: playing(0), 'reopen alongside retained reader'); assert len(held()) == 3
            playback_sets[0]={r['readerId'] for r in active(0)}; assert len(playback_sets[0])==2 and reopened in playback_sets[0]
            for identifier in playback_sets[0]-{reopened}:
                assert identifier not in all_ids; all_ids.add(identifier)
            cancel_times.append(cancel_probe(1, third)); assert len(held()) == 2; assert_live(0)
            stops.append(stop(0)); assert not held()
            assert all(not c.readers(value['sessionId'], 1) for c in clients)
            assert path.read_bytes() == data[:ready]; digest = hashlib.sha256(path.read_bytes()).hexdigest()
            path.unlink(); directory.rmdir()
            results.append(dict(bits=bits, sparse=sparse, result='PASS', playbackReaderIds=ids, reopenedReaderId=reopened,
                probeReaderIds=[first, second, third], samePhysicalFile=True, sameProfileConcurrentReaders=3,
                peakConcurrentReaders=5, registryContentionRecovered=True, contentionBoundaries=contention_boundaries, initialReceipts=initial, sameProfileReceipts=same_profile,
                playingPositions=[s['positionSeconds'] for s in states], decoderIoBytes=[s['decoderIoBytes']['audio'] for s in states],
                reopenedPosition=resumed['positionSeconds'], stopReleaseSeconds=stops, probeCancelSeconds=cancel_times,
                sourceSha256=digest, removedOnlyAfterAllReleased=True, processMetrics=[sample(pid) for pid in pids]))
            (root/'progress.json').write_text(json.dumps(dict(completed=len(results), last=results[-1])), encoding='utf-8')
        return dict(result='PASS', profiles=4, playbackReaderGroups=12, playbackResourceReceipts=24, auxiliaryProbeReaders=12, uniqueReaderReceipts=len(all_ids), peakConcurrentReaders=5,
            endpointScopedRegistries=True, endpointsDistinct=True, processIds=pids, separateProcesses=pids[0]!=pids[1],
            maxStopReleaseSeconds=max_stop, elapsedSeconds=time.monotonic()-began, rows=results,
            actualPlaybackEveryPeer=True, toolbarStopFirst=True, sourceDirectoriesRemoved=not any(root.glob('pcm*')))
    finally:
        for probe in probes:
            if probe is not None:
                try: probe.cancel(); probe.wait(); probe.forget()
                except (OSError, RuntimeError): pass
        for preview in previews:
            if preview is not None: preview.close(timeout=10)


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--player', required=True); p.add_argument('--profile', action='append', required=True); p.add_argument('--work', required=True); p.add_argument('--registry',action='append',required=True); a=p.parse_args()
    assert len(a.profile)==2; root=Path(a.work); sequence=[0]
    def control(peer, action):
        sequence[0]+=1; seq=sequence[0]; tmp=root/'control.tmp'
        tmp.write_text(json.dumps(dict(sequence=seq, peer=peer, action=action)), encoding='utf-8'); deadline=time.monotonic()+5
        while True:
            try: tmp.replace(root/'control.json'); break
            except PermissionError:
                assert time.monotonic()<deadline; time.sleep(.01)
        while time.monotonic()<deadline:
            try: reply=json.loads((root/('ack-%d.json' % seq)).read_text())
            except (OSError, ValueError): reply={}
            if reply.get('sequence')==seq: return reply
            time.sleep(.02)
        raise AssertionError(('UI acknowledgement', peer, action))
    def decoded(peer, session, bits):
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            try: value=json.loads((root/('decoder-%d-%s.json' % (peer,session))).read_text())
            except (OSError, ValueError): value={}
            if value.get('sessionId')==session:
                assert value['codec']=='pcm_s%dle' % bits and value['rate']==48000 and value['channels']==2 and value['seconds']>=.2; return
            time.sleep(.02)
        raise AssertionError(('No actual decoder', peer, session))
    print(json.dumps(exercise([ProgressiveClient(a.player, profile) for profile in a.profile], a.work, control, decoded, a.registry)))
