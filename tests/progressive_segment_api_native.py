"""Same player-wire segment corpus through the standalone native manager."""
import argparse,json,os,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_resource_conformance import Fixture
from progressive_segment_api_conformance import exercise
from progressive_client import ApiError
p=argparse.ArgumentParser();p.add_argument('--runner',required=True);p.add_argument('--fixtures',default=os.environ.get('FOXICMP_TEST_MEDIA',str(Path(__file__).resolve().parents[1]/'_bin/qt-test-media')));a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='foxicmp-segment-api-') as folder:
 f=Fixture(a.runner,Path(folder))
 class Client:
  def request(self,packet,timeout=5):
   if packet['action']=='capabilities':return dict(resourceIoProbeVersion=1,resourceIoPlaybackSupported=False,segmentProbeVersion=1,actions=['resourceProbe','resourceProbeStatus','resourceProbeCancel','resourceProbeForget','resourceProbeUpdate'])
   value=dict(packet);action=value.pop('action');reply=f.rpc(action,**value)
   if reply.get('ok') is not True:raise ApiError(reply)
   return reply
  def reader_status(self,identifier):return self.request(dict(version=1,action='readerStatus',readerId=identifier))['reader']
 try:
  report=exercise(Client(),f.base/'probe',a.fixtures);print(json.dumps(report))
 finally:f.close()
