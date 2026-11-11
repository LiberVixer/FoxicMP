"""Classic v2/AAC after-payload boundaries, with independent packet/clock oracle."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'qt' / 'tools'))
from progressive_stream_aac_aligned_test import fixture
from progressive_stream_aac_window_test import packets
from progressive_stream_remux_worker_test import serve
from progressive_integrity import protect_snapshot


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args):
    report = args.report_directory.resolve()
    report.mkdir(parents=True, exist_ok=False)
    rows = []
    with tempfile.TemporaryDirectory(prefix='foxicmp-projection-io-') as tmp:
        root = Path(tmp)
        for label, rate, channels, fps, bframes in (
                ('48000-mono', 48000, 1, '25', 2),
                ('44100-stereo', 44100, 2, '30000/1001', 3)):
            folder = root / label
            folder.mkdir()
            samples = rate * 24 + 137
            source, _ = fixture(folder, 'original', rate, channels, fps, bframes,
                                False, False, seconds=24.137, samples=samples)
            original_sha = sha(source)
            plans = []
            def accept(plan):
                assert not plans
                plans.append(plan)
                assert plan['sampleRate'] == rate and plan['channels'] == channels
                assert int(plan['timestampEnd']) == samples
                return True
            worker, data = serve(args.worker, source,
                mode='continuous-h264-aac-window-v1', short_reads=True,
                on_audio_window=accept, cut_seconds=(4, 8, 12, 20))
            assert worker['exitCode'] == 0 and worker['result']['code'] == 'data'
            assert worker['result']['sourceComplete'] and len(plans) == 1
            cache = folder / 'cache-output.mp4'
            cache.write_bytes(data)
            inputs = []
            snapshot = dict(schemaVersion=2, sessionId=uuid.uuid4().hex,
                generation='1', revision='1', writer=dict(instanceId=uuid.uuid4().hex, state='complete'),
                timeline=dict(timebase=dict(numerator=1, denominator=10000000),
                    live=False, duration='241370000', timestampOffset='0'), resources=[], tracks=[])
            for index, kind in enumerate(('video', 'audio')):
                path = folder / (kind + '.part')
                command = ['ffmpeg', '-v', 'error', '-y', '-i', str(source),
                    '-map', '0:' + ('v:0' if index == 0 else 'a:0'), '-c', 'copy',
                    '-movflags', 'empty_moov+frag_keyframe+default_base_moof',
                    '-frag_duration', '1000000', '-f', 'mp4', str(path)]
                value = subprocess.run(command, capture_output=True, timeout=30)
                assert value.returncode == 0, (value.returncode, value.stderr.decode(errors='replace'))
                inputs.append(path)
                size = path.stat().st_size
                snapshot['resources'].append(dict(id=kind, name=path.name, length=str(size),
                    ranges=[dict(offset='0', length=str(size))], state='complete'))
                snapshot['tracks'].append(dict(id=kind, kind=kind, required=True,
                    codecHint='h264' if index == 0 else 'aac-lc', containerHint='fmp4',
                    initializationReady=True, state='complete', source=dict(resourceId=kind)))
            snapshot = protect_snapshot(snapshot, folder, block_bytes=65536)
            config = folder / 'config.json'
            config.write_text(json.dumps(dict(cache=str(cache), window=plans[0],
                snapshot=snapshot, tracks=[str(p) for p in inputs])), encoding='utf-8')
            before = {p.name: sha(p) for p in [source, cache, *inputs, config]}
            result = subprocess.run([str(args.runner), str(config), str(folder / 'native')],
                capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=180)
            (report / (label + '.stdout')).write_text(result.stdout, encoding='utf-8')
            (report / (label + '.stderr')).write_text(result.stderr, encoding='utf-8')
            (report / (label + '.exit')).write_text(str(result.returncode) + '\n')
            assert result.returncode in (0, 1), (label, result.returncode, result.stdout, result.stderr)
            native = json.loads(result.stdout)
            assert native['count'] == 120
            failed = {row['profile'] for row in native['rows'] if row['result'] != 'PASS'}
            if args.expect_pre_fix:
                assert result.returncode == 1 and native['result'] == 'FAIL' and native['failures'] == 2
                assert failed == {backend + '-1-syncread-none-64-first-fault'
                                  for backend in ('v2-resources', 'aac-projection')}
            else:
                assert result.returncode == 0 and native['result'] == 'PASS' and native['failures'] == 0
                assert not failed
            assert len(native['rows']) == 120 and len(native['dumps']) == 4
            assert all(r['closedAndForgottenGroupReaders'] == 2
                       and r['registryLeasesEmpty'] for r in native['rows'])
            cache_packets = packets(cache)
            oracles = []
            artifacts = report / (label + '-artifacts')
            artifacts.mkdir()
            for dump in native['dumps']:
                path = Path(dump['path'])
                assert path.stat().st_size == dump['bytes'] and sha(path) == dump['sha256']
                kind = 'video' if dump['track'] == 0 else 'audio'
                actual = packets(path)
                assert set(actual) == {kind}
                expected = cache_packets[kind] if dump['backend'] == 'aac-projection' else packets(inputs[dump['track']])[kind]
                assert actual[kind] == expected, (label, dump['backend'], kind, 'packet payload/PTS/DTS/duration mismatch')
                if dump['backend'] == 'v2-resources':
                    assert path.read_bytes() == inputs[dump['track']].read_bytes()
                copied = artifacts / (dump['backend'] + '-' + kind + '.mp4')
                shutil.copyfile(path, copied)
                oracles.append(dict(backend=dump['backend'], track=kind,
                    bytes=dump['bytes'], sha256=dump['sha256'], packets=len(expected),
                    packetPayloadPtsDtsDurationExact=True,
                    packetSequenceSha256=hashlib.sha256(json.dumps(expected, separators=(',', ':')).encode()).hexdigest()))
            assert sha(source) == original_sha
            assert {p.name: sha(p) for p in [source, cache, *inputs, config]} == before
            rows.append(dict(profile=label, result='EXPECTED-PRE-FIX-FAILURE' if args.expect_pre_fix else 'PASS', nativeExit=result.returncode,
                native=native, worker=worker, audioWindow=plans[0], oracle=oracles,
                inputSha256=before, inputsUnchanged=True))
    value = dict(result='EXPECTED-PRE-FIX-FAILURE' if args.expect_pre_fix else 'PASS', count=sum(r['native']['count'] for r in rows),
        rows=rows, runnerSha256=sha(args.runner), workerSha256=sha(args.worker),
        driverSha256=sha(__file__), nativeSourceSha256=sha(Path(__file__).with_name('classic_projection_io_probe.cpp')),
        temporaryWorkRemoved=not root.exists(), actualClassicSessionReader=True,
        actualReadV2AndSyncRead=True, insideProjectionPhysicalReadBarrier=False,
        actualDecodeOrPcmOracle=False, actualDirectShowGraph=False,
        installedPlayerLaunched=False, testChangesProductionSource=False)
    (report / 'result.json').write_text(json.dumps(value, indent=2) + '\n')
    print(json.dumps(dict(result=value['result'], count=value['count'])))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--worker', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    parser.add_argument('--expect-pre-fix', action='store_true')
    args = parser.parse_args()
    args.runner = args.runner.resolve()
    args.worker = args.worker.resolve()
    run(args)
