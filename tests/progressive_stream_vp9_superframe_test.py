"""Bounded ordinary VP9 MP4 copy-remux: growing packet/clock/pixel fidelity."""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from progressive_stream_remux_worker_test import probe, serve, fixture as avc_fixture
from progressive_stream_reorder_test import media_prefix
from progressive_stream_hevc_test import frames
from progressive_stream_aac_aligned_test import fragment_durations

MODE='continuous-vp9-superframe-video-v1'
PROFILES=[('profile0-25','25','yuv420p','vp09'),('profile2-25','25','yuv420p10le','vp09'),
          ('profile0-fractional','30000/1001','yuv420p','vp09'),('profile2-fractional','30000/1001','yuv420p10le','vp09')]
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def fixture(root,name,rate='25',pixels='yuv420p',tag='vp09',lag=25):
    path=Path(root)/(name+'.mp4')
    args=['ffmpeg','-v','error','-y','-f','lavfi','-i',f'testsrc2=size=160x90:rate={rate}:duration=24',
          '-an','-c:v','libvpx-vp9','-threads','2','-filter_threads','1','-cpu-used','4','-b:v','200k',
          '-g','50','-lag-in-frames','25','-auto-alt-ref','1','-passlogfile',str(Path(root)/(name+'-pass')),
          '-pix_fmt',pixels,'-profile:v','2' if pixels=='yuv420p10le' else '0']
    for tail in [['-pass','1','-f','null','-'],['-pass','2','-video_track_timescale','60000','-tag:v',tag,'-movflags','+faststart',str(path)]]:
        result=subprocess.run(args+tail,capture_output=True,timeout=120)
        assert result.returncode==0,(result.returncode,result.stderr)
    raw=path.read_bytes();d=probe(path)
    count=sum(superframe(raw[int(p['pos']):int(p['pos'])+int(p['size'])]) for p in d['packets'])
    assert count>0, 'Each profile must contain actual indexed superframes'
    return path

def packets(path):
    d=probe(path);assert len(d['streams'])==1 and d['streams'][0]['codec_name']=='vp9'
    stream=d['streams'][0];clock=Fraction(stream['time_base'])
    table=fragment_durations(Path(path).read_bytes()).get(int(stream['id'],0))
    if table is not None:assert len(table)==len(d['packets'])
    rows=[]
    for i,p in enumerate(d['packets']):
        # FFmpeg6 omits VP9 packet durations for some fragmented outputs.
        # Read independent ISO trun/trex values rather than invent a frame rate.
        duration=table[i] if table is not None else int(p['duration'])
        if 'duration' in p:assert int(p['duration'])==duration
        rows.append([p['data_hash'],str(int(p['pts'])*clock),str(int(p['dts'])*clock),str(duration*clock)])
    return rows

def superframe_fixture(root):
    path=Path(root)/'superframe.mp4'
    args=['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=160x90:rate=25:duration=24',
          '-an','-c:v','libvpx-vp9','-threads','2','-cpu-used','4','-b:v','200k',
          '-g','240','-lag-in-frames','25','-auto-alt-ref','1','-passlogfile',str(Path(root)/'vp9-pass')]
    for tail in [['-pass','1','-f','null','-'],['-pass','2','-pix_fmt','yuv420p',
                 '-video_track_timescale','60000','-tag:v','vp09','-movflags','+faststart',str(path)]]:
        p=subprocess.run(args+tail,capture_output=True,timeout=120)
        assert p.returncode==0,(p.returncode,p.stderr)
    return path

def configuration(path):
    raw=Path(path).read_bytes();at=raw.index(b'vpcC');n=int.from_bytes(raw[at-4:at],'big');assert n==20
    return raw[at+4:at+16]

def superframe(raw):
    if not raw or raw[-1]&0xe0!=0xc0:return False
    marker=raw[-1];count=(marker&7)+1;mag=((marker>>3)&3)+1;length=2+count*mag
    if len(raw)<length or raw[-length]!=marker:return False
    sizes=[int.from_bytes(raw[len(raw)-length+1+i*mag:len(raw)-length+1+(i+1)*mag],'little') for i in range(count)]
    return min(sizes)>0 and sum(sizes)==len(raw)-length

