"""Managed WebM VP9 input: structural bounds, exact fMP4, waits and rejects."""
import argparse,csv,hashlib,io,json,subprocess,tempfile
from pathlib import Path
from fractions import Fraction
from progressive_webm_remux_preflight import fixture as make_webm,scan,unknown_clusters,video_rows
from progressive_stream_remux_worker_test import serve,probe
from progressive_stream_reorder_test import media_prefix
from progressive_stream_hevc_test import frames
from progressive_webm_fixtures import locate,width
MODE='continuous-webm-vp9-video-v1'
PROFILES=[('p0-finite','25','yuv420p','vp09'),('p2-finite','25','yuv420p10le','vp09'),('p0-unknown','25','yuv420p','vp09'),('p2-unknown','25','yuv420p10le','vp09')]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def fixture(root,name,rate='25',pixels='yuv420p',tag='vp09'):
    folder=Path(root)/name;folder.mkdir(exist_ok=True);source=make_webm(folder,rate,pixels)
    if 'unknown' in name:
        raw=source.read_bytes();target=folder/'unknown.webm';target.write_bytes(unknown_clusters(raw,scan(raw)));source=target
    return source

def packets(path):
    if Path(path).suffix in ('.mp4','.fmp4'):
        from progressive_stream_vp9_test import packets as iso_packets
        return iso_packets(path)
    return video_rows(probe(path),Path(path))
def vp8_fixture(root):
    p=Path(root)/'actual-vp8.webm';r=subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=160x90:rate=25:duration=24','-an','-c:v','libvpx','-threads','2','-cpu-used','8','-deadline','realtime','-lag-in-frames','0','-auto-alt-ref','0','-g','25','-live','1',str(p)],capture_output=True,timeout=120);assert r.returncode==0,r.stderr;return p

