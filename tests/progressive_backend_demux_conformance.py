"""Actual FFmpeg diagnostics over confirmed-range AVIO; no player activation."""
import argparse,copy,hashlib,json,os,subprocess,tempfile,time,uuid,sys
from pathlib import Path
from progressive_mux_ts_conformance import Rpc
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_integrity import protect_snapshot
from progressive_client import ApiError, ProgressiveClient

def snapshot(path,raw,*,complete=True,prefix=None,ranges=None):
    n=len(raw) if prefix is None else prefix
    path.write_bytes(raw[:n])
    value=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='complete' if complete else 'downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='80000000',timestampOffset='0'),resources=[dict(id='clip',name=path.name,length=str(len(raw)),state='complete' if complete else 'downloading',ranges=ranges if ranges is not None else [dict(offset='0',length=str(n))] if n else [])],tracks=[dict(id='media',kind='video',required=True,codecHint='',containerHint='mp4',state='complete' if complete else 'downloading',initializationReady=False,source=dict(resourceId='clip'))])
    return protect_snapshot(value,path.parent)

def fixtures(root):
    root=Path(root);root.mkdir(exist_ok=True)
    results={}
    for hint,ext,codec in [('mp4','mp4','aac'),('mov','mov','aac'),('webm','webm','libopus'),('matroska','mkv','aac'),('mpegts','ts','aac')]:
        path=root/('original.'+ext)
        video=['-c:v','libvpx-vp9','-deadline','realtime','-cpu-used','8'] if hint=='webm' else ['-c:v','libx264','-preset','ultrafast']
        options=['-movflags','+faststart'] if hint in ('mp4','mov') else []
        subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=320x180:rate=25:duration=8','-f','lavfi','-i','sine=sample_rate=48000:duration=8','-map','0:v','-map','1:a',*video,'-threads','2','-c:a',codec,*options,str(path)],check=True,capture_output=True)
        results[hint]=path
    for hint,ext,codec in [('m4a','m4a','aac'),('adts','aac','aac'),('mp3','mp3','libmp3lame'),('ogg','ogg','libvorbis'),('flac','flac','flac'),('wav','wav','pcm_s16le')]:
        path=root/('audio.'+ext);subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','sine=sample_rate=48000:duration=8','-c:a',codec,str(path)],check=True,capture_output=True);results[hint]=path
    return results

def wait(rpc,id,timeout=15):
    until=time.monotonic()+timeout
    while time.monotonic()<until:
        v=rpc('resourceProbeStatus',version=1,probeId=id)
        assert v.get('ok'),v
        if v['state'] in ('finished','cancelled','error','forgotten') and v['released']:return v
        time.sleep(.01)
    raise AssertionError(('demux probe timeout',v))

def forget(rpc,id):
    v=rpc('resourceProbeForget',version=1,probeId=id);assert v['ok'],v
    v=wait(rpc,id);assert v['state']=='forgotten',v

def start(rpc,model,folder,hint,wait_ms=0,packets=12):
    return rpc('resourceProbe',version=1,session=model,sessionDirectory=str(folder),reads=[],waitMilliseconds=wait_ms,media=dict(resourceId='clip',formatHint=hint,packetLimit=packets))

class ApiRpc:
    def __init__(self, client):self.client=client
    def __call__(self, action, **fields):
        try:return self.client.request(dict(action=action,**fields))
        except ApiError as error:return error.reply
    def close(self):pass

