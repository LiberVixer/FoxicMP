"""Bounded subprocess probe; the parent controller owns its deadline."""
import faulthandler,json,os,subprocess,sys
from pathlib import Path
from owned_process_job import OwnedProcessJob

if __name__=='__main__':
 case=json.loads(Path(sys.argv[1]).read_text());job=OwnedProcessJob();faulthandler.dump_traceback_later(3,repeat=False)
 env=os.environ.copy();env.update(case.get('environment',{}));env['QT_QPA_PLATFORM']='offscreen'
 if case.get('temp'):
  for key in ['TEMP','TMP','TMPDIR']:
   env[key]=case['temp']
   if case['scope']=='parent':os.environ[key]=case['temp']
 print(json.dumps(dict(phase='before-popen',case=case)),flush=True)
 child=subprocess.Popen(case['command'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 print(json.dumps(dict(phase='after-popen',pid=child.pid)),flush=True)
 out,err=child.communicate(timeout=case.get("timeout",10))
 print(json.dumps(dict(phase='child-exit',exit=child.returncode,stdout=out.decode('utf-8',errors='replace'),stderr=err.decode('utf-8',errors='replace'))),flush=True)
 faulthandler.cancel_dump_traceback_later();job.finish();sys.exit(child.returncode)
