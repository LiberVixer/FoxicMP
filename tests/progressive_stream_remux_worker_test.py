"""Internal continuous engine: growing reads, one worker, streamed fMP4, exact packets."""
import argparse,hashlib,json,os,subprocess,tempfile,threading,time,traceback
from pathlib import Path
from fractions import Fraction
from progressive_process_metrics import sample
SOURCE_SHA256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()


def fixture(root,name,*,rate='25',bframes=0,audio=False,seconds=24):
    path=root/(name+'.mp4');args=['ffmpeg','-v','error','-y','-f','lavfi','-i',f'testsrc2=size=160x90:rate={rate}:duration={seconds}']
    if audio:args+=['-f','lavfi','-i',f'sine=sample_rate=48000:duration={seconds}','-c:a','aac']
    args+=['-c:v','libx264','-threads','2','-preset','veryfast','-bf',str(bframes),'-g','25','-keyint_min','25','-sc_threshold','0','-movflags','+faststart',str(path)]
    subprocess.run(args,check=True,capture_output=True);return path


def probe(path):
    return json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(path)]))


def complete_media(raw):
    at=0;moof=False
    while at+8<=len(raw):
        n=int.from_bytes(raw[at:at+4],'big');kind=raw[at+4:at+8];header=8
        if n==1:
            if at+16>len(raw):break
            n=int.from_bytes(raw[at+8:at+16],'big');header=16
        if n<header or at+n>len(raw):break
        if kind==b'moof':moof=True
        if moof and kind==b'mdat':return True
        at+=n
    return False


