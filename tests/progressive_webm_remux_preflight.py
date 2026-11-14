"""WebM/VP9 continuous-input preparation; no player or new mode is enabled.

Keep sources/oracles/commands/exits. Compare the production native WebM index
with full and physically truncated-source FFmpeg copy references. Such references
are not managed growing playback, decoder graphs, or an A/V acceptance test.
"""
import argparse
import csv
from fractions import Fraction
import hashlib
import io
import json
from pathlib import Path
import subprocess
import time
import traceback

from progressive_webm_fixtures import element, locate, width
from progressive_stream_remux_worker_test import serve
from progressive_stream_aac_aligned_test import fragment_durations
from progressive_stream_vp9_superframe_test import superframe

PROPOSED_MODE='continuous-webm-vp9-video-v1'
PROFILES=[('p0-25','25','yuv420p'),('p2-25','25','yuv420p10le'),
          ('p0-fractional','30000/1001','yuv420p'),('p2-fractional','30000/1001','yuv420p10le')]
TOP={0x114d9b74,0x1549a966,0x1654ae6b,0x1f43b675,0x1c53bb6b,0x1254c367,0x1043a770,0x1941a469}

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def command(args,folder,name,*,input_bytes=None):
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    (folder/(name+'.command.json')).write_text(json.dumps([str(x) for x in args],indent=2)+'\n')
    p=subprocess.run([str(x) for x in args],input=input_bytes,capture_output=True,timeout=120)
    for ext,data in [('stdout',p.stdout),('stderr',p.stderr)]: (folder/(name+'.'+ext)).write_bytes(data)
    (folder/(name+'.exit')).write_text(str(p.returncode)+'\n')
    return p

def ok(args,folder,name):
    p=command(args,folder,name);assert p.returncode==0,(name,p.returncode,p.stderr[-1500:]);return p.stdout

