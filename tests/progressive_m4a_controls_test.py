"""Qt Window pause/seek/reload, using the same prepared native M4A corpus."""
import argparse,json,shutil,subprocess,tempfile
from pathlib import Path
from progressive_m4a_fixture import publish
from progressive_tail_mp4_fixture import publish as publish_tail

def run(args):
    report=Path(args.report_directory);report.mkdir(parents=True,exist_ok=True);rows=[]
    for name in ['48000-1-2-head','44100-2-2-tail','48000-1-5-tail','44100-2-5-head','48000-2-29-tail','ffmpeg-44100-1-head']:
        case=Path(args.corpus)/name;model=json.loads((case/'model.json').read_text());info=json.loads((case/'info.json').read_text());raw=(case/'original.m4a').read_bytes();publisher=publish_tail if info['tailOffset'] else publish
        with tempfile.TemporaryDirectory(prefix='foxicmp-m4a-controls-') as t:
            root=Path(t);data=root/'data';data.mkdir();value=publisher(model,raw,data,info['ends'][12*info['rate']//info['frame']])
            for label in ('initial','full','complete'):
                if label!='initial':value=publisher(model,raw,data,len(raw),previous=value,complete=label=='complete')
                (root/(label+'.json')).write_text(json.dumps(dict(value,sessionDirectory=str(data))))
            p=subprocess.run([args.runner,*[str(root/(s+'.json')) for s in ('initial','full','complete')],str(root/'profile')],capture_output=True,timeout=45)
            (report/(name+'.exit')).write_text(str(p.returncode));(report/(name+'.stdout')).write_bytes(p.stdout);(report/(name+'.stderr')).write_bytes(p.stderr)
            if p.returncode:shutil.copytree(root,report/(name+'-failure'),dirs_exist_ok=True)
            assert p.returncode==0,(name,p.returncode,p.stderr);assert (data/'audio.part').read_bytes()==raw
            rows.append(dict(profile=name,exitCode=p.returncode,consumer=json.loads(p.stdout.strip().splitlines()[-1])))
    result=dict(result='PASS',profiles=len(rows),rows=rows);(report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);p.add_argument('--corpus',required=True);p.add_argument('--report-directory',required=True);args=p.parse_args();print(json.dumps(run(args)))
