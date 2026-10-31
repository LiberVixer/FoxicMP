"""Exercise child shutdown directly, including sanitizer diagnostics on stderr."""
import argparse, hashlib, json, subprocess, tempfile, threading
from pathlib import Path


def serve(worker, raw, *, complete=True, prefix=None, hint='mp4'):
    prefix=len(raw) if prefix is None else prefix
    p=subprocess.Popen([worker],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
    frames=[];errors=[]
    def dialog():
        try:
            p.stdin.write(json.dumps(dict(formatHint=hint,length=str(len(raw)),complete=complete,packetLimit=12))+'\n');p.stdin.flush()
            while True:
                line=p.stdout.readline(140001)
                if not line:break
                assert len(line)<=140000,'oversize worker frame'
                message=json.loads(line)
                if 'result' in message:frames.append(message['result']);break
                assert message['io']=='read' and type(message['length']) is int and 0<message['length']<=65536,message
                at=int(message['offset']);assert 0<=at<=len(raw),message
                if at<prefix:reply=dict(code='data',hex=raw[at:min(prefix,at+message['length'])].hex())
                elif complete and at==len(raw):reply=dict(code='eof')
                else:reply=dict(code='waiting-range')
                p.stdin.write(json.dumps(reply)+'\n');p.stdin.flush()
        except BaseException as e:errors.append(str(e));p.kill()
    thread=threading.Thread(target=dialog);thread.start()
    try:p.wait(timeout=10)
    except subprocess.TimeoutExpired:p.kill();p.wait();raise
    finally:thread.join(timeout=2)
    error=p.stderr.read();p.stdin.close();p.stdout.close();p.stderr.close()
    assert not errors and len(frames)==1,(errors,error)
    assert not any(x in error for x in ('AddressSanitizer','LeakSanitizer','runtime error:')),error
    return p.returncode,frames[0]


def run(worker):
    worker=str(Path(worker).resolve());checks=[]
    info=subprocess.run([worker,'--info'],capture_output=True,text=True,timeout=10)
    assert info.returncode==0,info.stderr
    assert json.loads(info.stdout)['avformatVersion']>0;assert not info.stderr,info.stderr;checks.append('runtime-info-normal-exit')
    with tempfile.TemporaryDirectory(prefix='foxicmp-worker-exit-') as directory:
        path=Path(directory)/'clip.mp4'
        subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=160x90:rate=25:duration=2','-c:v','libx264','-threads','2','-preset','ultrafast','-movflags','+faststart',str(path)],check=True,capture_output=True)
        raw=path.read_bytes();expected=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_data_hash','sha256','-of','json',str(path)]))['packets'][:12]
        code,result=serve(worker,raw);assert code==0 and result['code']=='data',(code,result)
        assert [p['sha256'] for p in result['packets']]==[p['data_hash'].split(':')[1] for p in expected];checks.append('real-demux-packets-and-normal-cleanup')
        code,result=serve(worker,raw,complete=False,prefix=4);assert code==0 and result['code']=='waiting-range' and 'packets' not in result,(code,result);checks.append('missing-range-cleanup-without-packets')
        code,result=serve(worker,b'broken');assert code==0 and result['code']=='invalid-media',(code,result);checks.append('corrupt-media-normal-cleanup')
        code,result=serve(worker,raw,hint='http');assert code==1 and result['code']=='unsupported-container-hint',(code,result);checks.append('unsupported-demuxer-exception-cleanup')
    return dict(result='PASS',count=len(checks),checks=checks,normalChildExitObserved=True,stderrSanitizerDiagnosticsChecked=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--worker',required=True);args=p.parse_args();print(json.dumps(run(args.worker)))