def probe(path,folder,name):return json.loads(ok(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',path],folder,name))

def decoded(path,pixels,folder,name):
    raw=ok(['ffmpeg','-v','error','-threads','1','-i',path,'-map','0:v:0','-threads','1','-pix_fmt',pixels,'-f','framemd5','-'],folder,name)
    return [r.rsplit(b',',1)[-1].strip().decode() for r in raw.splitlines() if r and not r.startswith(b'#')]

def scan(raw):
    """Controlled fixture inspector, not a production/admission EBML parser."""
    tag,head,stop=element(raw,0);assert tag==0x1a45dfa3 and stop is not None
    _,b,e=locate(raw,0x4282);doctype=raw[b:e].decode()
    tag,start,end=element(raw,stop);assert tag==0x18538067
    result=dict(docType=doctype,unknownSegmentSize=end is None,clusters=[],blocks=[])
    _,b,e=locate(raw,0x2ad7b1);result['timestampScaleNs']=int.from_bytes(raw[b:e],'big')
    try:_,b,e=locate(raw,0x23e383);result['defaultDurationNs']=int.from_bytes(raw[b:e],'big')
    except ValueError:result['defaultDurationNs']=None
    at=start
    while at<(end or len(raw)):
        tag,body,stop=element(raw,at)
        if tag!=0x1f43b675:
            assert stop is not None;at=stop;continue
        cluster=dict(offset=at,body=body,unknownSize=stop is None,timestamp=None);cur=body;limit=stop or len(raw);items=[]
        while cur<limit:
            child,cb,ce=element(raw,cur)
            if stop is None and child in TOP:break
            assert ce is not None and ce<=limit
            if child==0xe7:cluster['timestamp']=int.from_bytes(raw[cb:ce],'big')
            if child==0xa3:items.append((cur,cb,ce,False))
            elif child==0xa0:
                pos=cb
                while pos<ce:
                    k,b,e=element(raw,pos);assert e is not None and e<=ce
                    if k==0xa1:items.append((pos,b,e,True))
                    pos=e
            cur=ce
        cluster['end']=cur;assert cluster['timestamp'] is not None
        for offset,b,e,group in items:
            w=width(raw[b]);flags=raw[b+w+2];payload=b+w+3
            value=(raw[b]&((0x80>>(w-1))-1))
            for byte in raw[b+1:b+w]:value=(value<<8)|byte
            packet=raw[payload:e];assert packet
            relative=int.from_bytes(raw[b+w:b+w+2],'big',signed=True)
            result['blocks'].append(dict(offset=offset,body=b,payload=payload,end=e,track=value,
                ticks=cluster['timestamp']+relative,flags=flags,lacing=(flags>>1)&3,blockGroup=group,
                sha256='SHA256:'+hashlib.sha256(packet).hexdigest(),hidden=not bool(packet[0]&2),
                showExisting=bool(packet[0]&8),indexedSuperframe=superframe(packet)))
        result['clusters'].append(cluster);assert cur>at;at=cur
    result['headBytes']=result['clusters'][0]['offset'];return result

def unknown_clusters(raw,layout):
    out=bytearray(raw)
    for cluster in layout['clusters']:
        at=cluster['offset']+width(raw[cluster['offset']],4);w=cluster['body']-at
        out[at:at+w]=((1<<(7*w+1))-1).to_bytes(w,'big')
    return bytes(out)

def fixture(folder,rate,pixels,*,altref=False,audio=False,matroska=False):
    path=folder/('source.mkv' if matroska else 'source.webm')
    args=['ffmpeg','-v','error','-y','-f','lavfi','-i',f'testsrc2=size=160x90:rate={rate}:duration=24']
    if audio:args+=['-f','lavfi','-i','sine=frequency=880:sample_rate=48000:duration=24','-map','0:v','-map','1:a','-c:a','libopus','-ac','2']
    else:args+=['-an']
    args+=['-c:v','libvpx-vp9','-threads','2','-filter_threads','1','-pix_fmt',pixels,'-profile:v','2' if pixels=='yuv420p10le' else '0',
           '-g','25','-cpu-used','4' if altref else '8','-lag-in-frames','25' if altref else '0','-auto-alt-ref',str(int(altref)),'-b:v','300k']
    if altref:
        args+=['-passlogfile',str(folder/'vp9-pass')]
        ok(args+['-pass','1','-f','null','-'],folder,'encode-pass1');args+=['-pass','2']
    args+=['-cluster_time_limit','1000','-live','1','-f','matroska' if matroska else 'webm',str(path)]
    ok(args,folder,'encode');return path

def video_rows(metadata,path):
    s=next(s for s in metadata['streams'] if s['codec_type']=='video');base=Fraction(s['time_base'])
    packets=[p for p in metadata['packets'] if p['stream_index']==s['index']]
    table=fragment_durations(path.read_bytes()) if path.suffix=='.mp4' else {}
    durations=table.get(int(s['id'],0)) if table else None
    rows=[]
    for i,p in enumerate(packets):
        duration=durations[i] if durations is not None else p.get('duration')
        rows.append([p['data_hash'],str(int(p['pts'])*base),str(int(p['dts'])*base),str(int(duration)*base) if duration is not None else None])
    return rows

def differences(left,right):
    changes=[]
    for n,(a,b) in enumerate(zip(left,right)):
        for k,label in enumerate(('payload','pts','dts','duration')):
            if a[k]!=b[k]:changes.append(dict(sample=n,field=label,source=a[k],output=b[k]))
    return dict(equalCount=len(left)==len(right),payloadsExact=len(left)==len(right) and all(a[0]==b[0] for a,b in zip(left,right)),
        ptsDtsExact=len(left)==len(right) and all(a[1:3]==b[1:3] for a,b in zip(left,right)),
        durationsExact=len(left)==len(right) and all(a[3]==b[3] for a,b in zip(left,right)),
        changedFields=len(changes),firstChanges=changes[:8])

def copy_oracle(source,pixels,folder,name,source_rows,source_frames):
    output=folder/(name+'.mp4')
    p=command(['ffmpeg','-v','error','-y','-i',source,'-map','0:v:0','-an','-c:v','copy','-fps_mode','passthrough','-copytb','1',
        '-avoid_negative_ts','disabled','-movflags','delay_moov+frag_keyframe+default_base_moof','-use_editlist','1',
        '-movie_timescale','1000','-video_track_timescale','1000',output],folder,name+'-copy')
    row=dict(exitCode=p.returncode,outputPath=str(output),fullSourceRead=True,managedGrowingPlayback=False)
    if p.returncode:return row
    metadata=probe(output,folder,name+'-probe');actual=video_rows(metadata,output)
    (folder/(name+'-packet-clocks.json')).write_text(json.dumps(actual,indent=2)+'\n')
    images=decoded(output,pixels,folder,name+'-decoded')
    row.update(outputSha256=sha(output),packetCount=len(actual),frameCount=len(images),
        decodedFramesIdentical=images==(source_frames[:len(images)] if name.startswith('prefix-') else source_frames),packetClocks=differences(source_rows[:len(actual)] if name.startswith('prefix-') else source_rows,actual),
        outputVideoTimebase=next(s['time_base'] for s in metadata['streams'] if s['codec_type']=='video'),sampleEntry=metadata['streams'][0]['codec_tag_string'])
    return row

def run(worker,index_runner,report):
    worker=Path(worker).resolve();index_runner=Path(index_runner).resolve();report=Path(report).resolve();report.mkdir(parents=True,exist_ok=True)
    info=json.loads(ok([worker,'--info'],report,'worker-info'));assert PROPOSED_MODE not in info['contract']['profiles']
    assert all(v['containerHint']=='mp4' for v in info['contract']['profiles'].values())
    result=dict(result='RUNNING',scope='fixture-native-index-and-copy-oracle-preflight-only',appVersion='0.2.0',sdkVersionUnchanged='1.95',
        proposedMode=PROPOSED_MODE,proposedModeAdvertised=False,publicPlayerTested=False,continuousWebmPlaybackAccepted=False,
        workerSha256=sha(worker),indexRunnerSha256=sha(index_runner),testSourceSha256=sha(__file__),rows=[])
    try:
        # Neither request opens a player or supplies media through IPC.
        for label,mode,fmt in [('format-webm','continuous-vp9-video-v1','webm'),('proposed-mode',PROPOSED_MODE,'mp4')]:
            p=command([worker],report,label,input_bytes=(json.dumps(dict(mode=mode,formatHint=fmt,length='4096',complete=False))+'\n').encode())
            messages=[json.loads(x) for x in p.stdout.splitlines()];assert p.returncode==1 and len(messages)==1 and messages[0]['result']['code']=='unsupported-stream-start'
            result[label]=dict(exitCode=p.returncode,messages=messages)
        for label,rate,pixels in PROFILES:
            folder=report/label;folder.mkdir(exist_ok=True);original=fixture(folder,rate,pixels);raw=original.read_bytes();layout=scan(raw)
            for unknown in (False,True):
                source=original
                if unknown:source=folder/'unknown-clusters.webm';source.write_bytes(unknown_clusters(raw,layout))
                name=label+('-unknown-clusters' if unknown else '-finite-clusters')
                inspect_source(worker,index_runner,source,pixels,rate,report/name,result,expected_native=not unknown,target=rate=='25')
        for label,options in [('missing-default-duration',{}),('alt-ref',dict(altref=True)),('muxed-opus',dict(audio=True)),('matroska-doctype',dict(matroska=True))]:
            folder=report/label;folder.mkdir(exist_ok=True);source=fixture(folder,'25','yuv420p',**options)
            if label=='missing-default-duration':
                data=bytearray(source.read_bytes());a,b,e=locate(data,0x23e383);length=e-a;assert length<128
                data[a:e]=bytes([0xec,0x80|(length-2)])+bytes(length-2);source.write_bytes(data)
            inspect_source(worker,index_runner,source,'yuv420p','25',folder,result,expected_native=label=='alt-ref',target=False,control=label)
        result.update(result='PASS-with-open-gates',ffmpegVersion=ok(['ffmpeg','-version'],report,'ffmpeg-version').decode().splitlines()[0],
            ffprobeVersion=ok(['ffprobe','-version'],report,'ffprobe-version').decode().splitlines()[0],count=len(result['rows']))
        assert result['count']==12
        (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result
    except BaseException:
        result.update(result='FAIL',error=traceback.format_exc());(report/'failure.json').write_text(json.dumps(result,indent=2)+'\n');raise

def inspect_source(worker,index_runner,source,pixels,rate,folder,result,*,expected_native,target,control=None):
    folder.mkdir(parents=True,exist_ok=True);raw=source.read_bytes();before=sha(source);layout=scan(raw)
    metadata=probe(source,folder,'source-probe');packets=video_rows(metadata,source);images=decoded(source,pixels,folder,'source-decoded')
    (folder/'source-packet-clocks.json').write_text(json.dumps(packets,indent=2)+'\n')
    # ffprobe WebM pos is a Block body, not its encoded payload. Verify the table independently.
    blocks=[b for b in layout['blocks'] if b['track']==1]
    assert len(blocks)==len(packets) and all(b['sha256']==p[0] for b,p in zip(blocks,packets))
    index=command([index_runner,source,'0','webm'],folder,'native-index')
    assert index.returncode==(0 if expected_native else 1),(folder,index.returncode,index.stderr)
    native=dict(exitCode=index.returncode,stderr=index.stderr.decode(errors='replace'),singleTrackInspectorOnly=True)
    if index.returncode==0:
        rows=list(csv.DictReader(io.StringIO(index.stdout.decode())));assert len(rows)==len(blocks)
        for row,b,p in zip(rows,blocks,packets):
            at,n=int(row['offset']),int(row['size']);assert 'SHA256:'+hashlib.sha256(raw[at:at+n]).hexdigest()==p[0]
            assert Fraction(int(row['start']),10000000)==Fraction(p[1])
            assert int(row['end'])-int(row['start'])==layout['defaultDurationNs']//100
        native.update(packetCount=len(rows),payloadsAndStartsExact=True,endsFromDefaultDuration100ns=True)
    oracle=copy_oracle(source,pixels,folder,'oracle',packets,images)
    if not control or control!='alt-ref':assert oracle['exitCode']==0,oracle
    if not control:
        assert oracle['packetCount']==len(packets) and oracle['frameCount']==len(images)>128
        assert oracle['packetClocks']['payloadsExact'] and oracle['packetClocks']['ptsDtsExact'] and oracle['decodedFramesIdentical']
    if target:assert oracle['packetClocks']['durationsExact'] and layout['defaultDurationNs']==40000000
    prefix=None
    if not control:
        chosen=next(c for c in layout['clusters'] if any(c['offset']<=b['offset']<c['end'] and Fraction(p[1])>=12 for b,p in zip(blocks,packets)))
        prefix_source=folder/'confirmed-prefix.webm';prefix_source.write_bytes(raw[:chosen['end']]);assert prefix_source.stat().st_size<len(raw)
        prefix=copy_oracle(prefix_source,pixels,folder,'prefix-oracle',packets,images)
        assert prefix['exitCode']==0 and 0<prefix['packetCount']<len(packets)
        assert prefix['packetClocks']['payloadsExact'] and prefix['packetClocks']['ptsDtsExact'] and prefix['decodedFramesIdentical']
        if target:assert prefix['packetClocks']['durationsExact']
        prefix.update(confirmedSourceBytes=prefix_source.stat().st_size,sourceBytes=len(raw),physicalPrefixEof=True,writerCompleteSimulated=False)
    rejection,data=serve(worker,source,mode='continuous-vp9-video-v1',cut_seconds=(20,),short_reads=True)
    assert rejection['exitCode']==1 and rejection['result']['code']=='unsupported-stream-vp9-configuration' and not data and not rejection['result']['sourceComplete']
    assert sha(source)==before
    invisible=sum(b['hidden'] or b['showExisting'] for b in blocks);indexed=sum(b['indexedSuperframe'] for b in blocks)
    if control=='alt-ref':assert invisible or indexed,'Alt-ref control must contain actual hidden/indexed packets'
    row=dict(profile=folder.name,control=control,targetFirstProfile=target,frameRate=rate,pixelFormat=pixels,sourcePath=str(source),sourceSha256=before,
        sourceBytes=len(raw),videoPackets=len(packets),decodedFrames=len(images),sourceStreams=[s['codec_name'] for s in metadata['streams']],
        layout={k:v for k,v in layout.items() if k not in ('blocks','clusters')},clusterCount=len(layout['clusters']),unknownClusters=sum(c['unknownSize'] for c in layout['clusters']),
        simpleBlocks=sum(not b['blockGroup'] for b in layout['blocks']),blockGroups=sum(b['blockGroup'] for b in layout['blocks']),
        lacedBlocks=sum(b['lacing']!=0 for b in layout['blocks']),leadingHiddenOrExistingPackets=invisible,indexedSuperframePackets=indexed,blockInvisibleFlags=sum(bool(b['flags']&8) for b in blocks),
        missingPacketDurations=sum(p[3] is None for p in packets),videoOnlyOracleDropsAudio=any(s['codec_type']=='audio' for s in metadata['streams']),
        nativeSingleTrackIndex=native,fullCopyOracle=oracle,prefixCopyOracle=prefix,existingMp4ModeRejection=rejection,sourceUnchanged=True)
    result['rows'].append(row);(folder.parent/'progress.json').write_text(json.dumps(dict(completed=[r['profile'] for r in result['rows']]))+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--worker',required=True);p.add_argument('--index-runner',required=True);p.add_argument('--report-directory',required=True);a=p.parse_args()
    began=time.monotonic();r=run(a.worker,a.index_runner,a.report_directory);print(json.dumps(dict(result=r['result'],count=r['count'],seconds=time.monotonic()-began)))
