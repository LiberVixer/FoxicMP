import copy,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_preview import RemuxPreview
class Client:
 def __init__(self):self.calls=[];self.id='a'*32;self.request_id=None
 def request(self,message):
  self.calls.append(message)
  if message['action']=='capabilities':return dict(preparedRemuxPreview=dict(version=1,mode='prepared-remux-clip',boundedClip=True,continuousPlayback=False,playbackSupported=True))
  if message['action']=='previewRemux':self.request_id=message['requestId']
  return dict(requestId=self.request_id,probeId='b'*32,mode='prepared-remux-clip',previewId=self.id,boundedClip=True,continuousPlayback=False,state='playing')
class Probe:
 def __init__(self,client):self.client=client;self.remux_mode=True;self.probe_id='b'*32
 def status(self):return dict(state='finished',media=dict(code='data',remux=dict(retained=True)))
class Test(unittest.TestCase):
 def test_capability_is_required_before_playback(self):
  c=Client();c.request=lambda _:{};self.assertRaises(RuntimeError,RemuxPreview,c)
 def test_preview_is_explicit_and_cannot_start_non_remux_probe(self):
  c=Client();p=Probe(c);p.remux_mode=False;r=RemuxPreview(c);n=len(c.calls);self.assertRaises(ValueError,r.start,p);self.assertEqual(len(c.calls),n)
 def test_closed_stage_cannot_start(self):
  c=Client();p=Probe(c);p.status=lambda:dict(state='forgotten');self.assertRaises(RuntimeError,RemuxPreview(c).start,p)
 def test_no_implicit_replace_or_activation(self):
  c=Client();RemuxPreview(c).start(Probe(c));self.assertFalse(c.calls[-1]['replace']);self.assertFalse(c.calls[-1]['activate'])
 def test_foreign_reply_is_not_adopted_or_cancelled(self):
  c=Client();r=RemuxPreview(c);r.start(Probe(c));c.id='c'*32;self.assertRaises(RuntimeError,r.status);self.assertEqual(c.calls[-1]['action'],'previewStatus')
 def test_bad_seek_rejected_before_sending(self):
  c=Client();r=RemuxPreview(c);r.start(Probe(c));n=len(c.calls)
  for value in (-1,3601,True,float('nan'),float('inf')):self.assertRaises(ValueError,r.control,'seek',value)
  self.assertEqual(len(c.calls),n)
 def test_control_requires_known_owner_and_command(self):
  c=Client();r=RemuxPreview(c);self.assertRaises(RuntimeError,r.cancel);self.assertRaises(ValueError,r.control,'stop-everything')
 def test_lost_start_reply_uses_readonly_recovery(self):
  c=Client();r=RemuxPreview(c);old=c.request
  def request(message):
   out=old(message)
   if message['action']=='previewRemux':raise TimeoutError()
   return out
  c.request=request;self.assertRaises(TimeoutError,r.start,Probe(c));r.status();self.assertEqual(c.calls[-1]['action'],'previewStatus');self.assertIn('requestId',c.calls[-1]);self.assertEqual(r.preview_id,'a'*32)
 def test_cancel_is_preview_scoped(self):
  c=Client();r=RemuxPreview(c);r.start(Probe(c));r.cancel();self.assertEqual(c.calls[-1],dict(version=1,action='previewCancel',previewId='a'*32))
if __name__=='__main__':unittest.main()
