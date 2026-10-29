"""Player-only optional SRT isolation with real incomplete A/V playback."""
import argparse,hashlib,json,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_client import ProgressiveClient
from progressive_v2 import V2PreviewSession
from progressive_multitrack_conformance import fixture
from progressive_v1_conformance import fragment_prefix
from progressive_webm_fixtures import webm_prefix
from progressive_v2_playback_conformance import forget_closed_readers

PROFILES=('initial-invalid','late-invalid','cached-truncate','cached-rewrite','missing-complete','writer-failed','cache-missing','required-audio-truncated')

def exercise(client,fixtures,work,profile,select_subtitle=None,seek=None):
    root=Path(work);value,data=fixture(fixtures,root)
    def set_sub(index,text):
        r=value['resources'][index];(root/r['name']).write_bytes(text)
        r.update(length=str(len(text)),ranges=[dict(offset='0',length=str(len(text)))] if text else [],state='complete')
        value['tracks'][index].update(initializationReady=True,state='complete')
    if profile=='initial-invalid':set_sub(4,b'\xff')
    if profile=='missing-complete':(root/value['resources'][4]['name']).unlink()
    if profile=='writer-failed':value['resources'][4]['state']='failed';value['tracks'][4]['state']='failed'
    if profile=='cache-missing':
        for i,r in enumerate(value['resources'][:4]):
            size=(fragment_prefix if i<2 else webm_prefix)(data[i],5);(root/r['name']).write_bytes(data[i][:size]);r['ranges']=[dict(offset='0',length=str(size))]
    caps=client.request(dict(version=1,action='capabilities'))['schema2Playback']['subtitles']
    assert caps['optionalErrorIsolation'] and caps['retainsValidatedCopyOnSourceError'],caps
    assert client.probe(value,root)['supported']
    preview=V2PreviewSession(client,str(root),title='SRT isolation '+profile)
    report=dict(profile=profile);mutated=late=selected=seek_sent=backward=False;cache_path=None;cache_bytes=None
    seek_from=None
    bad_id='sub-1' if profile=='late-invalid' else 'sub-0'
    good_id='sub-0' if profile in ('late-invalid','cached-truncate','cached-rewrite') else 'sub-1'
    try:
        preview.publish(value);reader=preview.reader_id;deadline=time.monotonic()+55
        while time.monotonic()<deadline:
            state=preview.status() if profile=='required-audio-truncated' and mutated else preview.heartbeat();(root/'last-status.json').write_text(json.dumps(state,ensure_ascii=False,indent=2),encoding='utf-8')
            if profile=='required-audio-truncated' and state.get('state')=='error':
                assert mutated and state.get('error'),state
                while not state.get('released') and time.monotonic()<deadline:time.sleep(.05);state=preview.status()
                assert state['released'];assert preview.close(timeout=5)['released'];forget_closed_readers(client,value)
                return dict(result='PASS',profile=profile,requiredFailureRejected=True,terminal=state)
            assert state.get('state')!='error' and not state.get('error'),state
            assert state['readerId']==reader and state['generation']=='1' and not state['complete'],state
            position=state.get('positionSeconds',0);tracks={t['id']:t for t in state.get('subtitleTracks',[])}
            if profile=='cache-missing' and not mutated and tracks.get('sub-0',{}).get('ready'):
                assert state['availableSeconds']<10 and position<1,state
                cache_path=Path(tracks['sub-0']['path']);assert cache_path.suffix=='.srt' and cache_path.parent.name.startswith('subtitles-')
                cache_path.unlink();mutated=True
                for i,r in enumerate(value['resources'][:4]):
                    size=(fragment_prefix if i<2 else webm_prefix)(data[i],12);(root/r['name']).write_bytes(data[i][:size]);r['ranges']=[dict(offset='0',length=str(size))]
                value['revision']=str(int(value['revision'])+1);preview.publish(value)
            if position>=2 and not mutated:
                if profile in ('cached-truncate','cached-rewrite'):
                    cache_path=Path(tracks['sub-0']['path']);cache_bytes=cache_path.read_bytes()
                    target=root/value['resources'][4]['name'];content=target.read_bytes();target.write_bytes(b'changed' if profile=='cached-truncate' else content.replace('Ранние'.encode(),'Поздне'.encode()))
                    assert target.read_bytes()!=content;mutated=True
                elif profile=='late-invalid':
                    set_sub(5,b'bad SRT');value['revision']=str(int(value['revision'])+1);preview.publish(value);mutated=True;late=True
                elif profile=='required-audio-truncated':(root/value['resources'][1]['name']).write_bytes(b'broken');mutated=True
            if profile not in ('late-invalid','cached-truncate','cached-rewrite','required-audio-truncated') and position>=3 and not late:
                set_sub(5,data[5]);value['revision']=str(int(value['revision'])+1);preview.publish(value);late=True
            good=tracks.get(good_id,{})
            if good.get('loaded') and position>=4 and not selected:
                if select_subtitle:select_subtitle()
                selected=True
            if profile=='cache-missing' and position>=6 and selected and not seek_sent and seek:
                seek_from=position;seek(2);seek_sent=True
            if seek_sent and position<seek_from-1:backward=True;report["seekObservedSeconds"]=position;report["seekFromSeconds"]=seek_from
            bad=tracks.get(bad_id,{})
            if position>=10 and selected:
                assert all((root/r['name']).stat().st_size<len(data[i]) for i,r in enumerate(value['resources'][:4]))
                assert good.get('ready') and good.get('loaded') and not good.get('backendError'),state
                if profile=='cache-missing':assert bad.get('backendErrorCode')=='decoder_error' and not bad.get('loaded'),bad;assert backward if seek else True
                elif profile.startswith('cached-'):
                    assert bad['state']=='cached' and bad['ready'] and bad['errorCode'] and cache_path.read_bytes()==cache_bytes,bad
                    report['validatedCopySha256']=hashlib.sha256(cache_bytes).hexdigest()
                else:assert bad['state']=='error' and not bad['ready'] and not bad['path'] and bad['errorCode'],bad
                report.update(playingWhileIncomplete=state,readerIdentityPreserved=True,backwardSeekAfterBackendFailure=backward);break
            time.sleep(.1)
        else:raise AssertionError(('No continued A/V after subtitle error',state))
        for i,r in enumerate(value['resources'][:4]):
            path=root/r['name'];path.rename(path.with_suffix('.done'));path=path.with_suffix('.done')
            with path.open('ab') as stream:stream.write(data[i][stream.tell():])
            r.update(length=str(len(data[i])),ranges=[dict(offset='0',length=str(len(data[i])))],state='complete');value['tracks'][i]['state']='complete'
        for i in (4,5):
            if value['tracks'][i]['state']=='downloading':value['tracks'][i]['state']='cancelled';value['resources'][i]['state']='cancelled'
        value['revision']=str(int(value['revision'])+1);value['writer']['state']='complete';preview.publish(value);deadline=time.monotonic()+25
        while time.monotonic()<deadline:
            state=preview.status();assert state.get('state')!='error' and not state.get('error'),state
            if state.get('state')=='finished' and state.get('released'):report['terminal']=state;break
            time.sleep(.1)
        else:raise AssertionError(('No natural finish after optional SRT failure',state))
        assert preview.close(timeout=5)['released'];forget_closed_readers(client,value)
        report['result']='PASS';return report
    finally:
        if not preview.closed:preview.close(timeout=10)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile',required=True);p.add_argument('--fixtures',required=True);p.add_argument('--work',required=True);p.add_argument('--media-profile',choices=PROFILES,required=True);a=p.parse_args()
    print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.fixtures,a.work,a.media_profile)))
