"""Qt's real temporary directory and atomic INI I/O under long child TEMP."""
import argparse,json,subprocess,sys,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner',type=Path,required=True)
p.add_argument('--report-directory',type=Path,required=True)
p.add_argument('--observe-system-temp',action='store_true')
a=p.parse_args();a.report_directory.mkdir(parents=True,exist_ok=False);rows=[]
with tempfile.TemporaryDirectory(prefix='foxicmp-settings-temp-') as name:
 root=Path(name)
 for length in (240,320,1100):
  folder=root/str(length)
  while len(str(folder))<length:
   component=('Временные файлы '+'x'*25)[:max(1,length-len(str(folder))-1)]
   folder/=component[:-1]+'x' if component.endswith(' ') else component
  folder.mkdir(parents=True)
  actual=folder
  if a.observe_system_temp:
   import os
   env=os.environ.copy()
   for key in ('TEMP','TMP','TMPDIR'):env[key]=str(folder)
   probe=subprocess.run([str(a.runner.resolve()),'--temp-info'],env=env,capture_output=True,timeout=5)
   assert probe.returncode==0,probe.stderr
   actual=Path(json.loads(probe.stdout)['actualTemp'])
  case=dict(command=[str(a.runner.resolve())],scope='child',temp=str(folder),environment={'FOXICMP_EXPECTED_TEMP':str(actual)},timeout=15)
  config=a.report_directory/(str(length)+'.json');config.write_text(json.dumps(case))
  with (a.report_directory/(str(length)+'.log')).open('wb') as log:
   proc=subprocess.run([sys.executable,'-u',str(Path(__file__).with_name('runtime_child_probe.py')),str(config)],stdout=log,stderr=subprocess.STDOUT,timeout=25)
  (a.report_directory/(str(length)+'.exit')).write_text(str(proc.returncode)+'\n')
  assert proc.returncode==0,(length,proc.returncode)
  rows.append(dict(result='PASS',tempLength=len(str(folder)),requestedTempHonored=actual.resolve()==folder.resolve(),actualTempDirectory=str(actual),bootstrapNativeExit=proc.returncode))
  (a.report_directory/'progress.json').write_text(json.dumps(rows,indent=2)+'\n')
result=dict(result='PASS',rows=rows,runtimeRootRemoved=not root.exists(),parentEnvironmentNotChanged=True)
(a.report_directory/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
