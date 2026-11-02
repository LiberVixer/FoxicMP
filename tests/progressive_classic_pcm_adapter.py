"""Actual Classic CFFAudioDecoder: exact trim, explicit preroll and lifecycle."""
import argparse,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
from progressive_mp3_gapless_fixture import fixture,PROFILES,frames

def run(runner):
 checks=[];rows=[]
 for profile in PROFILES:
  with tempfile.TemporaryDirectory(prefix='foxicmp-classic-pcm-') as temp:
   root=Path(temp);model,raw,original,probe=fixture(root,profile,4);rate=int(profile.split('-')[-1]);channels=2 if '-stereo-' in profile else 1
   packets=root/'packets.bin';items=frames(raw)[1:];packets.write_bytes(b''.join(struct.pack('<I',size)+raw[at:at+size] for at,size in items))
   begin,end=probe['_gapless']['begin'],probe['_gapless']['end'];total=len(items)*1152
   def capture(mode,target=0,first=0):
    output=root/(mode+'-'+str(target)+'.f32');expected=(end-begin-target if mode=='trim' else total-first)*channels*4
    p=subprocess.run([runner,str(packets),str(output),str(rate),str(channels),str(begin+target),str(end),str(first),mode,str(expected)],capture_output=True,text=True,timeout=20)
    assert p.returncode==0,(profile,mode,target,p.returncode,p.stdout,p.stderr)
    result=json.loads(p.stdout.strip().splitlines()[-1]);assert result['result']=='PASS';return output.read_bytes(),result
   plain,decoder=capture('plain');checks.append(profile+'-actual-untrimmed-decoder')
   native=subprocess.check_output(['ffmpeg','-v','error','-i',str(original),'-c:a','pcm_f32le','-f','f32le','-'])
   audible=plain[begin*channels*4:end*channels*4];assert len(audible)==len(native)
   a=struct.unpack('<'+'f'*(len(audible)//4),audible);b=struct.unpack('<'+'f'*(len(native)//4),native)
   maximum=max(abs(x-y) for x,y in zip(a,b));assert maximum<1e-5,(profile,maximum)
   checks.append(profile+'-decoder-delay529-independent-native-float-reference')
   outputs=[]
   for target in (0,1,rate*2+1,rate*4-1):
    first=(begin+target-rate//4)//1152*1152 if begin+target>rate//4 else 0
    actual,result=capture('trim',target,first);expected=plain[(begin+target)*channels*4:end*channels*4]
    assert actual==expected,(profile,target,'trim/preroll mismatch')
    outputs.append(dict(targetSample=target,decodeFirst=first,bytes=len(actual),sha256=hashlib.sha256(actual).hexdigest(),decoder=result))
    checks.append(profile+'-exact-pcm-window-'+str(target))
   for mode in ('flush','replace'):
    actual,_=capture(mode);assert actual==plain,(profile,mode,'trim leaked')
    checks.append(profile+'-'+mode+'-clears-window')
   truncated=subprocess.run([runner,str(packets),str(root/'truncated.f32'),str(rate),str(channels),str(begin),str(total+1152),'0','trim','0'],capture_output=True,text=True,timeout=20)
   assert truncated.returncode==1 and 'PCM window truncated before end' in truncated.stderr,(profile,truncated.stdout,truncated.stderr)
   checks.append(profile+'-truncated-pcm-cannot-report-successful-eof')
   assert original.read_bytes()==raw;checks.append(profile+'-original-unchanged')
   rows.append(dict(profile=profile,decoder=decoder,windows=outputs,nativeReferenceMaxAbsoluteFloatError=maximum))
 return dict(result='PASS',count=len(checks),checks=checks,profiles=rows,scope='actual internal Classic decoder before PCM output conversion; no DirectShow graph/controller',publicCapability=False)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
