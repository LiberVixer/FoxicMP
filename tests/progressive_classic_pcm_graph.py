"""Actual DirectShow source/decoder/PCM-output graph, separate portable profile."""
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path
from progressive_mp3_gapless_fixture import fixture,PROFILES,frames,prefix_at_seconds
from progressive_standalone_fixture import publish

def run(runner,decoder_runner):
 rows=[];checks=[]
 for profile in PROFILES:
  with tempfile.TemporaryDirectory(prefix='foxicmp-classic-graph-') as temp:
   root=Path(temp);model,raw,original,probe=fixture(root,profile,4);rate=int(profile.split('-')[-1]);channels=2 if '-stereo-' in profile else 1
   initial=publish(model,raw,root,prefix_at_seconds(raw,probe,2));full=publish(model,raw,root,len(raw),previous=initial);complete=publish(model,raw,root,len(raw),previous=full,complete=True)
   for name,v in [('initial',initial),('full',full),('complete',complete)]:
    v['sessionDirectory']=str(root);(root/(name+'.json')).write_text(json.dumps(v),encoding='utf-8')
   packets=root/'packets.bin';import struct
   items=frames(raw)[1:];packets.write_bytes(b''.join(struct.pack('<I',size)+raw[at:at+size] for at,size in items))
   plain=root/'decoder-plain.f32';begin,end=probe['_gapless']['begin'],probe['_gapless']['end'];total=len(items)*1152
   q=subprocess.run([decoder_runner,str(packets),str(plain),str(rate),str(channels),str(begin),str(end),'0','plain',str(total*channels*4)],capture_output=True,text=True,timeout=20);assert q.returncode==0,(q.stdout,q.stderr)
   coded=plain.read_bytes();outputs=[]
   for target in (0,1,rate*2+1,rate*4-1):
    output=root/('graph-'+str(target)+'.f32');registry=root/('registry-'+str(target))
    incomplete=target==rate*2+1
    args=[runner,str(root/'initial.json'),str(root/'full.json'),str(root/'complete.json'),str(output),str(rate),str(channels),str(target),'1' if target==0 or incomplete else '0',str(registry)]
    if incomplete:args.append('--seek-incomplete')
    p=subprocess.run(args,capture_output=True,text=True,timeout=60)
    assert p.returncode==0,(profile,target,p.returncode,p.stdout,p.stderr)
    status=json.loads(p.stdout.strip().splitlines()[-1]);assert status['result']=='PASS' and status['samples']==rate*4-target and status['windowApplied'] and status['stopCleared'] and status['readerPreserved'],status
    assert status['seekWhileIncomplete']==incomplete,status
    actual=output.read_bytes();expected=coded[(begin+target)*channels*4:end*channels*4];assert actual==expected,(profile,target,len(actual),len(expected))
    outputs.append(dict(targetSample=target,sha256=hashlib.sha256(actual).hexdigest(),pcmBytes=len(actual),graph=status));checks.append(profile+'-graph-pcm-'+str(target))
   negative=[runner,str(root/'initial.json'),str(root/'full.json'),str(root/'complete.json'),str(root/'rejected.f32'),str(rate),str(channels),'0','0',str(root/'negative-registry'),'--unsupported-decoder']
   p=subprocess.run(negative,capture_output=True,text=True,timeout=30);assert p.returncode==0,(profile,p.stdout,p.stderr)
   rejected=json.loads(p.stdout.strip().splitlines()[-1]);assert rejected['unsupportedDecoderRejected'] and rejected['samples']==0 and rejected['released'],rejected
   checks.append(profile+'-unsupported-decoder-rejected-without-output')
   assert original.read_bytes()==raw;checks.append(profile+'-original-unchanged');rows.append(dict(profile=profile,windows=outputs,unsupportedDecoder=rejected))
 return dict(result='PASS',count=len(checks),checks=checks,profiles=rows,publicClassicGaplessCapability=False,scope='production private MP4 source/reader + actual internal decoder + DirectShow PCM capture, no hardware/user processing')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runner',required=True);p.add_argument('--decoder-runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner,a.decoder_runner)))
