from pathlib import Path
import ctypes,ctypes.util,json,sys,tempfile,time,os,argparse
r=Path(__file__).resolve().parents[1];sys.path[:0]=[str(r/'tests'),str(r/'qt/tools')]
from progressive_rf64_fixture import fixture
from progressive_standalone_fixture import publish
from progressive_mux_ts_conformance import Rpc
args=argparse.ArgumentParser();args.add_argument('--runner',required=True);probeArgs=args.parse_args()
assert os.environ.get('PULSE_SERVER','').startswith('unix:'),'Use qt/tests/run_tests.sh private audio'
lib=ctypes.CDLL(ctypes.util.find_library('mpv'));lib.mpv_create.restype=ctypes.c_void_p
for n,args,result in [('mpv_set_option_string',[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p],ctypes.c_int),('mpv_initialize',[ctypes.c_void_p],ctypes.c_int),('mpv_command',[ctypes.c_void_p,ctypes.POINTER(ctypes.c_char_p)],ctypes.c_int),('mpv_terminate_destroy',[ctypes.c_void_p],None),('mpv_get_property_string',[ctypes.c_void_p,ctypes.c_char_p],ctypes.c_void_p),('mpv_free',[ctypes.c_void_p],None)]:
 f=getattr(lib,n);f.argtypes=args;f.restype=result
class Event(ctypes.Structure):_fields_=[('event_id',ctypes.c_int),('error',ctypes.c_int),('userdata',ctypes.c_ulonglong),('data',ctypes.c_void_p)]
lib.mpv_wait_event.argtypes=[ctypes.c_void_p,ctypes.c_double];lib.mpv_wait_event.restype=ctypes.POINTER(Event)
def probe(path,start,duplicate,hold):
 h=lib.mpv_create()
 for k,v in dict(config='no',vo='null',ao='pulse',pause='yes',idle='yes',start=str(start),**{'rebase-start-time':'no','keep-open':hold,'terminal':'no','audio-buffer':os.environ.get('FOXICMP_PROBE_BUFFER','0.2'),'hr-seek':'yes','hr-seek-demuxer-offset':os.environ.get('FOXICMP_PROBE_PREROLL','0')}).items():assert lib.mpv_set_option_string(h,k.encode(),v.encode())>=0
 assert lib.mpv_initialize(h)>=0
 def command(*items):return lib.mpv_command(h,(ctypes.c_char_p*(len(items)+1))(*[v.encode() for v in items],None))
 def get(key):
  p=lib.mpv_get_property_string(h,key.encode())
  if not p:return None
  v=ctypes.string_at(p).decode();lib.mpv_free(p);return v
 command('loadfile',str(path));events=[];sent=False;end=time.monotonic()+1
 while time.monotonic()<end:
  e=lib.mpv_wait_event(h,.02).contents
  if e.event_id:events.append(e.event_id)
  if e.event_id==8 and duplicate and not sent:command('seek',str(start),'absolute+exact');sent=True
 result=dict(path=path.name,start=start,duplicate=duplicate,keepOpen=hold,events=events,pos=get('time-pos'),pause=get('pause'),eof=get('eof-reached'));lib.mpv_terminate_destroy(h);return result
with tempfile.TemporaryDirectory(prefix='foxicmp-tail-probe-') as d:
 root=Path(d);m,raw,original,_=fixture(root/'source','audio-rf64-16-mono-44100',36,writer='manual');v=publish(m,raw,root,len(raw),complete=True);rpc=Rpc([probeArgs.runner,'--registry',str(root/'registry')]);data=bytearray()
 try:
  assert rpc('open',session=v,directory=str(root))['ok'];size=int(rpc('layout')['tracks'][0]['available'])
  for at in range(0,size,65536):data+=bytes.fromhex(rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,size-at)))['hex'])
 finally:rpc.close()
 at=0;moofs=[]
 while at<len(data):
  n=int.from_bytes(data[at:at+4],'big');assert n>=8
  if data[at+4:at+8]==b'moof':moofs.append(at)
  at+=n
 full=root/'full.mp4';full.write_bytes(data);last=root/'last.mp4';last.write_bytes(data[:moofs[0]]+data[moofs[-1]:]);preroll=root/'preroll.mp4';preroll.write_bytes(data[:moofs[0]]+data[moofs[-4]:]);out=[]
 for path in (original,last):
  for hold in ('no','yes'):
   value=probe(path,35.95,False,hold);out.append(value)
   if hold=='yes':assert 7 not in value['events'] and value['pause']=='yes' and value['pos'] is not None and abs(float(value['pos'])-35.95)<.2,value
 print(json.dumps(dict(result='PASS',scope='Linux libmpv with private PulseAudio; paused near-EOF retain behavior, separate from controller acceptance',cases=out,baselineEarlyEofObserved=any(7 in v['events'] for v in out if v['keepOpen']=='no'))))
