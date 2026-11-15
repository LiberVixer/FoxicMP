"""Actual ordinary player admission, required Opus, growing seek and release."""
import copy,json,shutil,time
from pathlib import Path
from fractions import Fraction
from progressive_client import ApiError
from progressive_continuous import ContinuousRemux,WEBM_VP9_OPUS_MODE,WEBM_VP9_OPUS_NTSC_MODE,WEBM_VP9_OPUS_CFR_MODE,WEBM_VP9_OPUS_VFR_MODE,WEBM_VP9_OPUS_SIMPLE_VFR_MODE,WEBM_VP9_OPUS_AUTO_VFR_MODE,WEBM_VP9_OPUS_NOMINAL_VFR_MODE
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_player_crash_recovery import wait,sha
from progressive_webm_opus_remux_preflight import fixture,PROFILES,probe,pcm,packets
from progressive_webm_remux_preflight import unknown_clusters,scan
MODE=WEBM_VP9_OPUS_MODE

def exercise(peers,root,report,expected_audio='null',ntsc=False,cfr=False,vfr=False,simple=False,auto_count=False,nominal=False):
 auto_count=auto_count or nominal
 simple=simple or auto_count
 global MODE
 MODE=WEBM_VP9_OPUS_NOMINAL_VFR_MODE if nominal else WEBM_VP9_OPUS_AUTO_VFR_MODE if auto_count else WEBM_VP9_OPUS_SIMPLE_VFR_MODE if simple else WEBM_VP9_OPUS_VFR_MODE if vfr else WEBM_VP9_OPUS_CFR_MODE if cfr else WEBM_VP9_OPUS_NTSC_MODE if ntsc else WEBM_VP9_OPUS_MODE
 vfr=vfr or simple
 options=dict(rate='30000/1001') if ntsc else {}
 report=Path(report);rows=[];current=None;client=peers[0].client
 def poll(sdk,predicate,label,timeout=25):
  def tick():
   s=sdk.status();(report/'last-job.json').write_text(json.dumps(s,indent=2));assert not s['errorCode'],s
   if not s['outputComplete']:
    try:sdk.heartbeat()
    except ApiError as e:assert e.code=='continuous-closed',e.reply
   return predicate(s)
  return wait(tick,label,timeout)
 def playback(sdk,predicate,label,timeout=25):
  def check(_):
   s=sdk.playback_status();(report/'last-playback.json').write_text(json.dumps(s,indent=2));assert s['state'] not in ('error','closed') and s['audio'] is True and s['mode']==MODE,s
   return s if predicate(s) else None
  return poll(sdk,check,label,timeout)
 try:
  from progressive_webm_cfr_fixtures import CASES,media
  from progressive_webm_vfr_fixtures import CASES as VFR_CASES,media as vfr_media
  from progressive_webm_simple_vfr_fixtures import CASES as SIMPLE_CASES,media as simple_media
  if nominal:
   from progressive_webm_nominal_vfr_fixtures import CASES as SIMPLE_CASES,media as simple_media
  if simple:VFR_CASES,vfr_media=SIMPLE_CASES,simple_media
  for case in (VFR_CASES if vfr else CASES if cfr else (PROFILES[0],PROFILES[1],PROFILES[4])):
   if vfr:
    name=case[0];folder=root/name;original,pixels,frame_count,video_duration,_,profile=vfr_media(folder,case);rate=None
   elif cfr:
    name,n,d,profile=case;folder=root/name;original,pixels,frame_count,video_duration,_=media(folder,case);rate=dict(numerator=n,denominator=d)
   else:
    profile=case;name=profile[0];folder=root/name;original,pixels=fixture(folder,profile,**options);video_duration=240240000 if ntsc else 240000000;rate=None
   raw=original.read_bytes()
   if profile[1]:raw=unknown_clusters(raw,scan(raw))
   directory=root/(name+'-session');directory.mkdir();path=directory/'video.part';model=snapshot(path,raw,complete=False,prefix=4);model['timeline']['duration']=str(video_duration);model['tracks'][0].update(codecHint='vp9',containerHint='webm');sound=copy.deepcopy(model['tracks'][0]);sound.update(id='sound',kind='audio',codecHint='opus');model['tracks'].append(sound)
   if not rows:
    import uuid
    for invalid in (None,True,1152000,'0','648','48000001'):
     command=dict(version=1,action='continuousRemuxStart',mode=MODE,requestId=uuid.uuid4().hex,sessionDirectory=str(directory),session=model,resourceId='clip')
     if invalid is not None:command['audioSamples']=invalid
     if vfr and not auto_count:command['videoSamples']=str(frame_count)
     if cfr:command['videoRate']=rate
     try:client.request(command)
     except ApiError:pass
     else:raise AssertionError(('Invalid Opus sample count admitted',invalid))
   if (ntsc or cfr) and not rows:
    for duration in (('240240002','240239998','1') if cfr else ('240000000','240240002','240239998','1')):
     bad=copy.deepcopy(model);bad['timeline']['duration']=duration
     command=dict(version=1,action='continuousRemuxStart',mode=MODE,requestId=uuid.uuid4().hex,sessionDirectory=str(directory),session=bad,resourceId='clip',audioSamples=str(profile[5]))
     if vfr and not auto_count:command['videoSamples']=str(frame_count)
     if cfr:command['videoRate']=rate
     try:client.request(command)
     except ApiError as e:assert e.code=='unsupported-stream-source',e.reply
     else:raise AssertionError('Inexact NTSC timeline admitted')
   if cfr and not rows:
    for invalid_rate in (None,dict(numerator=True,denominator=1),dict(numerator=24000.,denominator=1001),dict(numerator=48000,denominator=2002),dict(numerator=27,denominator=1),dict(numerator=24,denominator=0),dict(numerator=24000,denominator=1001,extra=1)):
     command=dict(version=1,action='continuousRemuxStart',mode=MODE,requestId=uuid.uuid4().hex,sessionDirectory=str(directory),session=model,resourceId='clip',audioSamples=str(profile[5]))
     if invalid_rate is not None:command['videoRate']=invalid_rate
     try:client.request(command)
     except ApiError:pass
     else:raise AssertionError('Invalid CFR rate admitted')
   if vfr and not auto_count and not rows:
    import uuid
    for invalid in (None,True,frame_count,0.5,'0','50001','01'):
     command=dict(version=1,action='continuousRemuxStart',mode=MODE,requestId=uuid.uuid4().hex,sessionDirectory=str(directory),session=model,resourceId='clip',audioSamples=str(profile[5]))
     if invalid is not None:command['videoSamples']=invalid
     try:client.request(command)
     except ApiError:pass
     else:raise AssertionError(('Invalid frame plan admitted',invalid))
   fields=dict(mode=MODE,audio_samples=profile[5])
   if vfr and not auto_count:fields['video_samples']=frame_count
   if cfr:fields['video_rate']=(n,d)
   sdk=current=ContinuousRemux(client,str(directory),**fields);sdk.prepare(model,'clip')
   replay=dict(version=1,action='continuousRemuxStart',mode=MODE,requestId=sdk.request_id,sessionDirectory=str(directory),session=copy.deepcopy(model),resourceId='clip',audioSamples=str(profile[5]))
   if vfr and not auto_count:replay['videoSamples']=str(frame_count)
   if cfr:replay['videoRate']=rate
   assert client.request(replay)['jobId']==sdk.job_id
   conflict=copy.deepcopy(replay);conflict['audioSamples']=str(profile[5]+1)
   try:client.request(conflict)
   except ApiError as e:assert e.code=='continuous-conflict',e.reply
   else:raise AssertionError('Opus sample plan identity changed')
   if vfr and not auto_count:
    conflict=copy.deepcopy(replay);conflict['videoSamples']=str(frame_count+1)
    try:client.request(conflict)
    except ApiError as e:assert e.code=='continuous-conflict',e.reply
    else:raise AssertionError('Frame plan identity changed')
   if auto_count:
    for invalid in (None,True,0,'0',str(frame_count),'50001'):
     conflict=copy.deepcopy(replay);conflict['requestId']=uuid.uuid4().hex;conflict['videoSamples']=invalid
     try:client.request(conflict)
     except ApiError as e:assert e.code=='invalid_request',e.reply
     else:raise AssertionError('Auto-count accepted an explicit frame plan')
   if cfr:
    conflict=copy.deepcopy(replay);conflict['videoRate']=dict(numerator=25 if n!=25 else 30,denominator=1)
    try:client.request(conflict)
    except ApiError as e:assert e.code=='continuous-conflict',e.reply
    else:raise AssertionError('Video rate identity changed')
   waiting=poll(sdk,lambda s:s if s['producerCode']=='waiting' else None,'partial EBML');assert not waiting['playbackReady']
   def publish(count,complete=False):
    nonlocal model
    old=model
    with path.open('ab') as f:f.write(raw[path.stat().st_size:count])
    model=copy.deepcopy(model);model['revision']=str(int(model['revision'])+1);phase='complete' if complete else 'downloading';model['writer']['state']=model['resources'][0]['state']=phase
    for t in model['tracks']:t['state']=phase
    model['resources'][0]['ranges']=[dict(offset='0',length=str(count))];model=protect_snapshot(model,directory,previous=old);sdk.publish(model)
    return poll(sdk,lambda s:s if s['appliedRevision']==model['revision'] else None,'applied bytes')
   head=scan(raw)['headBytes'];publish(head+3);partial=poll(sdk,lambda s:s if s['producerCode']=='waiting' else None,'partial Cluster');assert not partial['playbackReady']
   meta=probe(original,folder,'probe-source');clock=Fraction(next(s for s in meta['streams'] if s['codec_type']=='video')['time_base']);cut=next(int(p['pos'])+13 for p in meta['packets'] if p['codec_type']=='video' and int(p['pts'])*clock>=20)
   publish(cut);ready=poll(sdk,lambda s:s if s['playbackReady'] and s['indexedEndSeconds']>=12 else None,'Opus A/V ready');assert not ready['outputComplete']
   if auto_count:
    assert 'videoSamples' not in ready and not ready['videoSampleCountFinal'] and 0<int(ready['indexedVideoSamples'])<frame_count,ready
   sdk.play(title='WebM Opus '+name);running=playback(sdk,lambda s:s['state']=='playing' and s['positionSeconds']>.2,'Opus decoding')
   if peers[0].edition=='qt':assert running['audioCodec']=='opus' and running['audioOutput']==expected_audio and not running['pcmWindowApplied'],running
   began=time.monotonic();viewed=playback(sdk,lambda s:s['state']=='playing' and s['positionSeconds']>=running['positionSeconds']+10.1 and time.monotonic()-began>=10,'incomplete playback',20)
   sdk.control('pause');paused=playback(sdk,lambda s:s['state']=='paused','pause');sdk.control('seek',seconds=1,allow_growing=True);seeking=playback(sdk,lambda s:s['state']=='paused' and abs(s['positionSeconds']-1)<.12,'growing seek')
   sdk.control('seek',seconds=22,allow_growing=True,wait_for_range=True);queued=playback(sdk,lambda s:s['pendingSeekSeconds']==22 and s['seekState']=='waiting-range','queued seek')
   publish(len(raw),True);complete=poll(sdk,lambda s:s if s['outputComplete'] else None,'sealed source EOF');applied=playback(sdk,lambda s:s['pendingSeekSeconds'] is None and s['seekState']=='applied' and s['state']=='paused' and abs(s['positionSeconds']-22)<.12,'queued seek commit')
   if auto_count:
    for state in (complete,applied):assert 'videoSamples' not in state and state['videoSampleCountFinal'] and int(state['indexedVideoSamples'])==frame_count,state
   cache=Path(str(peers[0].registry)+'.continuous-cache');files=list(cache.glob('remux-*/output.fmp4'));assert len(files)==1;output=report/(name+'-sealed.webm');shutil.copyfile(files[0],output);actual=probe(output,folder,'probe-output')
   for kind in ('video','audio'):assert packets(meta,kind)==packets(actual,kind)
   assert pcm(original,folder,'pcm-source')==pcm(output,folder,'pcm-output')
   sdk.control('play');finished=playback(sdk,lambda s:s['state']=='finished','natural EOF',12);digest=sha(path);began=time.monotonic();released=sdk.close(timeout=2);elapsed=time.monotonic()-began;forgotten=sdk.forget(timeout=2);current=None
   assert released['sourceFilesClosed'] and released['released'] and elapsed<2 and forgotten['forgotten'] and sha(path)==digest and path.read_bytes()==raw and not list(cache.glob('remux-*'))
   rows.append(dict(profile=name,nominalDefaultDuration=scan(raw)["defaultDurationNs"] if nominal else None,derivedVideoCount=auto_count,videoRate=rate,videoSamples=frame_count if vfr else None,result='PASS',waiting=waiting,partial=partial,ready=ready,running=running,viewedBeforeComplete=viewed,paused=paused,growingSeek=seeking,queued=queued,applied=applied,complete=complete,finished=finished,releaseSeconds=elapsed,exactPcm=True,exactPacketsAndClocks=True,sourceUnchanged=True));(report/'progress.json').write_text(json.dumps(dict(completed=[r['profile'] for r in rows])))
 finally:
  if current:
   try:current.close(timeout=2);current.forget(timeout=2)
   except Exception:pass
 return dict(result='PASS',count=len(rows),rows=rows,mode=MODE,realIncompletePlayback=True,requiredAudio=True,physicalAvSyncMeasured=False)