def run(runner=None, client=None):
    rows=[];checks=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-backend-demux-') as tmp:
        root=Path(tmp);media=fixtures(root/'fixtures');rpc=ApiRpc(client) if client else Rpc([runner,'--registry',str(root/'registry')])
        try:
            for hint,original in media.items():
                folder=root/hint;folder.mkdir();raw=original.read_bytes();value=snapshot(folder/'input.part',raw)
                v=start(rpc,value,folder,hint);assert v['ok'],v;answer=wait(rpc,v['probeId']);result=answer['media']
                assert answer['state']=='finished' and result['code']=='data' and result['analysisOnly'] and not result['playbackSupported'],(hint,answer)
                probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_data_hash','sha256','-of','json',str(original)]))
                expected=probe['packets'][:12];actual=result['packets'];assert len(actual)==len(expected),(hint,len(actual),len(expected))
                for a,b in zip(actual,expected):
                    assert a['sha256']==b['data_hash'].split(':')[1] and a['bytes']==int(b['size']) and a['stream']==b['stream_index'],(hint,a,b)
                    for field in ('pts','dts','duration'):
                        if b.get(field) is not None:assert a[field]==str(b[field]),(hint,field,a,b)
                assert all(s['timebaseDenominator']>0 for s in result['streams'])
                rows.append(dict(format=hint,packets=len(actual),packetHashesAndClocksMatch=True,streams=result['streams'],backend=result['backend']))
                forget(rpc,v['probeId']);assert not rpc('readers',version=1,sessionId=value['sessionId'],generation='1')['readers'];checks.append(hint+'-real-demux-hashes-clocks-and-release')
            raw=media['mp4'].read_bytes();folder=root/'partial';folder.mkdir();path=folder/'input.part';value=snapshot(path,raw,complete=False,prefix=4)
            v=start(rpc,value,folder,'mp4');answer=wait(rpc,v['probeId']);assert answer['media']['code']=='waiting-range' and not answer['allReadable'] and answer['media']['neededOffset']=='4',answer
            forget(rpc,v['probeId']);checks.append('partial-header-waits-without-false-success')
            # Diagnostic success before writer complete, without reading unpublished tail.
            value=snapshot(path,raw,complete=False,prefix=len(raw)//2)
            v=start(rpc,value,folder,'mp4');answer=wait(rpc,v['probeId']);assert answer['media']['code']=='data',answer
            assert path.stat().st_size<len(raw);forget(rpc,v['probeId']);checks.append('faststart-demux-before-complete')
            # Tail moov is present physically, but is not readable unless writer confirms it.
            tail=root/'tail.mp4';subprocess.run(['ffmpeg','-v','error','-y','-i',str(media['mp4']),'-c','copy',str(tail)],check=True,capture_output=True);tailraw=tail.read_bytes()
            assert tailraw.index(b'moov')>len(tailraw)//2
            value=snapshot(path,tailraw,complete=False,prefix=len(tailraw)//2)
            v=start(rpc,value,folder,'mp4');answer=wait(rpc,v['probeId']);assert answer['media']['code']=='waiting-range',answer
            assert int(answer['media']['neededOffset'])>=len(tailraw)//2;forget(rpc,v['probeId']);checks.append('tail-moov-requests-confirmed-missing-range')
            value=snapshot(path,raw,complete=False,prefix=4);v=start(rpc,value,folder,'mp4',wait_ms=5000);id=v['probeId']
            until=time.monotonic()+3
            while time.monotonic()<until:
                state=rpc('resourceProbeStatus',version=1,probeId=id)
                if state.get('media',{}).get('code')=='waiting-range':break
                time.sleep(.01)
            else:raise AssertionError(state)
            path.write_bytes(raw);next_=copy.deepcopy(value);next_['revision']='2';next_['writer']['state']='complete';next_['resources'][0].update(state='complete',ranges=[dict(offset='0',length=str(len(raw)))]);next_['tracks'][0]['state']='complete';next_=protect_snapshot(next_,folder,previous=value)
            assert rpc('resourceProbeUpdate',version=1,probeId=id,session=next_)['ok'];answer=wait(rpc,id);assert answer['media']['code']=='data' and answer['appliedRevision']=='2',answer
            forget(rpc,id);checks.append('published-range-update-retries-demux')
            value=snapshot(path,raw,complete=False,prefix=4);v=start(rpc,value,folder,'mp4',wait_ms=10000);time.sleep(.1);before=time.monotonic();rpc('resourceProbeCancel',version=1,probeId=v['probeId']);answer=wait(rpc,v['probeId']);elapsed=time.monotonic()-before
            assert answer['state']=='cancelled' and elapsed<2,answer;forget(rpc,v['probeId']);checks.append('cancellation-releases-in-under-two-seconds')
            # Reject media request shape before launching a worker.
            value=snapshot(path,raw)
            for hint in ('http','hls','concat','lavfi'):
                v=start(rpc,value,folder,hint);assert not v['ok'] and v['code']=='invalid_request',v;checks.append('reject-'+hint+'-format')
            # A full file and a sparse hole must never bypass the confirmed prefix.
            value=snapshot(path,raw,complete=False,prefix=4);path.write_bytes(raw)
            v=start(rpc,value,folder,'mp4');answer=wait(rpc,v['probeId']);assert answer['media']['code']=='waiting-range' and answer['media']['neededOffset']=='4',answer
            forget(rpc,v['probeId']);checks.append('physical-tail-does-not-bypass-confirmed-ranges')
            value=snapshot(path,raw,complete=False,prefix=4)
            with path.open('r+b') as file:file.truncate(len(raw))
            v=start(rpc,value,folder,'mp4');answer=wait(rpc,v['probeId']);assert answer['media']['code']=='waiting-range',answer
            forget(rpc,v['probeId']);checks.append('sparse-hole-is-waiting-not-zero-packets')
            value=snapshot(path,raw);damaged=bytearray(raw);damaged[0]^=1;path.write_bytes(damaged)
            v=start(rpc,value,folder,'mp4');answer=wait(rpc,v['probeId']);assert answer['state']=='error' and answer['media']['code']=='integrity-mismatch',answer
            forget(rpc,v['probeId']);checks.append('sha256-mismatch-never-reaches-success')
            value=snapshot(path,b'not a media file')
            v=start(rpc,value,folder,'mp4');answer=wait(rpc,v['probeId']);assert answer['state']=='error' and answer['media']['code']!='data',answer;forget(rpc,v['probeId']);checks.append('corrupt-complete-input-is-not-ready')
        finally:rpc.close()
    return dict(result='PASS',scope='backend-demux-player-api' if client else 'backend-demux-native',count=len(checks),checks=checks,realMedia=rows,analysisOnly=True,playbackTested=False,remuxTested=False,networkRequests=0,cancelSeconds=elapsed)

if __name__=='__main__':
    p=argparse.ArgumentParser();g=p.add_mutually_exclusive_group(required=True);g.add_argument('--runner');g.add_argument('--player');p.add_argument('--profile');a=p.parse_args()
    print(json.dumps(run(a.runner,ProgressiveClient(a.player,a.profile) if a.player else None)))
