"""Classic legacy CLI/provider with long manifests; private player/INI only."""
import argparse,json,shutil,tempfile
from pathlib import Path
from types import SimpleNamespace
from progressive_family_test import run_legacy
p=argparse.ArgumentParser(description=__doc__)
for name in ('player','fixtures','report-directory'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.report_directory.mkdir(parents=True,exist_ok=False);rows=[]
with tempfile.TemporaryDirectory(prefix='foxicmp-legacy-long-') as name:
 root=Path(name);stage=root/'player';shutil.copytree(a.player.resolve().parent,stage);player=stage/a.player.name
 player.with_suffix('.ini').write_text('[Settings]\nContextMenuPrompted=1\nContextMenuEnabled=0\nPauseMinimizedVideo=0\nCheckForUpdates=0\nKeepHistory=0\n[Audio]\nAudioRenderer=Null Audio Renderer (Uncompressed)\n',encoding='utf-16')
 for length in (320,1100):
  folder=root/str(length)
  while len(str(folder))<length:folder/='Видео с пробелами '+'x'*25
  result=run_legacy(SimpleNamespace(player=str(player),fixtures=str(a.fixtures.resolve()),report=str(folder)))
  for kind,source in (('video','video.mp4'),('audio','audio.m4a')):assert (folder/'legacy'/(kind+'.part')).read_bytes()==(a.fixtures/'progressive'/source).read_bytes()
  rows.append(dict(manifestLength=len(str(folder/'legacy/session.json')),**result))
  (a.report_directory/'progress.json').write_text(json.dumps(rows,indent=2)+'\n')
report=dict(result='PASS',count=len(rows),rows=rows,testCopiesRemoved=not root.exists())
(a.report_directory/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(result='PASS',count=len(rows))))
