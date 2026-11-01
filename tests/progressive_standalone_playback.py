"""Real incomplete one-stream playback shared by Qt and Classic test adapters."""
import argparse,hashlib,json,time
from pathlib import Path
from progressive_standalone_fixture import fixture,publish,fragment_prefix,PROFILES
from progressive_client import ProgressiveClient
from progressive_v2 import V2PreviewSession
from progressive_v2_playback_conformance import forget_closed_readers


def exercise(client,fixtures,work,pause,seek,profile):
    root=Path(work).resolve();model,raw,_,probe=fixture(root,profile);audio=model['tracks'][0]['kind']=='audio';container=model['tracks'][0]['containerHint'];mode='standalone-mp3-indexed' if container=='mp3-indexed' else 'standalone-mp3' if container=='mp3' else 'standalone-flac' if container=='flac' else 'standalone-'+container+'-'+model['tracks'][0]['codecHint']
    caps=client.request(dict(version=1,action='capabilities'))['schema2Playback']['standalone'];caps=caps['mp3Indexed'] if container=='mp3-indexed' else caps['mp3'] if container=='mp3' else caps['adts'] if container=='adts' else caps['rawFlac'] if container=='flac' else caps['webm' if audio else 'webmVideo'] if container=='webm' else caps;assert caps['mode']==('standalone-mp3-indexed-v1' if container=='mp3-indexed' else 'standalone-mp3-v1' if container=='mp3' else 'standalone-adts-aac-lc-v1' if container=='adts' else 'standalone-raw-flac-v1' if container=='flac' else ('standalone-webm-audio-v1' if audio else 'standalone-webm-video-v1') if container=='webm' else 'standalone-fmp4-v1') and mode in caps['profiles'],caps
    if profile.startswith('audio-mp3-indexed-'):
        from progressive_mp3_indexed_fixture import prefix_at_seconds
        prefix=lambda seconds:prefix_at_seconds(raw,probe,seconds)
    elif profile.startswith('audio-mp3-'):
        from progressive_mp3_fixture import prefix_at_seconds
        prefix=lambda seconds:prefix_at_seconds(raw,probe,seconds)
    elif profile.startswith('audio-adts-'):
        from progressive_adts_fixture import prefix_at_seconds
        prefix=lambda seconds:prefix_at_seconds(raw,probe,seconds)
    elif profile.startswith('audio-raw-flac-'):
        from progressive_raw_flac_fixture import prefix_at_seconds
        prefix=lambda seconds:prefix_at_seconds(raw,probe,seconds)
    elif profile.startswith('video-webm-'):
        from progressive_standalone_webm_video_fixture import prefix_at_seconds
        prefix=lambda seconds:prefix_at_seconds(raw,probe,seconds)
    elif profile.startswith('video-fmp4-') or profile.startswith('audio-flac-'):
        from progressive_standalone_fmp4_video_fixture import prefix_at_seconds
        prefix=lambda seconds:prefix_at_seconds(raw,probe,seconds)
    else:prefix=lambda count:fragment_prefix(raw,count)
    value=publish(model,raw,root,17);preview=V2PreviewSession(client,str(root),title='One stream '+profile);report=dict(profile=profile,kind=model['tracks'][0]['kind'],initialLengthKnown=model['resources'][0]['length'] is not None,phases=[])
    try:
        answer=preview.publish(value);assert not preview.started and answer['state']=='waiting-initialization',answer
        value=publish(model,raw,root,prefix(4),previous=value);preview.publish(value);reader=preview.reader_id;assert reader
        def wait(predicate,label):
            deadline=time.monotonic()+35
            while time.monotonic()<deadline:
                try:s=preview.heartbeat()
                except Exception as error:
                    try:last=preview.status()
                    except Exception as status_error:last=dict(statusError=str(status_error))
                    raise AssertionError((profile,label,str(error),last)) from error
                assert s.get('ok') and not s.get('error') and s['state']!='error',s
                assert s['readerId']==reader and s['mediaMode']==mode,s
                assert len(s['mediaTracks'])==1 and s['mediaTracks'][0]['kind']==model['tracks'][0]['kind'],s
                if predicate(s):report['phases'].append(dict(phase=label,status=s));return s
                time.sleep(.08)
            raise AssertionError((profile,label,s))
        wait(lambda s:s['state']=='buffering','four-seconds-buffers')
        value=publish(model,raw,root,prefix(12),previous=value);preview.publish(value)
        wait(lambda s:s['state']=='playing' and s['positionSeconds']>=3,'plays-without-second-track')
        pause();wait(lambda s:s['state']=='paused','pause')
        seek(2);wait(lambda s:s['state']=='paused' and 1.8<=s['positionSeconds']<3,'seek-while-paused')
        pause();wait(lambda s:s['state']=='playing' and s['positionSeconds']>=4,'resume')
        wait(lambda s:s['state']=='buffering' and s['positionSeconds']>=9 and not s['complete'],'tail-waits-not-eof')
        value=publish(model,raw,root,len(raw),previous=value,complete=True);preview.publish(value)
        seek(17);wait(lambda s:s['positionSeconds']>=16.7,'seek-published-tail')
        wait(lambda s:s['state']=='finished' and s.get('released'),'natural-eof-and-release')
        assert preview.close(timeout=10)['released'];forget_closed_readers(client,value)
        assert hashlib.sha256((root/'mux.part').read_bytes()).digest()==hashlib.sha256(raw).digest()
        report.update(result='PASS',readerIdentityPreserved=True,originalUnchanged=True,singleTrack=True,audioChannels=int(probe['streams'][0]['channels']) if audio else 0,audioRate=int(probe['streams'][0]['sample_rate']) if audio else 0);return report
    finally:
        if not preview.closed:preview.close(timeout=10)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile');p.add_argument('--fixtures');p.add_argument('--work',required=True);p.add_argument('--media-profile',choices=PROFILES,required=True);a=p.parse_args();seq=[0]
    def command(action,value=None):
        seq[0]+=1;path=Path(a.work)/'control.json';tmp=path.with_suffix('.tmp');tmp.write_text(json.dumps(dict(sequence=seq[0],action=action,value=value)),encoding='utf-8');deadline=time.monotonic()+2
        while True:
            try:tmp.replace(path);break
            except PermissionError:
                if time.monotonic()>=deadline:raise
                time.sleep(.01)
    print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.fixtures,a.work,lambda:command('pause'),lambda t:command('seek',t),a.media_profile)))
