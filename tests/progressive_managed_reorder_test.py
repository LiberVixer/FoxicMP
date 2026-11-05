"""Private managed B-frame remux, physical growth, cache/index and libmpv decode."""
import argparse
import copy
from fractions import Fraction
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import traceback

from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_mux_ts_conformance import Rpc
from progressive_stream_remux_worker_test import fixture
from progressive_stream_reorder_test import projection, sha, MODE


def run(runner, worker, report):
    report.mkdir(parents=True, exist_ok=False)
    inputs = dict(testSourceSha256=sha(__file__), runnerSha256=sha(runner), workerSha256=sha(worker),
                  projectionHelperSha256=sha(Path(__file__).with_name('progressive_stream_reorder_test.py')))
    (report/'inputs.json').write_text(json.dumps(inputs, indent=2)+'\n')
    rows = []
    exits = []
    try:
        with tempfile.TemporaryDirectory(prefix='foxicmp-managed-reorder-') as directory:
            root = Path(directory)
            for name, rate, bframes in [('b2-25', '25', 2), ('b3-fractional', '30000/1001', 3),
                                        ('b16-60', '60000/1001', 16), ('b2-vfr', '25', 2)]:
                folder = root/name
                folder.mkdir()
                original = fixture(folder, 'reference', rate=rate, bframes=bframes)
                if name.endswith('vfr'):
                    unfiltered = folder/'unfiltered.mp4'
                    original.rename(unfiltered)
                    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(unfiltered),
                                    '-vf', r'select=not(eq(mod(n\,5)\,4))', '-fps_mode', 'vfr',
                                    '-c:v', 'libx264', '-threads', '2', '-preset', 'veryfast', '-bf', '2',
                                    '-g', '25', '-sc_threshold', '0', '-movflags', '+faststart', str(original)],
                                   capture_output=True, check=True)
                raw = original.read_bytes()
                original_sha = sha(original)
                reference = projection(original)
                expected_frames = len(reference)
                assert Fraction(reference[0][2]) < 0 and reference[0][1] == '0'
                # Packet positions deliberately cut through a packet, not at a GOP boundary.
                from progressive_stream_remux_worker_test import probe
                packet_data = probe(original)['packets']
                cuts = [int(next(p['pos'] for p in packet_data if float(p['pts_time']) >= s))+13
                        for s in (4, 8, 12, 20)]
                path = folder/'input.part'
                model = snapshot(path, raw, complete=False, prefix=4)
                model['timeline']['duration'] = '240000000'
                rpc = Rpc([str(runner), str(folder/'registry'), str(worker)])
                publications = []
                samples = []
                proof = []
                current = 4
                worker_id = None

                def cmd(action, **fields):
                    answer = rpc(action, **fields)
                    assert answer['ok'], (action, answer)
                    return answer

                def status():
                    value = cmd('status')
                    assert not value['failure'], value
                    assert value['sourceCode'] in ('starting', 'running', 'waiting', 'data'), value
                    if worker_id:
                        assert value['workerId'] == worker_id, value
                    assert int(value['maximumReadEnd']) <= current, value
                    return value

                def wait(fn, label, timeout=20):
                    end = time.monotonic()+timeout
                    while time.monotonic() < end:
                        value = fn()
                        if value:
                            return value
                        time.sleep(.02)
                    raise AssertionError(label)

                def images(where):
                    return [sha(p) for p in sorted(where.glob('*.png'))]

                def publish(count, complete=False, gap=False):
                    nonlocal model, current
                    previous = model
                    assert path.stat().st_size == current
                    with path.open('ab') as output:
                        assert output.write(raw[current:count]) == count-current
                        output.flush()
                        os.fsync(output.fileno())
                    if gap:
                        end = time.monotonic()+.3
                        while time.monotonic() < end:
                            assert int(status()['maximumReadEnd']) <= current
                            time.sleep(.02)
                    model = copy.deepcopy(previous)
                    model['revision'] = str(int(model['revision'])+1)
                    state = 'complete' if complete else 'downloading'
                    model['writer']['state'] = model['tracks'][0]['state'] = model['resources'][0]['state'] = state
                    model['resources'][0]['ranges'] = [dict(offset='0', length=str(count))]
                    model = protect_snapshot(model, folder, previous=previous)
                    current = count
                    cmd('publish', session=model)
                    assert sha(path) == hashlib.sha256(raw[:count]).hexdigest()
                    publications.append(dict(revision=model['revision'], physicalBytes=count, complete=complete))

                def check_index():
                    value = cmd('fragments')
                    actual = value['samples']
                    assert len(actual) <= len(reference)
                    for i, (x, y, packet) in enumerate(zip(actual, reference, packet_data)):
                        assert x['sha256'] == y[0].split(':')[1].lower()
                        # MP4 sample duration is the decode interval. ffprobe can
                        # report a codec-derived constant packet duration for VFR;
                        # independent source DTS deltas retain the stts interval.
                        duration = (Fraction(reference[i+1][2])-Fraction(y[2])
                                    if i+1 < len(reference) else Fraction(y[3]))
                        assert duration > 0
                        for key, want in [('pts', Fraction(y[1])), ('dts', Fraction(y[2])),
                                          ('end', Fraction(y[1])+duration)]:
                            assert abs(Fraction(x[key], 10000000)-want) <= Fraction(1, 10000000), (x, y)
                        assert x['sync'] == ('K' in packet['flags'])
                    samples.append(dict(count=len(actual), initialized=value['initialized']))
                    return actual

                try:
                    golden = folder/'golden'
                    golden.mkdir()
                    cmd('mpv', images=str(golden), file=str(original))
                    end = wait(lambda: (lambda s:s if s['ended'] else None)(cmd('mpvStatus')), 'Reference EOF')
                    assert end['reason'] == 0 and end['error'] == 0, end
                    cmd('mpvDestroy')
                    reference_images = images(golden)
                    assert len(reference_images) == expected_frames

                    cmd('start', directory=str(folder), root=str(folder/'cache'), session=model, reorderedVideo=True)
                    initial = wait(lambda: (lambda s:s if s['sourceCode'] == 'waiting' else None)(status()), 'Initial source wait')
                    worker_id = initial['workerId']
                    assert worker_id > 0 and initial['visible'] == 0 and not initial['complete']
                    first_reader = cmd('fragmentOpen')['readerId']
                    assert not check_index()
                    output_images = folder/'growing-images'
                    output_images.mkdir()
                    cmd('mpv', images=str(output_images))
                    publish(cuts[0], gap=True)
                    ready = wait(lambda: (lambda s:s if s['sourceCode'] == 'waiting' and s['visible'] else None)(status()), 'Growing cache ready')
                    wait(lambda: len(list(output_images.glob('*.png'))) > 10, 'Actual decoder output')
                    partial = check_index()
                    assert 0 < len(partial) < expected_frames
                    assert not cmd('mpvStatus')['ended']

                    cmd('mpvCommand', args=['set', 'pause', 'yes'])
                    time.sleep(.2)
                    paused = len(list(output_images.glob('*.png')))
                    publish(cuts[1])
                    wait(lambda: int(status()['maximumReadEnd']) == cuts[1], 'Next confirmed source prefix')
                    time.sleep(.25)
                    assert len(list(output_images.glob('*.png'))) == paused and not cmd('mpvStatus')['ended']
                    assert len(check_index()) > len(partial)
                    cmd('fragmentClose')
                    second_reader = cmd('fragmentOpen')['readerId']
                    assert second_reader != first_reader
                    assert cmd('readerStatus', version=1, readerId=first_reader)['reader']['released']
                    cmd('mpvCommand', args=['set', 'pause', 'no'])
                    wait(lambda: len(list(output_images.glob('*.png'))) > paused, 'Resume after manual pause')
                    for cut in cuts[2:]:
                        publish(cut)
                        wait(lambda: int(status()['maximumReadEnd']) == cut, 'Confirmed prefix read')
                        check_index()
                        assert not cmd('mpvStatus')['ended'] and not status()['complete']
                    publish(len(raw))
                    wait(lambda: status()['sourceCode'] == 'waiting', 'All bytes without writer complete')
                    time.sleep(.2)
                    assert not cmd('mpvStatus')['ended'] and not status()['complete']
                    publish(len(raw), True)
                    final = wait(lambda: (lambda s:s if s['done'] else None)(status()), 'Producer complete')
                    assert final['complete'] and final['sourceCode'] == 'data' and final['sourceFilesClosed']
                    result = json.loads(final['sourceResult'])
                    assert result['mode'] == MODE and not result['transcoded']
                    end = wait(lambda: (lambda s:s if s['ended'] else None)(cmd('mpvStatus')), 'Confirmed decoder EOF')
                    assert end['reason'] == 0 and end['error'] == 0, end
                    cmd('mpvDestroy')
                    assert images(output_images) == reference_images
                    assert len(check_index()) == expected_frames
                    cache = next((folder/'cache').glob('remux-*/output.fmp4'))
                    assert projection(cache) == reference

                    # A separate decoder starts paused so untimed output cannot race seek.
                    sought = folder/'sought-images'
                    sought.mkdir()
                    cmd('mpv', images=str(sought), paused=True)
                    wait(lambda: cmd('mpvStatus')['loaded'], 'Completed cache loaded for seek')
                    wait(lambda: images(sought), 'Initial paused preview frame')
                    preview = images(sought)
                    assert preview == reference_images[:1], preview
                    cmd('mpvCommand', args=['seek', '10', 'absolute+exact'])
                    cmd('mpvCommand', args=['set', 'pause', 'no'])
                    seek_end = wait(lambda: (lambda s:s if s['ended'] else None)(cmd('mpvStatus')), 'Seek EOF')
                    assert seek_end['reason'] == 0 and seek_end['error'] == 0, seek_end
                    cmd('mpvDestroy')
                    first = sum(Fraction(p[1]) < 10 for p in reference)
                    assert images(sought) == preview+reference_images[first:], (len(images(sought)), first)

                    # A raw cursor and the Classic index keep independent physical leases.
                    cmd('cursor')
                    assert not cmd('retire')['removed'] and cache.exists()
                    cmd('fragmentClose')
                    assert not cmd('retire')['removed'] and cache.exists()
                    assert cmd('seek', offset=0)['offset'] == 0
                    cmd('beginRead', length=8)
                    read = wait(lambda: (lambda s:s if not s.get('pending') else None)(cmd('readResult')), 'Held cursor read')
                    assert read['bytes'] == 8
                    cmd('closeCursor')
                    wait(lambda: cmd('retire')['removed'], 'Final cache retirement', 2)
                    assert not cache.exists()
                    for rid in final['sourceReaderIds']+status()['readerIds']:
                        receipt = cmd('readerStatus', version=1, readerId=rid)['reader']
                        assert receipt['released'] and receipt['filesClosed'] and receipt['reason'] == 'files-closed', receipt
                        proof.append(receipt)
                        cmd('readerForget', version=1, readerId=rid)
                    cmd('drop')
                    assert cmd('collect')['bytes'] == 0
                    assert sha(path) == sha(original) == original_sha
                    rows.append(dict(profile=name, result='PASS', decodedFrames=expected_frames,
                                     seekFrames=expected_frames-first, initialPausedPreviewFrames=len(preview), exactDecodedImages=True,
                                     exactPacketClocks=True, classicSampleTolerance100ns=1,
                                     indexEndOracle='source DTS intervals (MP4 stts), final packet duration',
                                     samples=samples, publications=publications, receipts=proof,
                                     singleWorkerPid=worker_id, manualPausePreserved=True,
                                     originalSha256=original_sha, physicalAppendOnly=True,
                                     unpublishedPhysicalTailNotRead=True, firstReady=ready, final=final))
                    (report/'progress.json').write_text(json.dumps(dict(completed=[r['profile'] for r in rows])))
                finally:
                    rpc.close()
                    exits.append(dict(profile=name, pid=rpc.p.pid, exitCode=rpc.p.returncode))
            assert sha(__file__) == inputs['testSourceSha256']
        result = dict(result='PASS', **inputs, count=len(rows), rows=rows, exactDriverExits=exits,
                      temporaryWorkRemoved=not root.exists(), realLibmpvDecode=True,
                      classicFragmentParserOnly=True, installedDirectShowGraphTested=False,
                      publicPlayerCapability=False, audioSupported=False)
        (report/'result.json').write_text(json.dumps(result, indent=2)+'\n')
        return result
    except BaseException:
        (report/'failure.json').write_text(json.dumps(dict(rows=rows, exactDriverExits=exits,
                                                         error=traceback.format_exc()), indent=2)+'\n')
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--worker', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    args = parser.parse_args()
    run(args.runner.resolve(), args.worker.resolve(), args.report_directory.resolve())