def run(worker,report,index_runner=None):
    report=Path(report);report.mkdir(parents=True,exist_ok=True);rows=[]
    with tempfile.TemporaryDirectory(prefix='fox-webm-continuous-') as tmp:
        root=Path(tmp);sources=[]
        for name,rate,pixels,tag in PROFILES:
            source=fixture(root,name,rate,pixels);sources.append(source);before=sha(source);reference=packets(source);images=frames(source,pixels);partials=[]
            def checkpoint(raw,state):
                if state['available']>=source.stat().st_size:return
                raw=media_prefix(raw)
                if not raw:return
                digest=hashlib.sha256(raw).hexdigest()
                if any(p['sha256']==digest for p in partials):return
                p=root/'partial.mp4';p.write_bytes(raw);actual=packets(p);decoded=frames(p,pixels)
                assert actual==reference[:len(actual)] and len(actual)>0 and len(decoded)==len(actual) and decoded==images[:len(decoded)]
                partials.append(dict(sha256=digest,packets=len(actual),frames=len(decoded),decodedFramesIdentical=True,sourceAvailable=state['available']))
            row,data=serve(worker,source,mode=MODE,format_hint='webm',video_samples=600,short_reads=True,on_wait=checkpoint)
            assert row['exitCode']==0 and row['result']['code']=='data' and row['firstCompleteFragmentBeforeSourceComplete'] and len(partials)>=3,row
            output=root/(name+'.mp4');output.write_bytes(data);assert packets(output)==reference and frames(output,pixels)==images and sha(source)==before
            metadata=probe(output)['streams'][0];assert metadata['codec_tag_string']=='vp09' and metadata['pix_fmt']==pixels
            native=None
            if index_runner:
                r=subprocess.run([str(index_runner),str(source),'600'],capture_output=True,timeout=90);assert r.returncode==0,r.stderr
                table=list(csv.DictReader(io.StringIO(r.stdout.decode())));raw=source.read_bytes();assert len(table)==600
                for n,p in enumerate(table):assert int(p['pts'])==n*40 and 'SHA256:'+p['sha256']==reference[n][0] and sha_bytes(raw[int(p['offset']):int(p['offset'])+int(p['size'])])==p['sha256']
                native=dict(exitCode=0,packetCount=600,exactRawPayloadClockMap=True)
            row.update(profile=name,pixelFormat=pixels,sourceSha256=before,sourceUnchanged=True,exactPacketClocks=True,decodedFramesIdentical=True,partials=partials,nativeValidator=native)
            rows.append(row);(report/'progress.json').write_text(json.dumps(dict(completed=[r['profile'] for r in rows])))
        original=sources[-1];raw=original.read_bytes();layout=scan(raw);block=layout['blocks'][1];invalid=[]
        def changed(label,offset,value,reasons):
            data=bytearray(raw);data[offset]=value;p=root/(label+'.webm');p.write_bytes(data);invalid.append((label,p,reasons,600))
        _,b,e=locate(raw,0x4282);data=bytearray(raw);data[b:e]=b'xxxx';p=root/'doctype.webm';p.write_bytes(data);invalid.append(('doctype',p,['unsupported-stream-webm-doctype'],600))
        for tag,label in [(0x23e383,'zero-default'),(0x2ad7b1,'zero-scale')]:
            _,b,e=locate(raw,tag);data=bytearray(raw);data[b:e]=bytes(e-b);p=root/(label+'.webm');p.write_bytes(data);invalid.append((label,p,['unsupported-stream-webm-header','unsupported-stream-webm-clock'],600))
        flags=block['body']+width(raw[block['body']])+2
        for label,value in [('lacing',raw[flags]|2),('invisible',raw[flags]|8),('reserved',raw[flags]|16)]:changed(label,flags,value,['unsupported-stream-webm-block'])
        changed('clock-step',block['body']+width(raw[block['body']])+1,(raw[block['body']+width(raw[block['body']])+1]+1)%256,['unsupported-stream-webm-clock'])
        changed('wrong-track',block['body'],0x82,['unsupported-stream-webm-track'])
        changed('show-existing',block['payload'],raw[block['payload']]|8,['unsupported-stream-vp9-frame'])
        changed('profile-change',block['payload'],raw[block['payload']]^16,['unsupported-stream-vp9-configuration-change'])
        p=root/'missing-last.webm';p.write_bytes(raw[:layout['blocks'][-1]['offset']]);invalid.append(('missing-last',p,['incomplete-stream-webm-vp9-samples'],600))
        p=root/'truncated.webm';p.write_bytes(raw[:-1]);invalid.append(('truncated',p,['truncated-stream-webm-element'],600))
        invalid.extend([('too-few-expected',original,['incomplete-stream-webm-vp9-samples'],599),('too-many-expected',original,['incomplete-stream-webm-vp9-samples'],601)])
        for label,options,reasons in [('fractional',dict(rate='30000/1001'),['unsupported-stream-webm-clock']),('audio',dict(audio=True),['unsupported-stream-webm-header','unsupported-stream-webm-metadata']),('matroska',dict(matroska=True),['unsupported-stream-webm-doctype']),('alt-ref',dict(altref=True),['unsupported-stream-vp9-frame','unsupported-stream-vp9-superframe','unsupported-stream-webm-vp9-key'])]:
            folder=root/label;folder.mkdir();p=make_webm(folder,options.pop('rate','25'),'yuv420p',**options);invalid.append((label,p,reasons,600))
        invalid.append(('actual-vp8',vp8_fixture(root),['unsupported-stream-codec'],600))
        for label,source,reasons,count in invalid:
            before=sha(source);row,_=serve(worker,source,mode=MODE,format_hint='webm',video_samples=count,growing=False)
            assert row['exitCode']==1 and row['result']['code'] in reasons and sha(source)==before,(label,row)
            if index_runner:
                native=subprocess.run([str(index_runner),str(source),str(count)],capture_output=True,timeout=90)
                assert native.returncode==1 and native.stderr.decode().strip() in reasons and not any(x in native.stderr for x in (b'AddressSanitizer',b'runtime error:',b'LeakSanitizer')),(label,native.returncode,native.stderr)
                row['nativeValidatorExitCode']=native.returncode
            row.update(profile='reject-'+label,sourceUnchanged=True);rows.append(row)
        for mode in ('continuous-vp9-video-v1','continuous-vp9-superframe-aac-window-v1'):
            row,data=serve(worker,original,mode=mode,growing=False);assert row['exitCode']==1 and row['result']['code']=='unsupported-stream-vp9-configuration' and not data
            row['profile']='old-mode-rejects-'+mode;rows.append(row)
        for fault,reason in [('cancel-wait','cancelled'),('cancel-write','cancelled'),('integrity','integrity-mismatch'),('unconfirmed-eof','unconfirmed-eof')]:
            row,_=serve(worker,original,mode=MODE,format_hint='webm',video_samples=600,fault=fault);assert row['exitCode']==1 and row['result']['code']==reason,row
            row['profile']=fault;rows.append(row)
    result=dict(result='PASS',count=len(rows),rows=rows,mode=MODE,workerSha256=sha(worker),testSourceSha256=sha(__file__),protocolHarnessSha256=sha(Path(__file__).with_name('progressive_stream_remux_worker_test.py')),indexRunnerSha256=sha(index_runner) if index_runner else None,publicPlayerTested=False)
    (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result

def sha_bytes(b):return hashlib.sha256(b).hexdigest()
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--worker',required=True);p.add_argument('--report-directory',required=True);p.add_argument('--index-runner');a=p.parse_args();r=run(Path(a.worker).resolve(),a.report_directory,Path(a.index_runner).resolve() if a.index_runner else None);print(json.dumps(dict(result=r['result'],count=r['count'])))