def serve(worker,path,*,growing=True,fault=None,short_reads=False,wait_seconds=.12):
    raw=path.read_bytes();source=probe(path);video=[p for p in source['packets'] if p['codec_type']=='video'];tb=Fraction(source['streams'][0]['time_base']);origin=int(video[0]['pts'])
    cuts=[int(next(p['pos'] for p in video if (int(p['pts'])-origin)*tb>=s))+13 for s in (4,8,12,20)] if growing else []
    cuts+=[len(raw)];available=cuts.pop(0);complete=not growing;output=bytearray();events=[];results=[];errors=[];reads=0;first_media=None;warmup=None;kill_ready=threading.Event();released=threading.Event()
    p=subprocess.Popen([str(worker)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8');began=time.monotonic()
    def send(value):p.stdin.write(json.dumps(value)+'\n');p.stdin.flush()
    def dialog():
        nonlocal available,complete,reads,first_media,warmup
        try:
            send(dict(mode='continuous-h264-video-v1',formatHint='mp4',length=str(len(raw)),complete=complete))
            while True:
                line=p.stdout.readline(140001)
                if not line:break
                assert len(line)<=140000;message=json.loads(line)
                if 'result' in message:results.append(message['result']);break
                if message['io']=='read':
                    at=int(message['offset']);n=message['length'];assert 0<=at<=len(raw) and 0<n<=65536;reads+=1
                    if fault=='unconfirmed-eof':send(dict(code='eof'));continue
                    if fault=='integrity':send(dict(code='integrity-mismatch'));continue
                    if at<available:
                        count=min(n,available-at,17 if short_reads and reads<=32 else 65536);send(dict(code='data',hex=raw[at:at+count].hex()))
                    elif complete and at==len(raw):send(dict(code='eof'))
                    else:send(dict(code='waiting-range'))
                elif message['io']=='wait':
                    events.append(dict(reason=message['reason'],offset=message['offset'],available=available,outputBytes=len(output),mediaReady=complete_media(output),beforeSourceComplete=not complete,seconds=time.monotonic()-began))
                    assert p.poll() is None
                    if fault=='kill-wait':kill_ready.set();released.wait(10);break
                    if fault=='cancel-wait':send(dict(code='cancelled'));continue
                    # Hold the same process/demuxer without reply: no false EOF,
                    # reconnect or repeatedly restarting the input parser.
                    if warmup is None and not fault:
                        start=time.monotonic();initial=previous=sample(p.pid);warm_samples=[];stable=0
                        while stable<2:
                            assert time.monotonic()-start<20,'Worker runtime failed to settle within20s'
                            time.sleep(1);current=sample(p.pid);delta=current['cpuSeconds']-previous['cpuSeconds']
                            stable=stable+1 if delta<=.05 and current['handles']<=previous['handles']+2 else 0
                            warm_samples.append(dict(seconds=time.monotonic()-start,metrics=current,cpuDelta=delta));previous=current
                        warmup=dict(seconds=time.monotonic()-start,cpuSeconds=current['cpuSeconds']-initial['cpuSeconds'],samples=warm_samples,stableWindows=2,maxSeconds=20)
                        assert warmup['seconds']<=20
                    cpu=sample(p.pid)['cpuSeconds'];wait_start=time.monotonic()
                    while time.monotonic()-wait_start<wait_seconds:time.sleep(.01)
                    events[-1].update(waitDurationSeconds=time.monotonic()-wait_start,waitCpuSeconds=sample(p.pid)['cpuSeconds']-cpu)
                    assert events[-1]['waitDurationSeconds']>=wait_seconds and events[-1]['waitCpuSeconds']<=.1,events[-1]
                    if cuts:available=cuts.pop(0)
                    else:complete=True
                    send(dict(code='resume',complete=complete))
                elif message['io']=='write':
                    assert int(message['offset'])==len(output) and 0<len(message['hex'])<=131072
                    if fault=='cancel-write':send(dict(code='cancelled'));continue
                    data=bytes.fromhex(message['hex']);output.extend(data);assert len(output)<=64*1024**2
                    if first_media is None and not complete and available<len(raw) and complete_media(output):first_media=dict(available=available,total=len(raw),outputBytes=len(output))
                    send(dict(code='data'))
                else:raise AssertionError(message)
        except (BrokenPipeError,OSError):
            if fault!='kill-wait':errors.append('Unexpected pipe closure')
        except BaseException as e:errors.append(traceback.format_exc());p.kill()
    t=threading.Thread(target=dialog,daemon=True);t.start();termination=None
    try:
        if fault=='kill-wait':
            assert kill_ready.wait(15),'Worker never waited';start=time.monotonic();p.kill();p.wait(timeout=2);termination=time.monotonic()-start;released.set()
        else:p.wait(timeout=30)
    except BaseException:p.kill();p.wait();released.set();raise
    finally:t.join(timeout=2)
    stderr=p.stderr.read();p.stdin.close();p.stdout.close();p.stderr.close();assert not errors and not t.is_alive(),(errors,stderr)
    assert not any(s in stderr for s in ('AddressSanitizer','LeakSanitizer','runtime error:')),stderr
    report=dict(exitCode=p.returncode,waits=events,firstCompleteFragmentBeforeSourceComplete=first_media,reads=reads,outputBytes=len(output),outputSha256=hashlib.sha256(output).hexdigest(),singleWorkerPid=p.pid,runtimeWarmup=warmup)
    if fault=='kill-wait':assert p.returncode==(1 if os.name=='nt' else -9) and termination<2;report['terminationSeconds']=termination
    else:assert len(results)==1;report['result']=results[0]
    return report,bytes(output)


def verify(source,target):
    a=probe(source);b=probe(target);left=a['packets'];right=b['packets'];assert len(left)==len(right)>128
    ta=Fraction(a['streams'][0]['time_base']);tb=Fraction(b['streams'][0]['time_base'])
    for x,y in zip(left,right):
        assert x['data_hash']==y['data_hash']
        for key in ('pts','dts','duration'):assert abs(int(x[key])*ta-int(y[key])*tb)<=tb,(key,x,y)
    def decoded(path):
        raw=subprocess.check_output(['ffmpeg','-v','error','-threads','2','-i',str(path),'-map','0:v','-threads','2','-f','framemd5','-']);return [line.rsplit(b',',1)[-1].strip() for line in raw.splitlines() if not line.startswith(b'#')]
    assert decoded(source)==decoded(target)
    return dict(packets=len(left),encodedPacketsUnchanged=True,ptsDtsDurationsPreservedWithinOneOutputTick=True,decodedFramesIdentical=True)


def run(worker,report_directory):
    worker=Path(worker).resolve();report_directory=Path(report_directory).resolve();report_directory.mkdir(parents=True,exist_ok=True);rows=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-stream-remux-') as temp:
        root=Path(temp);sources=[fixture(root,'main'),fixture(root,'fractional',rate='30000/1001')]
        for path in sources:
            before=hashlib.sha256(path.read_bytes()).hexdigest();row,data=serve(worker,path,short_reads=True,wait_seconds=1.05 if path.stem=='main' else .12);assert row['exitCode']==0 and row['result']['code']=='data' and row['result']['sourceComplete'] and row['result']['sourceEof'] and not row['result']['transcoded'] and not row['result']['playbackSupported'],row
            assert row['firstCompleteFragmentBeforeSourceComplete'] and len(row['waits'])>=4
            if path.stem=='main':assert sum(x['waitDurationSeconds'] for x in row['waits'])>4
            assert row['result']['sha256']==row['outputSha256'] and int(row['result']['bytesWritten'])==len(data)
            out=root/(path.stem+'-output.mp4');out.write_bytes(data);row.update(profile=path.stem,**verify(path,out));assert hashlib.sha256(path.read_bytes()).hexdigest()==before;rows.append(row)
        for fault,expected in [('cancel-wait','cancelled'),('cancel-write','cancelled'),('unconfirmed-eof','unconfirmed-eof'),('integrity','integrity-mismatch'),('kill-wait',None)]:
            row,_=serve(worker,sources[0],fault=fault);assert expected is None or (row['exitCode']==1 and row['result']['code']==expected and not row['result']['sourceComplete']);row.update(profile=fault);rows.append(row)
        for name,path,expected in [('bframes',fixture(root,'bframes',bframes=2),'unsupported-stream-reorder'),('audio',fixture(root,'audio',audio=True),'unsupported-stream-count')]:
            row,_=serve(worker,path,growing=False);assert row['exitCode']==1 and row['result']['code']==expected and row['outputBytes']==0;row['profile']=name;rows.append(row)
    result=dict(result='PASS',testSourceSha256=SOURCE_SHA256,engineOnly=True,publicPlayerCapability=False,count=len(rows),rows=rows,workerSha256=hashlib.sha256(worker.read_bytes()).hexdigest(),temporaryWorkRemoved=not root.exists())
    (report_directory/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--worker',required=True);p.add_argument('--report-directory',required=True);a=p.parse_args();print(json.dumps(run(a.worker,a.report_directory)))
