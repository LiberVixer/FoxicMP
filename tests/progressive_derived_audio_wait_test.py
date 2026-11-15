"""Short zero-trim audio waits for writer EOF; excessive pending queue rejects."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
from progressive_stream_remux_worker_test import serve
from progressive_simple_vfr_preflight import head_without_default
from progressive_webm_opus_remux_preflight import fixture,configuration,probe,packets,pcm,children
from progressive_webm_remux_preflight import scan
from progressive_webm_fixtures import width
from progressive_webm_vfr_preflight import ebml,unsigned

MODE='continuous-webm-vp9-opus-simple-vfr-derived-av-v1'


def many_clusters(raw):
    layout=scan(raw);out=bytearray(raw[:layout['headBytes']])
    for cluster in layout['clusters']:
        stamp=None
        for tag,_,body,end in children(raw,cluster['body'],cluster['end']):
            if tag==0xe7:stamp=int.from_bytes(raw[body:end],'big')
            if tag not in (0xa3,0xa0):continue
            value=raw[body:end]
            block=value if tag==0xa3 else next(raw[a:z] for key,_,a,z in children(raw,body,end) if key==0xa1)
            w=width(block[0]);ticks=stamp+int.from_bytes(block[w:w+2],'big',signed=True)
            block=block[:w]+b'\0\0'+block[w+2:]
            if tag==0xa3:value=block
            else:value=b''.join(ebml(key,block if key==0xa1 else raw[a:z]) for key,_,a,z in children(raw,body,end) if key!=0xbf)
            out+=ebml(0x1f43b675,unsigned(0xe7,ticks)+ebml(tag,value))
    return bytes(out)


def run(worker,report):
    report.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='fox-audio-zero-trim-wait-') as t:
        root=Path(t);source,_=fixture(root,('zero-trim-short',False,1,'20','audio',95688,False))
        source.write_bytes(head_without_default(source.read_bytes()));raw=source.read_bytes();meta=probe(source,root,'source-probe');reference=pcm(source,root,'source-pcm')
        assert len(reference)==95688*4
        audio_count=len(packets(meta,'audio'));observed=[]
        def waiting(output,event):
            if not event['mediaReady']:return
            path=root/'partial.webm';path.write_bytes(output);current=probe(path,root,'partial-probe')
            assert len(packets(current,'audio'))<audio_count
            observed.append(dict(available=event['available'],audioPackets=len(packets(current,'audio')),videoPackets=len(packets(current,'video'))))
        result,output=serve(worker,source,mode=MODE,format_hint='webm',video_duration=240000000,on_wait=waiting)
        assert result['exitCode']==0 and int(result['result']['derivedAudioSamples'])==95688 and len(observed)>=3
        (report/'wait-observations.json').write_text(json.dumps(observed,indent=2)+'\n')
        assert all(a['audioPackets']<=b['audioPackets'] for a,b in zip(observed,observed[1:]))
        # The muxer can interleave delayed audio packets among much later video.
        # Counts may advance, but the final packet must still wait for writer EOF.
        target=root/'complete.webm';target.write_bytes(output);actual=probe(target,root,'complete-probe')
        for kind in ('video','audio'):assert packets(actual,kind)==packets(meta,kind)
        assert pcm(target,root,'complete-pcm')==reference
        path=root/'many-clusters.webm';path.write_bytes(many_clusters(raw))
        refused,_=serve(worker,path,mode=MODE,format_hint='webm',video_duration=240000000,growing=False)
        assert refused['exitCode']==1 and refused['result']['code']=='stream-webm-pending-limit',refused
        assert source.read_bytes()==raw
    out=dict(result='PASS',mode=MODE,ordinaryMainTested=False,scope='worker-wait-and-budget',
        shortZeroTrimWaitsForWriterEof=True,pendingQueueLimitRejected=True,exactPacketsAndClocks=True,
        exactPcm=True,sourceUnchanged=True,waitObservations=observed,worker=result,
        pendingBudgetRefusal=refused,workerSha256=hashlib.sha256(worker.read_bytes()).hexdigest())
    (report/'result.json').write_text(json.dumps(out,indent=2)+'\n');print('PASS short zero-trim waiting and pending budget')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--worker',type=Path,required=True);p.add_argument('--report-directory',type=Path,required=True)
    a=p.parse_args();run(a.worker.resolve(),a.report_directory.resolve())
