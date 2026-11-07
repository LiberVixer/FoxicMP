"""Native range I/O and continuous cache with long explicit private roots.

No desktop or player loader is involved; these are component checks.
"""
import argparse,copy,json,subprocess,tempfile,time,uuid
from pathlib import Path
from progressive_resource_conformance import snapshot as io_snapshot
from progressive_backend_demux_conformance import snapshot
from progressive_stream_remux_worker_test import fixture
from progressive_integrity import protect_snapshot

p=argparse.ArgumentParser(description=__doc__)
for name in ('resource-runner','controller-runner','worker','report-directory'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.report_directory.mkdir(parents=True,exist_ok=False);rows=[]
class Rpc:
 def __init__(self,args):self.p=subprocess.Popen(list(map(str,args)),stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
 def __call__(self,action,**fields):
  self.p.stdin.write(json.dumps(dict(action=action,**fields))+'\n');self.p.stdin.flush();s=self.p.stdout.readline();assert s,(self.p.poll(),self.p.stderr.read());d=json.loads(s);assert d['ok'],d;return d
 def close(self):
  self.p.stdin.close();self.p.wait(timeout=10);err=self.p.stderr.read();assert self.p.returncode==0,(self.p.returncode,err);self.p.stdout.close();self.p.stderr.close();return self.p.returncode
with tempfile.TemporaryDirectory(prefix='foxicmp-private-paths-') as name:
 root=Path(name);original=fixture(root,'source');raw=original.read_bytes()
 for length in (120,320,1100):
  parent=root/str(length)
  while len(str(parent))<length:parent/= 'private-folder-'+ 'x'*35
  parent.mkdir(parents=True)
  registry=parent/'registry';cache=parent/'cache'
  (a.report_directory/'current.json').write_text(json.dumps(dict(registry=str(registry),cache=str(cache),registryLength=len(str(registry)),cacheLength=len(str(cache)))))
  source=root/('data-'+str(length));source.mkdir();(source/'media.part').write_bytes(b'0123456789abcdef')
  rpc=Rpc([a.resource_runner,'--registry',registry]);error=None
  try:
   rpc('open',directory=str(source),session=io_snapshot(ranges=((0,16),)))
   answer=rpc('read',resourceId='media',offset='0',length='16');assert answer['hex']==b'0123456789abcdef'.hex(),answer
   identifier=rpc('bindings')['readerIds'][0];assert not rpc('readerStatus',version=1,readerId=identifier)['reader']['released']
   rpc('release');proof=rpc('readerStatus',version=1,readerId=identifier)['reader'];assert proof['filesClosed'] and proof['released'],proof
   assert rpc('readerForget',version=1,readerId=identifier)['forgotten']
   assert rpc('readers',version=1,sessionId='a'*32,generation='1')['readers']==[]
  finally:exit_code=rpc.close()
  rpc=Rpc([a.controller_runner,registry,cache,a.worker]);path=source/'input.part';model=snapshot(path,raw,complete=False,prefix=len(raw)//2);model['timeline']['duration']='240000000'
  job=None
  try:
   job=rpc('continuousRemuxStart',version=1,requestId=uuid.uuid4().hex,sessionDirectory=str(source),resourceId='clip',session=model)
   def wait(predicate):
    until=time.monotonic()+20
    while time.monotonic()<until:
     s=rpc('continuousRemuxStatus',version=1,jobId=job['jobId']);assert not s['errorCode'],s
     if predicate(s):return s
     time.sleep(.03)
    raise AssertionError(s)
   ready=wait(lambda s:s['playbackReady']);assert ready['workerId']>0
   opened=rpc('reader',name='output',jobId=job['jobId']);read=rpc('read',name='output',offset='0');assert read['code']=='data' and read['bytes']>0
   with path.open('ab') as f:f.write(raw[f.tell():])
   previous=model;model=copy.deepcopy(model);model['revision']='2';model['writer']['state']='complete';model['tracks'][0]['state']='complete';model['resources'][0].update(state='complete',ranges=[dict(offset='0',length=str(len(raw)))])
   model=protect_snapshot(model,source,previous=previous);rpc('continuousRemuxUpdate',version=1,jobId=job['jobId'],session=model)
   completed=wait(lambda s:s['outputComplete']);assert completed['workerId']==ready['workerId']
   rpc('close',name='output');rpc('continuousRemuxCancel',version=1,jobId=job['jobId'],sessionId=job['sessionId'],generation=job['generation'],writerInstanceId=job['writerInstanceId'])
   final=wait(lambda s:s['released'] and s['sourceFilesClosed']);assert not list(cache.glob('remux-*'))
   forgotten=rpc('continuousRemuxForget',version=1,jobId=job['jobId'],sessionId=job['sessionId'],generation=job['generation'],writerInstanceId=job['writerInstanceId']);forgotten=wait(lambda s:s['forgotten'])
   assert path.read_bytes()==raw
  finally:controller_exit=rpc.close()
  rows.append(dict(result='PASS',registryLength=len(str(registry)),cacheLength=len(str(cache)),resourceExit=exit_code,controllerExit=controller_exit,proof=proof,ready=ready,read=read,completed=completed,release=final))
  (a.report_directory/'progress.json').write_text(json.dumps(rows,indent=2)+'\n')
(a.report_directory/'result.json').write_text(json.dumps(dict(result='PASS',count=len(rows),rows=rows),indent=2)+'\n');print(json.dumps(dict(result='PASS',count=len(rows))))
