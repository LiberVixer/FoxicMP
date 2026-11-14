"""Player-only WebM/VP9/Opus clock, trim and sealed-Cluster reference study.

No new playback mode is advertised. Native projection is diagnostic ResourceIO,
not ordinary player/DirectShow/libmpv playback or physical A/V acceptance.
"""
import argparse,copy,hashlib,json,struct,subprocess,traceback,uuid,tempfile,sys
from fractions import Fraction
from pathlib import Path
from progressive_webm_fixtures import element,width
from progressive_webm_remux_preflight import scan,unknown_clusters,decoded,video_rows,differences,command,ok,sha
from progressive_stream_remux_worker_test import serve
from progressive_mux_ts_conformance import Rpc
from progressive_growing_ts_fixture import publish

PROFILES=[('p0-mono-20',False,1,'20','audio',1152000,False),
 ('p2-indexed-stereo-20',True,2,'20','audio',1152000,False),
 ('p0-stereo-10',False,2,'10','audio',1152000,False),
 ('p0-lowdelay-mono-5',False,1,'5','lowdelay',1152000,False),
 ('p0-short-stereo-20',False,2,'20','audio',95983,False),
 ('p2-one-sample-audio-first',True,1,'20','audio',1,True)]

def children(raw,start,end):
 while start<end:
  tag,body,stop=element(raw,start);assert stop is not None and stop<=end
  yield tag,start,body,stop;start=stop

def configuration(raw):
 _,_,at=element(raw,0);_,body,_=element(raw,at)
 info=None;tracks={}
 for tag,at,b,e in children(raw,body,scan(raw)['headBytes']):
  if tag==0x1654ae6b:
   for kind,_,tb,te in children(raw,b,e):
    assert kind==0xae
    fields={t:(a,z) for t,_,a,z in children(raw,tb,te)}
    number=lambda t:int.from_bytes(raw[slice(*fields[t])],'big')
    entry=dict(number=number(0xd7),kind=number(0x83),codec=raw[slice(*fields[0x86])].decode())
    if entry['kind']==2:
     head=raw[slice(*fields[0x63a2])];assert len(head)==19 and head[:8]==b'OpusHead' and head[8]==1 and head[18]==0
     entry.update(channels=head[9],preSkip=int.from_bytes(head[10:12],'little'),inputRate=int.from_bytes(head[12:16],'little'),gain=int.from_bytes(head[16:18],'little',signed=True),codecDelayNs=number(0x56aa),seekPreRollNs=number(0x56bb),opusHeadHex=head.hex())
    tracks[entry['kind']]=entry
 assert set(tracks)=={1,2} and tracks[1]['codec']=='V_VP9' and tracks[2]['codec']=='A_OPUS'
 audio=tracks[2];assert audio['channels'] in (1,2) and audio['codecDelayNs']*48000==audio['preSkip']*1000000000 and audio['seekPreRollNs']==80000000
 return tracks

def padding_map(raw,layout):
 result={}
 for c in layout['clusters']:
  for tag,_,b,e in children(raw,c['body'],c['end']):
   if tag==0xa0:
    fields={t:(at,a,z) for t,at,a,z in children(raw,b,e)};assert 0xa1 in fields
    result[fields[0xa1][0]]=int.from_bytes(raw[slice(*fields[0x75a2][1:])],'big',signed=True) if 0x75a2 in fields else 0
 return result

def toc_samples(packet):
 toc=packet[0];config=toc>>3;code=toc&3
 frame=(120<<(config&3)) if config>=16 else (480<<(config&1)) if config>=12 else (480,960,1920,2880)[config&3]
 count=1 if code==0 else packet[1]&63 if code==3 else 2
 assert 0<count<=48 and frame*count<=5760;return frame*count

