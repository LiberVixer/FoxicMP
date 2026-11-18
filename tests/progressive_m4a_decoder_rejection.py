"""Classic must reject a decoder without the exact M4A PCM handshake."""
import argparse,json,subprocess,tempfile
from pathlib import Path
from progressive_m4a_fixture import publish

def run(args):
    report=Path(args.report_directory);report.mkdir(parents=True,exist_ok=True)
    case=Path(args.corpus)/'48000-2-2-head';model=json.loads((case/'model.json').read_text());raw=(case/'original.m4a').read_bytes()
    with tempfile.TemporaryDirectory(prefix='foxicmp-m4a-foreign-') as t:
        root=Path(t);value=publish(model,raw,root,len(raw),complete=True);manifest=root/'complete.json';manifest.write_text(json.dumps(dict(value,sessionDirectory=str(root))))
        p=subprocess.run([args.runner,*[str(manifest)]*3,str(root/'actual.pcm'),'48000','2','0','0',str(root/'readers'),'--unsupported-decoder'],capture_output=True,timeout=20)
        (report/'exit.txt').write_text(str(p.returncode));(report/'stdout.txt').write_bytes(p.stdout);(report/'stderr.txt').write_bytes(p.stderr)
        assert p.returncode==0,(p.returncode,p.stdout,p.stderr)
        result=json.loads(p.stdout.strip().splitlines()[-1]);assert result['unsupportedDecoderRejected'] and result['samples']==0 and result['released'],result
    (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);p.add_argument('--corpus',required=True);p.add_argument('--report-directory',required=True);args=p.parse_args();print(json.dumps(run(args)))
