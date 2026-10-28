"""Confirmed-byte seeks with mixed/all-fMP4/all-WebM multi-audio and SRT."""
import argparse,json,sys,time,uuid
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_client import ProgressiveClient
from progressive_v2 import V2PreviewSession
from progressive_v1_conformance import fragment_prefix
from progressive_webm_fixtures import element
from progressive_v2_playback_conformance import forget_closed_readers

def webm_time_prefix(data,seconds):
    # A muxer may split clusters at keyframes as well as the time limit.
    # Choose by cluster clock, never equate the number of clusters to seconds.
    _,_,at=element(data,0);_,at,limit=element(data,at);scale=1000000
    while at<(limit or len(data)):
      tag,body,end=element(data,at)
      if tag==0x1549a966:
        cursor=body
        while cursor<end:
          kind,start,stop=element(data,cursor)
          if kind==0x2ad7b1:scale=int.from_bytes(data[start:stop],'big')
          cursor=stop
      if tag==0x1f43b675:
        cursor=body
        while cursor<end:
          kind,start,stop=element(data,cursor)
          if kind==0xe7 and int.from_bytes(data[start:stop],'big')*scale>=seconds*1000000000:return end
          cursor=stop
      at=end
    raise ValueError('Fixture has no cluster at requested time')

def exercise(client,fixtures,work,profile):
    root=Path(work);root.mkdir(parents=True)
    hidden=profile.endswith('-hidden-tail');known=profile.endswith('-known') or hidden;media_profile=profile.removesuffix('-known').removesuffix('-hidden-tail')
    video={'mixed-hevc10':('video-hevc10.mp4','hevc'),'mixed-av1-10':('video-av1-10.mp4','av1'),'mixed-vp8':('video-vp8.webm','vp8')}.get(media_profile)
    base='mixed' if video else media_profile
    sources={'mixed':['video.mp4','audio.m4a','opus.webm','vorbis.webm'],'fmp4':['video.mp4','audio.m4a','opus.mp4','audio.m4a'],'webm':['video.webm','opus.webm','opus.webm','vorbis.webm']}[base]
    codecs={'mixed':['h264','aac-lc','opus','vorbis'],'fmp4':['h264','aac-lc','opus','aac-lc'],'webm':['vp9','opus','opus','vorbis']}[base]
    if video:sources[0]=video[0];codecs[0]=video[1]
    value=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='900000000',timestampOffset='0'),resources=[],tracks=[])
    data=[]
    for i,name in enumerate(sources):
      content=(Path(fixtures)/'progressive-seek'/name).read_bytes();data.append(content);webm=name.endswith('.webm');prefix=(webm_time_prefix if webm else fragment_prefix)(content,75);rid='track-'+str(i);path=rid+'.part';(root/path).write_bytes(content if hidden else content[:prefix])
      value['resources'].append(dict(id=rid,name=path,length=str(len(content)) if known else None,ranges=[dict(offset='0',length=str(prefix))],state='downloading'))
      value['tracks'].append(dict(id=rid,kind='audio' if i else 'video',required=True,codecHint=codecs[i],containerHint='webm' if webm else 'fmp4',initializationReady=True,state='downloading',source=dict(resourceId=rid),default=i==1))
    for i in range(2):
      text=('1\n00:00:00,000 --> 00:01:30,000\nДальняя перемотка '+str(i)+'\n\n').encode();rid='sub-'+str(i);path=rid+'.srt';(root/path).write_bytes(text)
      value['resources'].append(dict(id=rid,name=path,length=str(len(text)),ranges=[dict(offset='0',length=str(len(text)))],state='complete'))
      value['tracks'].append(dict(id=rid,kind='subtitle',required=False,codecHint='srt',containerHint='srt',initializationReady=True,state='complete',source=dict(resourceId=rid),default=False))
    preview=V2PreviewSession(client,str(root),title='Far seek '+profile);report=dict(profile=profile,declaredLengthsKnown=known,unpublishedTailPhysicallyPresent=hidden,observed=[]);forward=back=paused=resumed=tail=False
    try:
      preview.publish(value);reader=preview.reader_id;deadline=time.monotonic()+55
      while time.monotonic()<deadline:
        state=preview.heartbeat();(root/'last-status.json').write_text(json.dumps(state,ensure_ascii=False,indent=2),encoding='utf-8');assert state.get('state')!='error',state
        assert state['readerId']==reader and state['generation']=='1' and not state['complete'],state
        position=state.get('positionSeconds',0)
        if not forward and 59.9<=position<65:forward=True;report['observed'].append(dict(step='forward-60',status=state))
        if forward and not back and 29.9<=position<35:back=True;report['observed'].append(dict(step='back-30',status=state))
        if back and state['state']=='paused' and 49.9<=position<51 and not paused:paused=True;report['observed'].append(dict(step='paused-50',status=state))
        if forward and back and paused and not resumed and state['state']=='playing' and 51<=position<55:
          resumed=True;report['observed'].append(dict(step='resumed',status=state))
        if resumed and not tail and state['state']=='buffering' and (position>70 or state.get('seekTargetSeconds',0)>70):
          tail=True;report['observed'].append(dict(step='buffering-at-tail',status=state))
        if tail and state['state']=='playing' and 30.5<=position<35:
          assert all(int(r['ranges'][0]['length'])<len(data[i]) and (root/r['name']).stat().st_size==(len(data[i]) if hidden else int(r['ranges'][0]['length'])) for i,r in enumerate(value['resources'][:4]))
          report['observed'].append(dict(step='seek-from-buffering-30',status=state));break
        time.sleep(.05)
      else:raise AssertionError(('Far seek sequence did not complete',state))
      for i,r in enumerate(value['resources'][:4]):
        path=root/r['name'];path.rename(path.with_suffix('.done'));path=path.with_suffix('.done')
        with path.open('ab') as stream:stream.write(data[i][stream.tell():])
        r.update(length=str(len(data[i])),ranges=[dict(offset='0',length=str(len(data[i])))],state='complete');value['tracks'][i]['state']='complete'
      value['revision']='2';value['writer']['state']='complete';preview.publish(value);deadline=time.monotonic()+20
      while time.monotonic()<deadline:
        state=preview.status();assert state.get('state')!='error',state
        if state.get('state')=='finished' and state.get('released'):report['terminal']=state;break
        time.sleep(.05)
      else:raise AssertionError(('No natural finish after complete seek',state))
      assert preview.close(timeout=5)['released'];forget_closed_readers(client,value)
      report.update(result='PASS',readerIdentityPreserved=True);return report
    finally:
      if not preview.closed:preview.close(timeout=10)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile',required=True);p.add_argument('--fixtures',required=True);p.add_argument('--work',required=True);p.add_argument('--media-profile',choices=['mixed','fmp4','webm','mixed-hevc10','mixed-av1-10','mixed-vp8','mixed-known','fmp4-known','webm-known','mixed-hidden-tail'],required=True);a=p.parse_args()
    print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.fixtures,a.work,a.media_profile)))
