"""Production Classic MP4 splitter/H264 decoder over managed growing cache."""
import argparse,copy,hashlib,json,subprocess,tempfile
from pathlib import Path
from progressive_backend_demux_conformance import snapshot
from progressive_stream_remux_worker_test import fixture
from progressive_integrity import protect_snapshot

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(runner,worker,report,reordered_video=False):
    rows=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-continuous-graph-') as tmp:
        root=Path(tmp);original=fixture(root,'source',bframes=2 if reordered_video else 0);raw=original.read_bytes();expected_sha=sha(original);reference=None
        decoded=subprocess.check_output(['ffmpeg','-v','error','-i',str(original),'-an','-pix_fmt','nv12','-f','rawvideo','-']);quantum=160*90*3//2;assert len(decoded)==600*quantum
        original_frames=[hashlib.sha256(decoded[at:at+quantum]).hexdigest() for at in range(0,len(decoded),quantum)]
        for mode in ['complete','growing','seek','stop','failure']:
            folder=root/mode;folder.mkdir();path=folder/'input.part';value=snapshot(path,raw,complete=False,prefix=len(raw)//3);value['timeline']['duration']='240000000'
            with path.open('ab') as f:f.write(raw[f.tell():])
            initial=folder/'initial.json';initial.write_text(json.dumps(value),encoding='utf8')
            previous=copy.deepcopy(value);value['revision']='2';value['resources'][0]['ranges']=[dict(offset='0',length=str(len(raw)))];value=protect_snapshot(value,folder,previous=previous)
            full=folder/'full.json';full.write_text(json.dumps(value),encoding='utf8');value=copy.deepcopy(value);value['revision']='3';value['writer']['state']=value['tracks'][0]['state']=value['resources'][0]['state']='complete'
            complete=folder/'complete.json';complete.write_text(json.dumps(value),encoding='utf8');out=folder/'frames.jsonl'
            args=[str(runner),str(initial),str(full),str(complete),str(worker),str(folder/'registry'),str(folder/'cache'),str(out),mode]
            if reordered_video:args.append('reordered')
            result=subprocess.run(args,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=60)
            assert result.returncode==0,(mode,result.returncode,result.stdout,result.stderr)
            status=json.loads(result.stdout);assert status['result']=='PASS' and status['released'] and status['sharedSourceNotCancelled'],status
            frames=[json.loads(line) for line in out.read_text().splitlines()]
            if mode=='complete':
                assert len(frames)==600 and [v['sha256'] for v in frames]==original_frames;reference=frames
                for k,row in enumerate(frames):assert abs(row['pts']-k*400000)<=1 and abs(row['end']-(k+1)*400000)<=1,row
            elif mode=='growing':
                assert frames==reference and 0<status['framesBeforeWriterComplete']<600,(status,len(frames))
            elif mode=='seek':assert len(frames)==350 and [v['sha256'] for v in frames]==[v['sha256'] for v in reference[250:]]
            else:assert len(frames)>0 and status['stopSeconds']<2
            assert sha(path)==expected_sha and sha(original)==expected_sha
            rows.append(dict(profile=mode,result='PASS',exactExitCode=result.returncode,status=status,frameCount=len(frames),decodedSequenceSha256=hashlib.sha256(''.join(v['sha256'] for v in frames).encode()).hexdigest(),stderr=result.stderr))
    report.mkdir(parents=True,exist_ok=True);d=dict(result='PASS',count=len(rows),rows=rows,testSourceSha256=sha(__file__),runnerSha256=sha(runner),workerSha256=sha(worker),temporaryWorkRemoved=not root.exists(),realDirectShowGraph=True,originalDecodedNv12Exact=True,originalDecodedSequenceSha256=hashlib.sha256(''.join(original_frames).encode()).hexdigest(),hardwareDecoder=False,installedClassicLaunched=False,publicPlayerApi=False,reorderedVideo=reordered_video);(report/'result.json').write_text(json.dumps(d,indent=2)+'\n');print(json.dumps(dict(result='PASS',count=len(rows))))
if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('--runner',type=Path,required=True);a.add_argument('--worker',type=Path,required=True);a.add_argument('--report-directory',type=Path,required=True);a.add_argument('--reordered-video',action='store_true');v=a.parse_args();run(v.runner.resolve(),v.worker.resolve(),v.report_directory.resolve(),v.reordered_video)