def probe(path,folder,name):return json.loads(ok(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',path],folder,name))

def pcm(path,folder,name,manual=False):
 args=['ffmpeg','-v','error','-threads','1',*(['-flags2','+skip_manual'] if manual else []),'-i',path,'-map','0:a:0','-threads','1','-c:a','pcm_f32le','-f','f32le','-']
 raw=ok(args,folder,name);(folder/(name+'.stdout')).unlink()
 (folder/(name+'.summary.json')).write_text(json.dumps(dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest()))+'\n');return raw

def fixture(folder,profile):
 name,ten,channels,ms,application,samples,audio_first=profile;folder.mkdir(parents=True,exist_ok=True)
 pixels='yuv420p10le' if ten else 'yuv420p';video=folder/'video.webm'
 args=['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=160x90:rate=25:duration=24','-an','-c:v','libvpx-vp9','-threads','2','-filter_threads','1','-pix_fmt',pixels,'-profile:v','2' if ten else '0','-g','25','-cpu-used','4' if ten else '8','-lag-in-frames','25' if ten else '0','-auto-alt-ref',str(int(ten)),'-b:v','300k']
 if ten:
  args+=['-passlogfile',str(folder/'pass')];ok(args+['-pass','1','-f','null','-'],folder,'encode-video-pass1');args+=['-pass','2']
 ok(args+['-live','1',str(video)],folder,'encode-video')
 source=folder/'source.webm'
 # The first input is already exactly25fps video with a zero presentation origin.
 args=['ffmpeg','-v','error','-y','-i',video,'-f','lavfi','-i','sine=frequency=997:sample_rate=48000:duration=24','-map','1:a' if audio_first else '0:v','-map','0:v' if audio_first else '1:a','-c:v','copy','-c:a','libopus','-ac',str(channels),'-frame_duration',ms,'-application',application,'-af',f'atrim=end_sample={samples}','-cluster_time_limit','1000','-live','1',source]
 ok(args,folder,'encode-mux');return source,pixels

def seal(raw,layout,count=None):
 """Test-only reference with a full-fixture boundary oracle; not a managed parser."""
 selected=layout['clusters'][:count];out=bytearray(raw[:layout['headBytes']])
 for c in selected:
  part=bytearray(raw[c['offset']:c['end']]);at=width(part[0],4);n=c['body']-c['offset']-at;size=c['end']-c['body'];assert size<(1<<(7*n))-1
  part[at:at+n]=((1<<(7*n))|size).to_bytes(n,'big');out+=part
 return bytes(out)

def packets(metadata,kind):
 stream=next(s for s in metadata['streams'] if s['codec_type']==kind);clock=Fraction(stream['time_base'])
 return [[p['data_hash'],str(int(p['pts'])*clock),str(int(p['dts'])*clock)] for p in metadata['packets'] if p['stream_index']==stream['index']]

def native_projection(runner,raw,folder,source_video,source_pcm,pixels,channels,complete,strict_pcm=True):
 folder.mkdir(exist_ok=True);runtime=tempfile.TemporaryDirectory(prefix='fox-webm-opus-native-');directory=Path(runtime.name)/'session';directory.mkdir();registry=Path(runtime.name)/'registry'
 model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='250000000',timestampOffset='0'),resources=[dict(id='mux',name='mux.part',length=str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id=kind,kind=kind,required=True,codecHint=codec,containerHint='webm-muxed',state='downloading',initializationReady=True,source=dict(resourceId='mux')) for kind,codec in [('video','vp9'),('audio','opus')]])
 value=publish(model,raw,directory,len(raw),complete=complete);rpc=Rpc([str(runner),'--registry',str(registry)])
 try:
  opened=rpc('open',directory=str(directory),session=value)
  if not opened['ok']:
   assert not complete and not strict_pcm and opened['code']=='waiting_initialization',opened
   assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
   return dict(result='OPEN-prefix-no-audio-yet',nativeReadOnlyProjection=True,ordinaryPlayerTested=False,complete=False,code=opened['code'],lastOwnerReleased=True,expectedPcmSamples=len(source_pcm)//(4*channels))
  index=rpc('index');layout=rpc('layout');views=[];ends=[]
  for kind in (0,1):
   size=int(layout['tracks'][kind]['available']);data=bytearray()
   for at in range(0,size,65536):
    reply=rpc('read',track=kind,offset=str(at),length=str(min(65536,size-at)));assert reply['ok'] and reply['bytesRead']>0,reply;data+=bytes.fromhex(reply['hex'])
   view=folder/('video.webm' if kind==0 else 'audio.webm');view.write_bytes(data);views.append(view)
   end=rpc('read',decoder=True,track=kind,offset=str(size),length='1');assert end['bytesRead']==(0 if complete else -1),end;ends.append(end)
  images=decoded(views[0],pixels,folder,'native-video');actual=pcm(views[1],folder,'native-pcm');video_matches=images==source_video[:len(images)];pcm_matches=actual==source_pcm[:len(actual)];assert video_matches
  if strict_pcm:assert pcm_matches
  if complete:
   assert images==source_video
   if strict_pcm:assert actual==source_pcm
  readers=rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];assert readers
  rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
  return dict(result='PASS' if pcm_matches else 'OPEN-one-packet-trim',nativeReadOnlyProjection=True,ordinaryPlayerTested=False,complete=complete,videoFrames=len(images),pcmSamples=len(actual)//(4*channels),pcmSha256=hashlib.sha256(actual).hexdigest(),decodedVideoIdentical=video_matches,decodedPcmMatchesExpectedWindow=pcm_matches,expectedPcmSamples=len(source_pcm)//(4*channels),lastOwnerReleased=True,writerIncompleteStillWaits=not complete,indexSampleCounts=[t['samples'] for t in index['tracks']],viewsSha256=[sha(p) for p in views])
 finally:
  primary=sys.exc_info()[1]
  try:rpc.close()
  except BaseException as cleanup:
   if primary is None:raise
   primary.add_note('Native RPC cleanup: '+repr(cleanup))
  finally:runtime.cleanup()

def run(worker,runner,index_runner,report,only=None):
 report=Path(report).resolve();report.mkdir(parents=True,exist_ok=True)
 info=json.loads(ok([worker,'--info'],report,'worker-info'));assert not any(p.get('audioCodec')=='opus' for p in info['contract']['profiles'].values())
 result=dict(result='RUNNING',scope='preflight-only-no-new-mode',sdkVersionUnchanged='1.97',workerSha256=sha(worker),nativeRunnerSha256=sha(runner),indexRunnerSha256=sha(index_runner),testSourceSha256=sha(__file__),publicPlayerTested=False,sourceInstallationChanged=False,rows=[])
 try:
  for profile in PROFILES:
   if only and profile[0]!=only:continue
   folder=report/profile[0];source,pixels=fixture(folder,profile);original_raw=source.read_bytes();original_layout=scan(original_raw)
   for unknown in (False,True):
    name='unknown' if unknown else 'finite';data=folder/name;data.mkdir(exist_ok=True);path=data/'input.webm';raw=unknown_clusters(original_raw,original_layout) if unknown else original_raw;path.write_bytes(raw);before=sha(path);layout=scan(raw);config=configuration(raw);audio=config[2];stride=audio['channels']*4
    pad=padding_map(raw,layout);ab=[b for b in layout['blocks'] if b['track']==audio['number']];vb=[b for b in layout['blocks'] if b['track']==config[1]['number']];assert len(vb)==600
    declared=[toc_samples(raw[b['payload']:b['end']]) for b in ab];encoded=sum(declared);padding_ns=pad.get(ab[-1]['offset'],0);padding_samples=(padding_ns*48000+500000000)//1000000000
    assert padding_ns in (padding_samples*1000000000//48000,(padding_samples*1000000000+47999)//48000)
    assert encoded-audio['preSkip']-padding_samples==profile[5]
    metadata=probe(path,data,'source-probe');images=decoded(path,pixels,data,'source-video');standard=pcm(path,data,'source-pcm');manual=pcm(path,data,'source-untrimmed-pcm',True)
    assert len(images)==600 and len(manual)//stride==encoded
    expected_pcm=manual[audio['preSkip']*stride:(encoded-padding_samples)*stride];assert len(expected_pcm)//stride==profile[5]
    if profile[5]>1:assert standard==expected_pcm
    for kind,blocks in [('video',vb),('audio',ab)]:assert [b['sha256'] for b in blocks]==[row[0] for row in packets(metadata,kind)]
    origins=[];cursor=0
    for block,n in zip(ab,declared):
     origins.append(Fraction(block['ticks']*layout['timestampScaleNs']*48000,1000000000)-cursor);cursor+=n
    row=dict(profile=profile[0]+'-'+name,sourceSha256=before,sourceBytes=len(raw),channels=audio['channels'],packetMilliseconds=profile[3],audioFirst=profile[6],audio=audio,encodedSamples=encoded,expectedPresentationSamples=profile[5],discardPaddingNs=padding_ns,discardPaddingSamples=padding_samples,rawPcmWindowExact=standard==expected_pcm,expectedPcmSha256=hashlib.sha256(expected_pcm).hexdigest(),standardDecodedPcmSamples=len(standard)//stride,sourcePcmSha256=hashlib.sha256(standard).hexdigest(),audioPackets=len(ab),firstAudioPayloadOffset=ab[0]['payload'],videoPackets=len(vb),indexedSuperframePackets=sum(b['indexedSuperframe'] for b in vb),audioContainerClockResidualSamples=sorted(str(n) for n in set(origins)),unknownClusters=unknown,references=[])
    if profile[1]:assert row['indexedSuperframePackets']>0
    # Native clock is raw Track Ticks minus exact CodecDelay, not an integer-ms demuxer's rounded PTS.
    row['audioContainerFirstPts']=str(Fraction(ab[0]['ticks']*layout['timestampScaleNs']-audio['codecDelayNs'],1000000000));row['audioDemuxFirstPts']=packets(metadata,'audio')[0][1]
    for variant,extra in [('mp4-default',[]),('mp4-copyts',['-copyts'])]:
     output=data/(variant+'.mp4');ok(['ffmpeg','-v','error','-y',*extra,'-i',path,'-map','0','-c','copy','-avoid_negative_ts','disabled','-movflags','delay_moov+frag_keyframe+default_base_moof','-use_editlist','1',output],data,variant+'-copy')
     other=probe(output,data,variant+'-probe');other_pcm=pcm(output,data,variant+'-pcm');other_raw=pcm(output,data,variant+'-untrimmed-pcm',True)
     assert packets(metadata,'video')[0][0]==packets(other,'video')[0][0]
     for kind in ('video','audio'):assert [r[0] for r in packets(metadata,kind)]==[r[0] for r in packets(other,kind)]
     assert other_raw==manual
     assert decoded(output,pixels,data,variant+'-video')==images
     body=output.read_bytes();at=body.index(b'dOps')+4;dops=body[at:at+11];elst=body.rfind(b'elst',0,at)+4;assert elst>4 and body[elst]==0
     edit=int.from_bytes(body[elst+12:elst+16],'big',signed=True);opus_skip=int.from_bytes(dops[2:4],'big');assert opus_skip==audio['preSkip']
     native=command([index_runner,output,'1','opus'],data,variant+'-native-index')
     row['references'].append(dict(variant=variant,exitCode=0,outputSha256=sha(output),packetsCopiedExactly=True,untrimmedPcmExact=True,videoFramesExact=True,videoFirstPtsDelta=str(Fraction(packets(other,'video')[0][1])-Fraction(packets(metadata,'video')[0][1])),audioFirstPtsDelta=str(Fraction(packets(other,'audio')[0][1])-Fraction(packets(metadata,'audio')[0][1])),pcmSamples=len(other_pcm)//stride,pcmMatchesSource=other_pcm==standard,sourcePcmPrefixExact=other_pcm[:len(standard)]==standard,editMediaTime=edit,dOpsPreSkip=opus_skip,editMatchesPreSkip=edit==opus_skip,nativeAudioIndexExitCode=native.returncode,nativeAudioIndexError=native.stderr.decode(errors='replace').strip()))
    sealed=seal(raw,layout);sealed_path=data/'sealed.webm';sealed_path.write_bytes(sealed);sealed_layout=scan(sealed);assert all(not c['unknownSize'] for c in sealed_layout['clusters'])
    assert [b['sha256'] for b in sealed_layout['blocks']]==[b['sha256'] for b in layout['blocks']]
    assert [(b['ticks'],b['track']) for b in sealed_layout['blocks']]==[(b['ticks'],b['track']) for b in layout['blocks']]
    assert configuration(sealed)==config and list(padding_map(sealed,sealed_layout).values())==list(pad.values())
    row['nativeSealedFull']=native_projection(runner,sealed,data/'native-full',images,expected_pcm,pixels,audio['channels'],True,strict_pcm=profile[5]>1)
    closed=next(i for i,c in enumerate(layout['clusters']) if any(c['offset']<=b['offset']<c['end'] and b['ticks']>=12000 for b in vb));assert closed+1<len(layout['clusters'])
    prefix=seal(raw,layout,closed+1);assert len(prefix)<len(raw);row['nativeSealedPrefix']=native_projection(runner,prefix,data/'native-prefix',images,expected_pcm,pixels,audio['channels'],False,strict_pcm=profile[5]>1)
    row['prefixBoundaryOracle']=dict(fullFixtureLayoutUsed=True,nextClusterHeaderConfirmedThrough=layout['clusters'][closed+1]['body'],sourceBytes=len(raw),sealedOutputBytes=len(prefix),audioPacketsWithinClosedClusters=sum(b['end']<=layout['clusters'][closed]['end'] for b in ab),productionScannerTested=False)
    rejected=[]
    for mode in ('continuous-webm-vp9-video-v1','continuous-webm-vp9-superframe-video-v1'):
     answer,output=serve(worker,path,mode=mode,format_hint='webm',video_samples=600,growing=False);assert answer['exitCode']==1 and answer['result']['code'].startswith('unsupported-') and not output
     rejected.append(dict(mode=mode,exitCode=answer['exitCode'],code=answer['result']['code']))
    row['oldVideoModesRejectAudio']=rejected;assert sha(path)==before;row['sourceUnchanged']=True
    result['rows'].append(row);(report/'progress.json').write_text(json.dumps(dict(completed=[r['profile'] for r in result['rows']])))
  result.update(result='PASS-with-open-integration-gates',count=len(result['rows']),ffmpegVersion=ok(['ffmpeg','-version'],report,'ffmpeg-version').decode().splitlines()[0],ffprobeVersion=ok(['ffprobe','-version'],report,'ffprobe-version').decode().splitlines()[0]);(report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result
 except BaseException:
  result.update(result='FAIL',error=traceback.format_exc());(report/'failure.json').write_text(json.dumps(result,indent=2)+'\n');raise

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--worker',required=True);p.add_argument('--runner',required=True);p.add_argument('--index-runner',required=True);p.add_argument('--report-directory',required=True);p.add_argument('--profile');a=p.parse_args();r=run(Path(a.worker).resolve(),Path(a.runner).resolve(),Path(a.index_runner).resolve(),a.report_directory,a.profile);print(json.dumps(dict(result=r['result'],count=r['count'])))
