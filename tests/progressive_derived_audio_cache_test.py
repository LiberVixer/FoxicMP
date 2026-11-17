"""Independent canonical cache recount and forged producer-result rejection."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
from progressive_mux_ts_conformance import Rpc


def run(runner, worker, canonical, samples, report, packet_clock=False):
    raw = canonical.read_bytes();digest = hashlib.sha256(raw).hexdigest()
    from progressive_webm_opus_remux_preflight import configuration
    from progressive_webm_remux_preflight import scan
    audio=configuration(raw)[2] if packet_clock else None
    skip=audio['preSkip'] if audio else 312
    packets = sum(b['track']==audio['number'] for b in scan(raw)['blocks']) if packet_clock else (samples+1271)//960
    with tempfile.TemporaryDirectory(prefix='fox-derived-audio-cache-') as temporary:
        root = Path(temporary);rpc = Rpc([str(runner), str(root/'registry'), str(worker)])
        try:
            assert rpc('derivedStore', root=str(root/'cache'), sessionId='d'*32, endpoint='240000000',**(dict(packetClock=True) if packet_clock else {}))['ok']
            for at in range(0, len(raw), 4093):assert rpc('append', offset=str(at), hex=raw[at:at+4093].hex())['ok']
            before = rpc('status');assert before['ok'] and not before['complete']
            assert before['audioPresentationSamples'] == samples and before['audioSamples'] == packets
            for count, number in [(samples+1,packets),(samples,packets+1)]:
                reply = rpc('verifyDerived', samples=str(count), packets=str(number),skip=str(skip))
                assert not reply['ok'] and reply['code'] == 'cache-invalid-opus-result',reply
            assert rpc('verifyDerived', samples=str(samples), packets=str(packets),skip=str(skip))['ok']
            if packet_clock:
                wrong=rpc('verifyDerived',samples=str(samples),packets=str(packets),skip=str(312 if skip==120 else 120))
                assert not wrong['ok'] and wrong['code']=='cache-invalid-opus-result',wrong
            assert not rpc('status')['complete']  # Recount alone cannot complete output.
            assert rpc('producerClosed', error='', sha256=digest)['ok']
            complete = rpc('status');assert complete['complete'] and not complete['failure'] and complete['audioIndexedComplete']
            assert rpc('retire')['ok'];assert rpc('status')['removed']
        finally:rpc.close()
        assert rpc.p.returncode == 0
        assert not list((root/'cache').glob('remux-*'))
    assert hashlib.sha256(canonical.read_bytes()).hexdigest() == digest
    result = dict(result='PASS', exitCode=rpc.p.returncode, samples=samples, packets=packets,
        preSkip=skip,wrongPreSkipRejected=packet_clock,wrongSampleCountRejected=True, wrongPacketCountRejected=True,
        recountDoesNotCompleteOutput=True, sourceUnchanged=True, cacheRemoved=True,
        canonicalSha256=digest, runnerSha256=hashlib.sha256(runner.read_bytes()).hexdigest())
    report.mkdir(parents=True, exist_ok=True);(report/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__ == '__main__':
    p=argparse.ArgumentParser();p.add_argument('--packet-opus',action='store_true');p.add_argument('--runner',type=Path,required=True);p.add_argument('--worker',type=Path,required=True)
    p.add_argument('--canonical',type=Path,required=True);p.add_argument('--samples',type=int,required=True)
    p.add_argument('--report-directory',type=Path,required=True);a=p.parse_args()
    run(a.runner.resolve(),a.worker.resolve(),a.canonical.resolve(),a.samples,a.report_directory.resolve(),a.packet_opus)
