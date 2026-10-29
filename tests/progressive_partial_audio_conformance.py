"""Player-only partial optional audio: ignored tail, selection, resume and flush.

The caller controls actual UI commands; no downloader changes are involved.
"""
import argparse, copy, json, sys, time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_client import ProgressiveClient
from progressive_v2 import V2PreviewSession
from progressive_multitrack_conformance import fixture
from progressive_v1_conformance import fragment_prefix
from progressive_webm_fixtures import webm_prefix
from progressive_v2_playback_conformance import forget_closed_readers
PROFILES=('aac','opus-webm','vorbis','opus-fmp4')

def exercise(client,fixtures,work,profile,select_audio,pause,seek):
    root=Path(work);value,data=fixture(fixtures,root)
    value['metadata']['chapters'][1]['time']='20000000'
    index={'aac':1,'opus-webm':2,'vorbis':3,'opus-fmp4':2}[profile]
    extra=copy.deepcopy(value['tracks'][index]);resource=copy.deepcopy(value['resources'][index]);content=data[index]
    if profile=='opus-fmp4':
        content=(Path(fixtures)/'progressive-opus-2/audio.mp4').read_bytes();extra['containerHint']='fmp4'
    extra.update(id='optional-audio',required=False,default=False,label='Partial '+profile,initializationReady=profile!='aac')
    extra['source']={'resourceId':'optional-audio'};resource.update(id='optional-audio',name='optional.part',length=None,state='downloading')
    value['tracks']=value['tracks'][:2]+[extra,value['tracks'][4]];value['resources']=value['resources'][:2]+[resource,value['resources'][4]]
    count_prefix=fragment_prefix if extra['containerHint']=='fmp4' else webm_prefix
    def prefix_at(seconds):
        count=count_prefix(content,seconds);path=root/resource['name'];previous=path.stat().st_size if path.exists() else 0
        assert previous<=count
        with path.open('ab') as stream:stream.write(content[previous:count])
        resource['ranges']=[dict(offset='0',length=str(count))]
    prefix_at(6)
    for i,r in enumerate(value['resources'][:2]):
        count=fragment_prefix(data[i],19);(root/r['name']).write_bytes(data[i][:count]);r['ranges']=[dict(offset='0',length=str(count))]
        if profile=='aac':
            with (root/r['name']).open('ab') as stream:stream.write(data[i][count:])
            r.update(length=str(len(data[i])),ranges=[dict(offset='0',length=str(len(data[i])))],state='complete')
            value['tracks'][i]['state']='complete'
    caps=client.request(dict(version=1,action='capabilities'))['schema2Playback']['optionalAudio']
    assert not caps['completeFileRequired'] and caps['selectedTrackBuffering'],caps
    assert client.probe(value,root)['supported']
    preview=V2PreviewSession(client,str(root),title='Partial audio '+profile);report=dict(profile=profile,phases=[])
    try:
        preview.publish(value);reader=preview.reader_id
        def wait(predicate,label,timeout=25):
            deadline=time.monotonic()+timeout;state=None
            while time.monotonic()<deadline:
                state=preview.heartbeat();(root/'last-status.json').write_text(json.dumps(dict(phase=label,status=state),ensure_ascii=False,indent=2),encoding='utf-8')
                assert not state.get('error') and state['state']!='error',state
                assert state['readerId']==reader,state
                if selected(state) and state['state']=='playing':
                    assert state['subtitleTracks'] and all(t['loaded'] for t in state['subtitleTracks']),state
                if predicate(state):
                    report['phases'].append(dict(phase=label,status=state));return state
                time.sleep(.1)
            raise AssertionError(('partial audio timeout',label,state))
        def selected(s):return any(t['id']=='optional-audio' and t.get('selected') for t in s['mediaTracks'])
        def publish():value['revision']=str(int(value['revision'])+1);preview.publish(value)
        if profile=='aac':
            wait(lambda s:s['positionSeconds']>=2 and s['audioSelection']['count']==1,'primary-before-late-partial-audio')
            extra['initializationReady']=True;publish()
        state=wait(lambda s:s['positionSeconds']>=7.5 and s['audioSelection']['count']==2,'unselected-tail-does-not-block')
        assert state['availableSeconds']>=17 and next(t for t in state['mediaTracks'] if t['id']=='optional-audio')['ready'],state
        select_audio(1)
        state=wait(lambda s:selected(s) and s['state']=='buffering' and s['availableSeconds']<6.5,'selected-behind-clock-buffers')
        time.sleep(.4)
        if profile=='aac':
            # Required A/V is already complete. Selected partial audio must
            # nevertheless retain downloader liveness and never become EOF.
            deadline=time.monotonic()+18
            while time.monotonic()<deadline:
                disconnected=preview.status()
                assert not disconnected.get('error'),disconnected
                if not disconnected['connected']:break
                time.sleep(.2)
            else:raise AssertionError(('selected audio lost writer-liveness guard',disconnected))
            assert disconnected['state']=='waiting-downloader',disconnected
            report['phases'].append(dict(phase='selected-partial-writer-timeout',status=disconnected))
            preview.heartbeat()
        select_audio(0)
        wait(lambda s:s['audioSelection']['index']==0 and s['state']=='playing' and s['positionSeconds']>=8,'switch-back-during-buffering')
        prefix_at(14);publish();select_audio(1)
        wait(lambda s:selected(s) and s['state']=='playing' and s['positionSeconds']>=9,'partial-audio-playing')
        state=wait(lambda s:selected(s) and s['state']=='buffering' and s['positionSeconds']>=12,'selected-tail-buffers')
        position=state['positionSeconds'];time.sleep(.5)
        held=preview.heartbeat();assert abs(held['positionSeconds']-position)<.5,held
        prefix_at(19);publish()
        wait(lambda s:selected(s) and s['state']=='playing' and s['positionSeconds']>position+.3,'published-audio-resumes')
        pause();state=wait(lambda s:s['state']=='paused','pause-selected-audio');position=state['positionSeconds'];time.sleep(.4)
        held=preview.heartbeat();assert abs(held['positionSeconds']-position)<.3,held
        seek(2);wait(lambda s:selected(s) and s['state']=='paused' and 1.8<=s['positionSeconds']<3,'paused-backward-seek')
        pause();wait(lambda s:selected(s) and s['state']=='playing' and s['positionSeconds']>=3,'resume-after-seek')
        assert (root/resource['name']).stat().st_size<len(content)
        preview.close(timeout=10);forget_closed_readers(client,value)
        report.update(result='PASS',readerIdentityPreserved=True,released=True);return report
    finally:
        if not preview.closed:preview.close(timeout=10)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile',required=True);p.add_argument('--fixtures',required=True);p.add_argument('--work',required=True);p.add_argument('--media-profile',choices=PROFILES,required=True);a=p.parse_args()
    sequence=[0]
    def command(action,value=None):
        sequence[0]+=1
        path=Path(a.work)/'control.json';temp=path.with_suffix('.tmp');temp.write_text(json.dumps(dict(sequence=sequence[0],action=action,value=value)),encoding='utf-8');deadline=time.monotonic()+1
        while True:
            try:temp.replace(path);break
            except PermissionError:
                if time.monotonic()>=deadline:raise
                time.sleep(.01)
    print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.fixtures,a.work,a.media_profile,lambda index:command('audio',index),lambda:command('pause'),lambda seconds:command('seek',seconds))))