def run(worker,report):
    report=Path(report);report.mkdir(parents=True,exist_ok=True);rows=[]
    info=json.loads(subprocess.check_output([str(worker),'--info']))
    assert info['contract']['profiles'][MODE]['oneDisplayedFramePerSample'] is True
    assert info['contract']['profiles'][MODE]['superframes'] is True
    with tempfile.TemporaryDirectory(prefix='fox-vp9-stream-') as name:
        root=Path(name);sources=[]
        for label,rate,pixels,tag in PROFILES:
            source=fixture(root,label,rate,pixels,tag);sources.append(source)
            before=sha(source);reference=packets(source);decoded=frames(source,pixels);partials=[]
            def checkpoint(raw,state):
                raw=media_prefix(raw)
                if not raw or state['available']>=source.stat().st_size:return
                digest=hashlib.sha256(raw).hexdigest()
                if any(p['sha256']==digest for p in partials):return
                p=root/(label+'-partial.mp4');p.write_bytes(raw);actual=packets(p);images=frames(p,pixels)
                assert 0<len(actual)<len(reference) and actual==reference[:len(actual)]
                assert images==decoded[:len(images)] and len(images)==len(actual)
                partials.append(dict(packets=len(actual),frames=len(images),sha256=digest,sourceAvailable=state['available']))
            row,data=serve(worker,source,mode=MODE,short_reads=True,on_wait=checkpoint)
            assert row['exitCode']==0 and row['result']['code']=='data',row
            assert row['firstCompleteFragmentBeforeSourceComplete'] and len(partials)>=3,row
            out=root/(label+'-output.mp4');out.write_bytes(data)
            actual=probe(out)['streams'][0]
            assert actual['codec_tag_string']=='vp09' and actual['pix_fmt']==pixels
            assert packets(out)==reference and frames(out,pixels)==decoded
            assert configuration(out)==configuration(source)
            assert sha(source)==before and sha(out)==row['result']['sha256']
            row.update(profile=label,pixelFormat=pixels,sourceSha256=before,partials=partials,
                       indexedSuperframePackets=sum(superframe(source.read_bytes()[int(p['pos']):int(p['pos'])+int(p['size'])]) for p in probe(source)['packets']),
                       exactPayloadPtsDtsDuration=True,decodedFramesIdentical=True)
            rows.append(row);(report/'progress.json').write_text(json.dumps(dict(completed=[r['profile'] for r in rows])))
        for mode in ['continuous-h264-video-v1','continuous-hevc-reordered-video-v1','continuous-av1-video-v1']:
            row,data=serve(worker,sources[0],mode=mode,growing=False)
            assert row['exitCode']==1 and row['result']['code']=='unsupported-stream-codec' and not data,row
            row['profile']='reject-through-'+mode;rows.append(row)
        avc=avc_fixture(root,'actual-avc')
        row,data=serve(worker,avc,mode=MODE,growing=False)
        assert row['exitCode']==1 and row['result']['code']=='unsupported-stream-codec' and not data,row
        row['profile']='reject-actual-avc';rows.append(row)
        raw=sources[0].read_bytes();at=raw.index(b'vpcC')+4
        for label,offset,value in [('version',0,0),('flags',3,1),('profile-1',4,1),('profile-3',4,3),
                ('invalid-level',5,255),('profile-depth-mismatch',6,0xa2),('twelve-bit',6,0xc2),
                ('chroma-422',6,0x84),('chroma-444',6,0x86),('reserved-chroma',6,0x8e),
                ('reserved-primaries',7,3),('reserved-transfer',8,3),('reserved-matrix',9,3),
                ('rgb-matrix',9,0),('initialization-data',11,1)]:
            content=bytearray(raw);content[at+offset]=value;p=root/(label+'.mp4');p.write_bytes(content)
            row,data=serve(worker,p,mode=MODE,growing=False)
            assert row['exitCode']==1 and row['result']['code']=='unsupported-stream-vp9-configuration' and not data,row
            row['profile']='reject-config-'+label;rows.append(row)
        content=bytearray(raw);content[at-4:at]=b'free';p=root/'missing-vpcc.mp4';p.write_bytes(content)
        row,data=serve(worker,p,mode=MODE,growing=False)
        assert row['exitCode']==1 and row['result']['code']=='unsupported-stream-vp9-configuration' and not data,row
        row['profile']='reject-missing-vpcc';rows.append(row)
        content=bytearray(raw);content[at+4]=2;content[at+6]=0xa2;p=root/'actual-depth-mismatch.mp4';p.write_bytes(content)
        row,_=serve(worker,p,mode=MODE,growing=False)
        assert row['exitCode']==1 and row['result']['code'] in ['unsupported-stream-vp9-configuration','unsupported-stream-vp9-configuration-change'],row
        row['profile']='reject-actual-depth-mismatch';rows.append(row)
        content=bytearray(raw);m=content.index(b'moov');content[m:m+4]=b'mdat';p=root/'non-head-moov.mp4';p.write_bytes(content)
        row,data=serve(worker,p,mode=MODE,growing=False)
        assert row['exitCode']==1 and row['result']['code']=='unsupported-stream-vp9-configuration' and not data,row
        row['profile']='reject-non-head-moov';rows.append(row)
        original=probe(sources[0]);p=root/'truncated.mp4';p.write_bytes(raw[:int(original['packets'][-1]['pos'])])
        row,_=serve(worker,p,mode=MODE,growing=False)
        assert row['exitCode']==1 and row['result']['code']=='incomplete-stream-vp9-samples',row
        row['profile']='reject-truncated-complete';rows.append(row)
        for fault,reason in [('cancel-wait','cancelled'),('cancel-write','cancelled'),('integrity','integrity-mismatch')]:
            row,_=serve(worker,sources[0],mode=MODE,fault=fault)
            assert row['exitCode']==1 and row['result']['code']==reason,row
            row['profile']=fault;rows.append(row)
        for old_mode in ['continuous-vp9-video-v1','continuous-vp9-aac-window-v1']:
            row,_=serve(worker,sources[0],mode=old_mode,growing=False)
            assert row['exitCode']==1 and row['result']['code'] in ['unsupported-stream-count','unsupported-stream-vp9-superframe','unsupported-stream-vp9-frame'],row
            row['profile']='old-mode-still-rejects-'+old_mode;rows.append(row)
        media=probe(sources[0]);body=sources[0].read_bytes()
        packet=next(p for p in media['packets'] if superframe(body[int(p['pos']):int(p['pos'])+int(p['size'])]))
        pos=int(packet['pos']);size=int(packet['size']);marker=body[pos+size-1];count=(marker&7)+1;mag=((marker>>3)&3)+1;index=pos+size-(2+count*mag)
        first_length=int.from_bytes(body[index+1:index+1+mag],'little')
        for label,offset,value,reasons in [
          ('hidden-marked-shown',pos,body[pos]|2,['unsupported-stream-vp9-frame']),
          ('last-marked-hidden',pos+first_length,body[pos+first_length]&~2,['unsupported-stream-vp9-frame','unsupported-stream-vp9-intra-only']),
          ('hidden-show-existing',pos,body[pos]|8,['unsupported-stream-vp9-frame']),
          ('zero-index-length',index+1,0,['unsupported-stream-vp9-superframe-index','invalid-media']),
        ]:
            content=bytearray(body);content[offset]=value
            if label=='zero-index-length':content[index+1:index+1+mag]=bytes(mag)
            invalid=root/(label+'.mp4');invalid.write_bytes(content)
            before=sha(invalid);row,_=serve(worker,invalid,mode=MODE,growing=False)
            assert row['exitCode']==1 and row['result']['code'] in reasons and sha(invalid)==before,row
            row['profile']='reject-'+label;rows.append(row)

    result=dict(result='PASS',count=len(rows),rows=rows,mode=MODE,workerSha256=sha(worker),testSourceSha256=sha(__file__),publicPlayerTested=False,workerInfo=info)
    (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--worker',required=True);p.add_argument('--report-directory',required=True);a=p.parse_args()
    r=run(Path(a.worker).resolve(),a.report_directory);print(json.dumps(dict(result=r['result'],count=r['count'])))
