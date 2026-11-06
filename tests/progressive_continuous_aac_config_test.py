"""Bounded probe regression: a 1-sample AAC track after the first video read."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile

from progressive_stream_aac_aligned_test import fixture
import progressive_stream_remux_worker_test as worker_test
from progressive_stream_remux_worker_test import serve


def run(worker,report):
    report.mkdir(parents=True,exist_ok=False)
    rows=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-aac-config-') as name:
        root=Path(name)
        original,_=fixture(root,'short-audio',44100,1,'25',2,True,False,seconds=14.137,samples=1)
        raw=original.read_bytes();canonical=bytes.fromhex('120856e500')
        assert raw.count(canonical)==1
        variants=[('canonical',canonical),('960-frame',bytes.fromhex('120c56e500')),
                  ('different-rate',bytes.fromhex('118856e500')),('he-aac',bytes.fromhex('2a0856e500')),
                  ('sbr-present',bytes.fromhex('120856e580')),('invalid-extension',bytes.fromhex('120856e501'))]
        reference=worker_test.probe(original)
        original_probe=worker_test.probe
        # The video layout is unchanged; malformed ASC must reach the worker,
        # even when the independent ffprobe refuses to inspect its audio.
        worker_test.probe=lambda _:reference
        for label,asc in variants:
            path=root/(label+'.mp4');path.write_bytes(raw.replace(canonical,asc));before=hashlib.sha256(path.read_bytes()).hexdigest()
            result,output=serve(worker,path,growing=False,mode='continuous-h264-aac-window-v1',on_audio_window=lambda _:True)
            if label=='canonical':
                assert result['exitCode']==0 and result['result']['audioPresentationSamples']=='1' and result['result']['audioPackets']==2,result
            else:
                assert result['exitCode']==1 and not output and result['result']['code'] in ('unsupported-stream-aac-config','invalid-media','unsupported-stream-clock'),result
            assert hashlib.sha256(path.read_bytes()).hexdigest()==before
            rows.append(dict(profile=label,result='PASS',worker=result))
        worker_test.probe=original_probe
    value=dict(result='PASS',count=len(rows),rows=rows,workerSha256=hashlib.sha256(worker.read_bytes()).hexdigest(),testSourceSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),temporaryWorkRemoved=not root.exists())
    (report/'result.json').write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
    return value


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--worker',type=Path,required=True);parser.add_argument('--report-directory',type=Path,required=True);args=parser.parse_args()
    value=run(args.worker.resolve(),args.report_directory);print(json.dumps(dict(result=value['result'],count=value['count'])))
