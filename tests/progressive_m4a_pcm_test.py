"""Real Qt Player / Classic internal decoder PCM; no audio device."""
import argparse,hashlib,json,math,shutil,subprocess,tempfile
from array import array
from pathlib import Path
from progressive_m4a_fixture import publish
from progressive_tail_mp4_fixture import publish as publish_tail
from progressive_mux_ts_conformance import Rpc

def run(args):
    report=Path(args.report_directory).resolve();report.mkdir(parents=True,exist_ok=True);rows=[]
    cases=args.case or json.loads((Path(args.corpus)/'cases.json').read_text())
    for name in cases:
        case=Path(args.corpus)/name;model=json.loads((case/'model.json').read_text());info=json.loads((case/'info.json').read_text());raw=(case/'original.m4a').read_bytes();publisher=publish_tail if info['tailOffset'] else publish
        with tempfile.TemporaryDirectory(prefix='foxicmp-m4a-pcm-') as temp:
            root=Path(temp);folder=root/'source';folder.mkdir()
            cut=info['ends'][min(len(info['ends'])-1,8*info['rate']//info['frame'])];initial=publisher(model,raw,folder,cut)
            full=publisher(model,raw,folder,len(raw),previous=initial);complete=publisher(model,raw,folder,len(raw),previous=full,complete=True)
            targets=[0]+([info['rate']+137] if args.seek and info['samples']>info['rate']*2 else [])
            if args.edition=='qt':
                rpc=Rpc([args.media_runner,'--registry',str(root/'projection-readers')])
                try:
                    answer=rpc('open',directory=str(folder),session=complete);assert answer['ok'],answer
                    n=int(rpc('layout')['tracks'][0]['available']);parts=[]
                    for at in range(0,n,65536):parts.append(bytes.fromhex(rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,n-at)))['hex']))
                    source=root/'decoder.m4a';source.write_bytes(b''.join(parts))
                finally:rpc.close()
            else:
                for label,value in [('initial',initial),('full',full),('complete',complete)]:
                    (root/(label+'.json')).write_text(json.dumps(dict(value,sessionDirectory=str(folder))))
            for target in targets:
                label=name+'-'+str(target);output=root/'actual.pcm'
                if args.edition=='qt':
                    command=[args.runner,str(source),str(output),str(info['rate']),str(info['channels']),str(target),str(info['samples']),str(target/info['rate']),'m4a:'+str(info['skip'])]
                else:
                    command=[args.runner,*[str(root/(s+'.json')) for s in ('initial','full','complete')],str(output),str(info['rate']),str(info['channels']),str(target),'1' if args.growing and not name.startswith('short-') else '0',str(root/'readers')]
                    if target and args.growing:command+=['--seek-incomplete']
                p=subprocess.run(command,capture_output=True,timeout=45);(report/(label+'.exit')).write_text(str(p.returncode));(report/(label+'.stdout')).write_bytes(p.stdout);(report/(label+'.stderr')).write_bytes(p.stderr)
                if p.returncode:shutil.copytree(root,report/(label+'-failure'),dirs_exist_ok=True)
                assert p.returncode==0,(label,p.returncode,p.stdout,p.stderr)
                consumer=json.loads(p.stdout.strip().splitlines()[-1]);fmt='s16le' if args.edition=='qt' else 'f32le';stride=info['channels']*(2 if args.edition=='qt' else 4)
                reference=subprocess.check_output(['ffmpeg','-v','error','-i',str(case/'original.m4a'),'-af',f"atrim=start_sample={target}:end_sample={info['samples']}",'-f',fmt,'-'],stderr=subprocess.PIPE)
                actual=output.read_bytes();expected=(info['samples']-target)*stride
                if len(actual)!=expected or len(reference)!=expected:shutil.copytree(root,report/(label+'-failure'),dirs_exist_ok=True)
                assert len(actual)==len(reference)==expected,(label,len(actual)//stride,len(reference)//stride,expected//stride)
                a,b=array('h' if args.edition=='qt' else 'f'),array('h' if args.edition=='qt' else 'f');a.frombytes(actual);b.frombytes(reference)
                maximum=max(abs(x-y) for x,y in zip(a,b));limit=2 if args.edition=='qt' else 2e-6
                if maximum>limit:
                    (report/(label+'-actual.pcm')).write_bytes(actual);(report/(label+'-reference.pcm')).write_bytes(reference)
                # AAC PNS/SBR/PS noise generators restart at a seek's preroll;
                # that valid decoded signal need not equal a linear decode bit
                # for bit. Keep exact sample geometry, a strict full-decode
                # check, and an explicit seek-only SNR/peak bound instead.
                noise=sum((x-y)**2 for x,y in zip(a,b));signal=sum(y*y for y in b)
                snr=10*math.log10(max(signal,1e-30)/max(noise,1e-30))
                if target:assert maximum<=(2048 if args.edition=='qt' else .0625) and (maximum<=limit or snr>=60),(label,maximum,snr)
                else:assert maximum<=limit,(label,maximum,limit)
                assert (folder/'audio.part').read_bytes()==raw
                rows.append(dict(profile=name,targetSample=target,samples=expected//stride,exactSampleCount=True,maxAbsolutePcmError=maximum,fullDecodeTolerance=limit,seekSnrDb=snr if target else None,seekMinSnrDb=60 if target else None,format=fmt,pcmSha256=hashlib.sha256(actual).hexdigest(),exitCode=p.returncode,nativeConsumer=consumer,audioDeviceOpened=False))
                (report/'progress.json').write_text(json.dumps(rows,indent=2))
    result=dict(result='PASS',edition=args.edition,count=len(rows),rows=rows,runnerSha256=hashlib.sha256(Path(args.runner).read_bytes()).hexdigest())
    (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--runner',required=True);p.add_argument('--media-runner');p.add_argument('--corpus',required=True);p.add_argument('--report-directory',required=True);p.add_argument('--case',action='append');p.add_argument('--seek',action='store_true');p.add_argument('--growing',action='store_true');args=p.parse_args();print(json.dumps(dict(result=run(args)['result'])))
