"""Independent RF64 upper-bound writer; only head/tail bytes are materialized."""
import json,struct,uuid
from pathlib import Path
from progressive_rf64_sparse_large_playback import create_sparse,unallocated
from progressive_integrity import protect_snapshot
PACKET=16384
PACKETS=250000
FILE_LIMIT=1<<34

def geometry(bits,channels,rate,frames=None):
 # An empty permitted JUNK chunk makes the PCM24 stereo byte limit exact.
 offset=112 if bits==24 and channels==2 else 80 if bits==16 else 104
 align=bits//8*channels
 if frames is None:frames=min(PACKET*PACKETS,(FILE_LIMIT-offset)//align)
 size=offset+frames*align+(frames*align&1)
 return dict(bits=bits,channels=channels,rate=rate,align=align,offset=offset,frames=frames,fileBytes=size,packets=(frames+PACKET-1)//PACKET,duration=frames*10000000//rate)

def header(g):
 b=bytearray(b'RF64'+b'\xff'*4+b'WAVEds64'+struct.pack('<IQQQI',28,g['fileBytes']-8,g['frames']*g['align'],g['frames'],0))
 fmt=struct.pack('<HHIIHH',1 if g['bits']==16 else 65534,g['channels'],g['rate'],g['rate']*g['align'],g['align'],g['bits'])
 if g['bits']==24:fmt+=struct.pack('<HHI',22,24,4 if g['channels']==1 else 3)+bytes.fromhex('0100000000001000800000aa00389b71')
 b+=b'fmt '+struct.pack('<I',len(fmt))+fmt
 if g['bits']==24 and g['channels']==2:b+=b'JUNK'+struct.pack('<I',0)
 b+=b'data'+b'\xff'*4
 assert len(b)==g['offset'];return bytes(b)

def pcm(g,at,n):
 period=b''.join((((i*(137+c*74)*257+c*93)%(1<<g['bits']))-(1<<(g['bits']-1)) & ((1<<g['bits'])-1)).to_bytes(g['bits']//8,'little') for i in range(4093) for c in range(g['channels']))
 start=at%len(period);return (period*((start+n+len(period)-1)//len(period)))[start:start+n]

def fixture(root,g,head_seconds=4,tail_seconds=16):
 root=Path(root);root.mkdir(parents=True,exist_ok=True);path=root/'limit.part';create_sparse(path,g['fileBytes']);h=header(g);head=g['offset']+int(head_seconds*g['rate'])*g['align'];last=(g['frames']-int(tail_seconds*g['rate']))//PACKET*PACKET;tail=g['offset']+last*g['align']
 def write(a,z):
  with path.open('r+b') as f:
   f.seek(a)
   while a<z:
    if a<len(h):data=h[a:min(len(h),z)]
    elif a<g['offset']+g['frames']*g['align']:data=pcm(g,a-g['offset'],min(1024**2,z-a,g['offset']+g['frames']*g['align']-a))
    else:data=b'\0'*(z-a)
    f.write(data);a+=len(data)
   f.flush()
 write(0,head);write(tail,g['fileBytes']);assert unallocated(path,32*1024**2,65536) and unallocated(path,(1<<32)-8*1024**2,65536)
 model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(g['duration']),timestampOffset='0'),resources=[dict(id='media',name=path.name,length=str(g['fileBytes']),state='downloading',ranges=[dict(offset='0',length=str(head)),dict(offset=str(tail),length=str(g['fileBytes']-tail))])],tracks=[dict(id='main',kind='audio',required=True,codecHint='pcm-s'+str(g['bits'])+'le',containerHint='rf64-sparse-pcm'+str(g['bits']),state='downloading',initializationReady=True,source=dict(resourceId='media'))])
 # Two whole packets before the end, with enough buffered tail on either side.
 far=(g['frames']-10*g['rate'])//g['rate'];model['metadata']=dict(revision='1',title='RF64 upper bound',chapters=[dict(id='c'+str(i),time=str(t*10000000),title=str(i)) for i,t in enumerate((0,4,far))])
 return protect_snapshot(model,root),path,write,head,tail,far
