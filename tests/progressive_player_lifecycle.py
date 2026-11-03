"""Actual player/decoder start -> toolbar Stop -> acknowledged file close cycles."""
import argparse,copy,hashlib,json,os,shutil,statistics,sys,time,uuid
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_rf64_limits_fixture import geometry,header,pcm
from progressive_client import ProgressiveClient
from progressive_v2 import V2PreviewSession
from progressive_integrity import protect_snapshot
from progressive_process_metrics import sample
from progressive_v2_playback_conformance import forget_closed_readers

def exercise(client,work,control,decoded=None,cycles=100):
 assert 1<=cycles<=200
 root=Path(work).resolve();root.mkdir(parents=True,exist_ok=True);pid=client.request(dict(version=1,action='capabilities'))['pid'];began=time.monotonic();rows=[];warm=[];reader_ids=set();max_request=0.;max_post_stop=0.;original_request=client.request
 def request(message,timeout=5):
  nonlocal max_request
  t=time.monotonic();answer=original_request(message,timeout=timeout);max_request=max(max_request,time.monotonic()-t);return answer
 client.request=request
 tracer=None
 if os.name=='nt' and os.environ.get('FOXICMP_TEST_HANDLE_TRACE')=='1':
  from progressive_windows_handle_trace import Trace;tracer=Trace(pid)
 raw={bits:header(geometry(bits,2,48000,20*48000))+pcm(geometry(bits,2,48000,20*48000),0,20*48000*(bits//8)*2) for bits in (16,24)}
 def wait(preview,reader,predicate,label,timeout=30):
  deadline=time.monotonic()+timeout;s={}
  while time.monotonic()<deadline:
   s=preview.heartbeat();assert s.get('ok') and not s.get('error') and s.get('readerId')==reader,s
   if predicate(s):return s
   time.sleep(.04)
  raise AssertionError((label,s))
 try:
  for index in range(cycles+4):
   bits=16 if index%2==0 else 24;sparse=index%4>=2;g=geometry(bits,2,48000,20*48000);data=raw[bits];directory=root/f'cycle-{index:03}';directory.mkdir();path=directory/'media.part';head=g['offset']+4*g['rate']*g['align'];ready=g['offset']+12*g['rate']*g['align'];path.write_bytes(data[:head]);session=uuid.uuid4().hex
   value=dict(schemaVersion=2,sessionId=session,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='200000000',timestampOffset='0'),resources=[dict(id='media',name=path.name,length=str(len(data)),ranges=[dict(offset='0',length=str(head))],state='downloading')],tracks=[dict(id='main',kind='audio',required=True,codecHint='pcm-s'+str(bits)+'le',containerHint='rf64-'+('sparse-' if sparse else '')+'pcm'+str(bits),state='downloading',initializationReady=True,source=dict(resourceId='media'))],metadata=dict(revision='1',title='Decoder lifecycle '+str(index),chapters=[dict(id='c0',time='0',title='Start'),dict(id='c1',time='20000000',title='Seek')]))
   protected=protect_snapshot(value,directory);preview=V2PreviewSession(client,str(directory),title='Lifecycle '+str(index));reader=None
   try:
    preview.publish(protected);reader=preview.reader_id;assert reader and reader not in reader_ids;reader_ids.add(reader);wait(preview,reader,lambda s:s['state']=='buffering','head-buffers')
    with path.open('ab') as f:f.write(data[head:ready]);f.flush()
    value['revision']='2';value['resources'][0]['ranges'][0]['length']=str(ready);protected=protect_snapshot(value,directory,previous=protected);t=time.monotonic();preview.publish(protected)
    s=wait(preview,reader,lambda s:s['state']=='playing' and s['positionSeconds']>=.25 and int(s['decoderIoBytes']['audio'])>0,'actual-playing');startup=time.monotonic()-t;assert not s['complete'] and path.stat().st_size<len(data)
    if decoded is not None:decoded(session,bits)
    live=sample(pid);controls=index%10==0
    if controls:
     control('pause',None);wait(preview,reader,lambda s:s['state']=='paused','pause');control('seek',2);wait(preview,reader,lambda s:s['state']=='paused' and 1.8<=s['positionSeconds']<3,'paused-seek');control('pause',None);wait(preview,reader,lambda s:s['state']=='playing' and s['positionSeconds']>=2.25,'resume')
    # Do not send readerRelease to cause the close: first prove UI Stop alone
    # closed the reader, using read-only status polling and a strict deadline.
    t=time.monotonic();control('stop',None);deadline=t+2
    while True:
     receipt=client.reader_status(reader)
     if receipt.get('released'):break
     assert time.monotonic()<deadline,receipt;time.sleep(.025)
    stop=time.monotonic()-t;assert stop<2 and receipt.get('filesClosed'),(stop,receipt);max_post_stop=max(max_post_stop,stop)
    assert preview.close(timeout=2)['released'];forget_closed_readers(client,protected);control('settle',None)
    assert path.read_bytes()==data[:ready];digest=hashlib.sha256(path.read_bytes()).hexdigest();path.unlink();directory.rmdir()
    after=sample(pid)
    if os.name=='nt' and os.environ.get('FOXICMP_TEST_HANDLE_TYPES')=='1':
     from progressive_process_metrics import windows_handle_types
     after['handleTypes']=windows_handle_types(pid)
    m=dict(cycle=index-3,bits=bits,sparse=sparse,actualPlayback=True,startupSeconds=startup,uiStopReleaseSeconds=stop,controlsTested=controls,playedPositionSeconds=s['positionSeconds'],decoderIoBytes=int(s['decoderIoBytes']['audio']),receiptFilesClosed=True,readerId=reader,sourceSha256=digest,live=live,afterClose=after)
    (rows if index>=4 else warm).append(m)
    (root/'progress.json').write_text(json.dumps(dict(completedMeasuredCycles=len(rows),last=m)),encoding='utf-8')
    if index>=4 and (index-3)%10==0:print('Player lifecycle cycles',index-3,'RSS',after['rssBytes'],'handles',after['handles'],'threads',after['threads'],file=sys.stderr,flush=True)
   finally:
    if preview.started and not preview.closed:preview.close(timeout=30)
  trace=tracer.finish() if tracer is not None else None
  (root/'measurements.json').write_text(json.dumps(dict(rows=rows,warmup=warm)),encoding='utf-8')
  print('Player lifecycle measurements: '+json.dumps(rows),file=sys.stderr,flush=True)
  early=[v['afterClose'] for v in rows[:10]];steady=[v['afterClose'] for v in rows[30:40] or rows[:10]];late=[v['afterClose'] for v in rows[-10:]]
  if cycles>=100:
   assert max(m['handles'] for m in late)<=max(m['handles'] for m in steady)+8
   assert max(m['threads'] for m in late)<=max(m['threads'] for m in steady)+2
   assert max(m['threads'] for m in late)<=max(m['threads'] for m in early)+8
   for key in ('rssBytes','privateBytes'):assert max(m[key] for m in late)<=max(m[key] for m in early)+256*1024**2
  assert not any(root.glob('cycle-*'))
  return dict(result='PASS',handleTrace=trace,cycles=cycles,warmupCycles=4,decoderSessions=cycles+4,actualPlaybackEveryCycle=True,toolbarStopEveryCycle=True,apiReleaseOnlyAfterFilesClosed=True,pid=pid,rows=rows,warmup=warm,readerIdentitiesUnique=len(reader_ids)==cycles+4,maxRequestSeconds=max_request,maxUiStopReleaseSeconds=max_post_stop,medianStartupSeconds=statistics.median(v['startupSeconds'] for v in rows),elapsedSeconds=time.monotonic()-began,sourceDirectoriesRemoved=True,fullPlayerCycles=True,longDurationPlayback=False,budget=dict(steadyHandleGrowth=8,steadyThreadGrowth=2,initialThreadGrowth=8,rssPrivateGrowthBytes=256*1024**2))
 finally:client.request=original_request

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile',required=True);p.add_argument('--work',required=True);p.add_argument('--cycles',type=int,default=100);a=p.parse_args();sequence=[0];root=Path(a.work)
 def control(action,value):
  sequence[0]+=1;seq=sequence[0];tmp=root/'control.tmp';tmp.write_text(json.dumps(dict(sequence=seq,action=action,value=value)),encoding='utf-8');deadline=time.monotonic()+5
  while True:
   try:tmp.replace(root/'control.json');break
   except PermissionError:
    assert time.monotonic()<deadline;time.sleep(.01)
  while time.monotonic()<deadline:
   try:reply=json.loads((root/('control-ack-'+str(seq)+'.json')).read_text());
   except (FileNotFoundError,PermissionError,json.JSONDecodeError):reply={}
   if reply.get('sequence')==seq:return reply
   time.sleep(.02)
  raise AssertionError(('UI control acknowledgement timed out',action))
 def decoded(session,bits):
  deadline=time.monotonic()+5
  while time.monotonic()<deadline:
   try:v=json.loads((root/('decoder-'+session+'.json')).read_text())
   except (FileNotFoundError,PermissionError,json.JSONDecodeError):v={}
   if v.get('sessionId')==session:
    assert v['codec']=='pcm_s'+str(bits)+'le' and v['rate']==48000 and v['channels']==2 and v['seconds']>=.2,v;return
   time.sleep(.02)
  raise AssertionError(('Real decoder observation missing',session,bits))
 print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.work,control,decoded,a.cycles)))
