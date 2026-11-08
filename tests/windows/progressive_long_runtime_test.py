"""Full Qt main with long profile/TEMP roots, isolated from the desktop profile.

TEMP is passed explicitly to owned children. Parent Python's environment is
never changed. The separate parent-environment launch stall remains unqualified.
"""
import argparse
import json
from pathlib import Path
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from progressive_legacy_cli_paths_test import exercise
from owned_process_job import OwnedProcessJob


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('player','fixtures','report-directory'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--require-exact-temp',action='store_true')
    a=p.parse_args()
    a.report_directory.mkdir(parents=True,exist_ok=False)
    job=OwnedProcessJob()
    rows=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-long-runtime-') as name:
        root=Path(name)
        for length in (0,320,1100):
            result=exercise(a.player.resolve(),a.fixtures.resolve(),a.report_directory/('profile-'+str(length)),0,'stop',length)
            rows.append(dict(route='profile',targetLength=length,**result))
            (a.report_directory/'progress.json').write_text(json.dumps(rows,indent=2)+'\n')
        for length in (240,320,1100):
            folder=root/str(length)
            while len(str(folder))<length:
                component=('Временная папка '+'x'*25)[:max(1,length-len(str(folder))-1)]
                folder/=component[:-1]+'x' if component.endswith(' ') else component
            folder.mkdir(parents=True)
            result=exercise(a.player.resolve(),a.fixtures.resolve(),a.report_directory/('temp-'+str(length)),0,'stop',0,folder,a.player.resolve().with_name('foxicmp-settings-path-tests.exe'))
            if length==240 or a.require_exact_temp:
                assert result['requestedTempHonored'], result
            rows.append(dict(route='temp' if result['requestedTempHonored'] else 'temp-os-fallback',targetLength=length,**result))
            (a.report_directory/'progress.json').write_text(json.dumps(rows,indent=2)+'\n')
    report=dict(result='PASS',count=len(rows),rows=rows,runtimeRootRemoved=not root.exists(),parentEnvironmentNotChanged=True,exactTempRequired=a.require_exact_temp)
    (a.report_directory/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(result='PASS',count=len(rows))))
    job.finish()

if __name__=='__main__':
    main()
