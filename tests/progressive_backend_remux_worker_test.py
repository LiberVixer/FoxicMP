"""Normal-exit/LSan coverage for the real custom AVIO output callbacks."""
import argparse,hashlib,json,tempfile
from pathlib import Path
from progressive_backend_worker_test import serve
from progressive_backend_remux_conformance import fixtures,verify_artifact


def run(worker):
    worker=str(Path(worker).resolve());reports=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-remux-worker-') as temp:
        root=Path(temp);inputs=fixtures(root/'originals')
        for name in ('av','bframes','video','audio','audio44100','fractional','mov','partial'):
            path=inputs[name];raw=path.read_bytes();partial=name=='partial';limit=64 if partial else 128;output=bytearray();hint='m4a' if name in ('audio','audio44100') else 'mov' if name=='mov' else 'mp4'
            code,result=serve(worker,raw,complete=not partial,prefix=len(raw)//2 if partial else None,hint=hint,remux=True,output=output,limit=limit)
            assert code==0 and result['code']=='data' and result['preparationOnly'] and not result['analysisOnly'],(code,result)
            assert hashlib.sha256(output).hexdigest()==result['remux']['sha256'];target=root/(name+'.mp4');target.write_bytes(output)
            reports.append(dict(profile=name,**verify_artifact(path,target,limit,not partial)))
        for name,raw,expected in [('missing',inputs['av'].read_bytes(),'waiting-range'),('unsupported',inputs['unsupported'].read_bytes(),'unsupported-remux-codec')]:
            output=bytearray();code,result=serve(worker,raw,remux=True,output=output,prefix=4 if name=='missing' else None,complete=name!='missing',limit=128)
            assert code==(0 if name=='missing' else 1) and result['code']==expected and not output,(code,result);reports.append(dict(profile=name,normalExitOrTypedError=True))
    return dict(result='PASS',count=len(reports),reports=reports,normalChildExitObserved=True,stderrSanitizerDiagnosticsChecked=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--worker',required=True);a=p.parse_args();print(json.dumps(run(a.worker)))
