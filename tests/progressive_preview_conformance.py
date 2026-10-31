"""Actual explicit, bounded RAM MP4 playback in each player edition."""
import argparse,copy,json,subprocess,sys,tempfile,time,uuid
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parents[1]/'qt/tools')]
from progressive_client import ProgressiveClient,ApiError
from progressive_resources import ResourceProbe
from progressive_preview import RemuxPreview
from progressive_backend_demux_conformance import snapshot


def wait(preview,predicate,timeout=15):
    until=time.monotonic()+timeout
    while time.monotonic()<until:
        status=preview.status()
        if predicate(status):return status
        assert status['state'] not in ('error','closed'),status
        time.sleep(.015)
    raise AssertionError(('Preview timeout',status))


def exercise(client):
    results=[];checks=[]
    def reject(message,code):
        try:client.request(message)
        except ApiError as error:assert error.code==code,error.reply
        else:raise AssertionError(('Unexpected accepted command',message))
    with tempfile.TemporaryDirectory(prefix='foxicmp-preview-') as temp:
        root=Path(temp)
        for kind in ('video','av','audio','partial'):
            directory=root/kind;directory.mkdir();path=directory/'original.mp4';seconds='5' if kind=='video' else '8' if kind=='partial' else '1'
            args=['ffmpeg','-v','error','-y']
            if kind!='audio':args+=['-f','lavfi','-i','testsrc2=size=320x180:rate=25:duration='+seconds]
            if kind!='video':args+=['-f','lavfi','-i','sine=sample_rate=48000:duration='+seconds]
            if kind!='audio':args+=['-c:v','libx264','-preset','ultrafast','-threads','2','-g','25']
            if kind!='video':args+=['-c:a','aac']
            subprocess.run(args+['-movflags','+faststart',str(path)],check=True,capture_output=True)
            raw=path.read_bytes();value=snapshot(directory/'input.part',raw,complete=kind!='partial',prefix=len(raw)//2 if kind=='partial' else None)
            probe=ResourceProbe(client);probe.start_remux(value,str(directory),'clip','mp4',packet_limit=128);answer=probe.wait();assert answer['media']['code']=='data',answer
            preview=RemuxPreview(client);start=preview.start(probe,title='Prepared fragment '+kind);assert start['sourceComplete']==(kind!='partial') and start['continuousPlayback'] is False,start
            reject(dict(version=1,action='previewCancel',previewId='f'*32),'unknown_preview')
            loaded=wait(preview,lambda s:s['state']=='playing' and s['durationSeconds']>.2 and int(s['decoderBytesRead'])>0)
            if kind=='video':
                # Replaying the same token is idempotent, including after a
                # caller has lost the start reply. Discovery is read-only.
                identity=preview.preview_id;preview.preview_id=None
                assert preview.status()['previewId']==identity
                retry=dict(version=1,action='previewRemux',probeId=probe.probe_id,requestId=preview.request_id,replace=False,activate=False,title='Prepared fragment '+kind)
                assert client.request(retry)['previewId']==identity
                reject(dict(retry,title='Different request'),'preview_conflict')
                reject(dict(retry,requestId=uuid.uuid4().hex),'session_busy')
                reject(dict(version=1,action='previewStatus',requestId=uuid.uuid4().hex),'unknown_preview')
                checks+=['lost-start-read-only-discovery','idempotent-start','conflicting-token-rejected','implicit-replace-rejected','foreign-request-rejected']
            # The finished preparation and its source receipts can be acknowledged
            # while native decoders hold their own immutable buffer reference.
            probe.forget();assert not client.readers(value['sessionId'],1)
            if kind in ('video','partial'):
                preview.control('pause');paused=wait(preview,lambda s:s['state']=='paused')
                target=min(.4,paused['durationSeconds']/3);preview.control('seek',target);seeked=wait(preview,lambda s:s['state']=='paused' and abs(s['positionSeconds']-target)<.15)
                assert preview.status()['previewId']==preview.preview_id
                preview.control('play');resumed=wait(preview,lambda s:s['state']=='playing');checks+=['pause-'+kind,'seek-'+kind,'resume-'+kind]
            if kind=='video':
                preview.control('seek',loaded['durationSeconds']-.2)
            ended=wait(preview,lambda s:s['state']=='finished')
            assert ended['durationSeconds']>.2 and abs(ended['positionSeconds']-ended['durationSeconds'])<.25,ended
            assert ended['sourceComplete']==(kind!='partial') and ended['boundedClip'] and not ended['continuousPlayback'],ended
            checks.append('real-playback-and-distinct-clip-eof-'+kind)
            preview.cancel();closed=preview.wait_released();assert closed['released'],closed
            assert (directory/'input.part').read_bytes()==(raw[:len(raw)//2] if kind=='partial' else raw)
            checks.append('readers-close-and-original-survives-'+kind)
            results.append(dict(profile=kind,playing=loaded,finished=ended,released=closed))
        # Immediate cancellation during native open must not leave readers.
        directory=root/'cancel';directory.mkdir();raw=(root/'video/original.mp4').read_bytes()
        value=snapshot(directory/'input.part',raw,complete=True)
        probe=ResourceProbe(client);probe.start_remux(value,str(directory),'clip','mp4',packet_limit=128);probe.wait()
        for cycle in range(8):
            pending=RemuxPreview(client);pending.start(probe)
            pending.cancel();closed=pending.wait_released()
            assert closed['state']=='closed' and closed['readerCount']==0,closed
        checks.append('eight-immediate-start-cancel-release-cycles')
        probe.forget();assert not client.readers(value['sessionId'],1)
    return dict(result='PASS',count=len(checks),checks=checks,profiles=results,actualPlaybackTested=True,continuousPlayback=False,sourceEofDistinct=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile');a=p.parse_args();print(json.dumps(exercise(ProgressiveClient(a.player,a.profile))))
