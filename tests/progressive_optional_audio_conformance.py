"""Deferred complete optional audio, with no downloader-side implementation."""
import argparse,copy,json,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_client import ProgressiveClient
from progressive_v2 import V2PreviewSession
from progressive_multitrack_conformance import fixture
from progressive_v2_playback_conformance import forget_closed_readers

PROFILES=('late-aac','late-opus','late-vorbis','initial-ready','missing-complete','invalid-complete','writer-failed','never-ready','out-of-order','late-opus-fmp4')

def exercise(client,fixtures,work,profile,select_audio=None,seek=None):
    root=Path(work);value,all_data=fixture(fixtures,root)
    index=1 if profile=='late-aac' else 3 if profile=='late-vorbis' else 2
    extra=copy.deepcopy(value['tracks'][index]);resource=copy.deepcopy(value['resources'][index]);content=all_data[index]
    extra.update(id='optional-audio',required=False,initializationReady=False,state='downloading',default=False,label='Optional '+extra['codecHint'])
    if profile=='late-opus-fmp4':
        content=(Path(fixtures)/'progressive-opus-2/audio.mp4').read_bytes();extra['containerHint']='fmp4'
    extra['source']={'resourceId':'optional-audio'};resource.update(id='optional-audio',name='optional.part',length=None,ranges=[],state='downloading')
    value['tracks']=value['tracks'][:2]+[extra];value['resources']=value['resources'][:2]+[resource]
    positive=profile in ('late-aac','late-opus','late-vorbis','initial-ready','out-of-order','late-opus-fmp4')
    def complete_optional():
        resource.update(length=str(len(content)),ranges=[dict(offset='0',length=str(len(content)))],state='complete');extra.update(initializationReady=True,state='complete')
        if profile!='missing-complete':(root/resource['name']).write_bytes(content if profile!='invalid-complete' else b'x'*len(content))
        if profile=='writer-failed':extra['state']=resource['state']='failed'
    if profile=='initial-ready':complete_optional()
    if profile=='out-of-order':
        complete_optional()
        later=copy.deepcopy(value['tracks'][1]);later.update(id='a-optional-later',required=False,initializationReady=False,default=False,label='Later AAC');later['source']={'resourceId':'a-optional-later'}
        later_resource=dict(id='a-optional-later',name='later-aac.part',length=None,ranges=[],state='downloading')
        value['tracks'].append(later);value['resources'].append(later_resource)
        def complete_optional():
            data=all_data[1];(root/later_resource['name']).write_bytes(data)
            later_resource.update(length=str(len(data)),ranges=[dict(offset='0',length=str(len(data)))],state='complete');later.update(initializationReady=True,state='complete')
    caps=client.request(dict(version=1,action='capabilities'))['schema2Playback']['optionalAudio']
    assert caps['completeFileRequired'] and caps['lateCompletion'] and caps['pendingErrorIsolation'] and caps['activeResourceErrorsFatal'],caps
    assert client.probe(value,root)['supported']
    preview=V2PreviewSession(client,str(root),title='Optional audio '+profile)
    report=dict(profile=profile);published=profile=='initial-ready';selected=seek_sent=backward=False;seek_from=None
    try:
        preview.publish(value);reader=preview.reader_id;deadline=time.monotonic()+50
        while time.monotonic()<deadline:
            state=preview.heartbeat();(root/'last-status.json').write_text(json.dumps(state,ensure_ascii=False,indent=2),encoding='utf-8')
            assert state.get('state')!='error' and not state.get('error'),state
            assert state['readerId']==reader and not state['complete'],state
            tracks={t['id']:t for t in state['mediaTracks']};optional=tracks['optional-audio'];position=state.get('positionSeconds',0)
            if position>=1 and not published:
                pending=tracks['a-optional-later'] if profile=='out-of-order' else optional
                assert not pending['ready'] and pending['state']=='waiting' and not pending.get('loaded'),state
                assert state['availableSeconds']>=10 and state['audioSelection']['count']==(2 if profile=='out-of-order' else 1),state
                report['pendingDidNotBlock']=state
            if position>=3 and not published and profile!='never-ready':
                assert state['audioSelection']['index']==0,state
                complete_optional();value['revision']=str(int(value['revision'])+1);preview.publish(value);published=True
            if positive and published and optional.get('loaded') and position>=4 and not selected:
                # Late attachment must preserve the primary selection.
                assert state['audioSelection']['index']==0 and state['audioSelection']['count']==(3 if profile=='out-of-order' else 2),state
                if select_audio:select_audio()
                selected=True
            if positive and select_audio and selected and position>=6 and not seek_sent and seek:
                assert optional.get('selected'),state
                seek_from=position;seek(2);seek_sent=True
            if seek_sent and position<seek_from-1:backward=True
            if position>=10 and (not positive or selected) and (not seek or not positive or backward):
                assert all((root/r['name']).stat().st_size<len(all_data[i]) for i,r in enumerate(value['resources'][:2]))
                if positive:
                    assert optional['ready'] and optional.get('loaded') and not optional.get('backendError'),state
                    if select_audio:assert optional.get('selected'),state
                elif profile=='never-ready':assert optional['state']=='waiting' and not optional['ready'],state
                else:assert optional['state']=='error' and optional['errorCode'] and not optional['ready'] and not optional.get('loaded'),state
                report.update(playingWhileIncomplete=state,readerIdentityPreserved=True,backwardSeek=backward);break
            time.sleep(.1)
        else:raise AssertionError(('Optional audio did not continue',state))
        for i,r in enumerate(value['resources'][:2]):
            path=root/r['name'];path.rename(path.with_suffix('.done'));path=path.with_suffix('.done')
            with path.open('ab') as stream:stream.write(all_data[i][stream.tell():])
            r.update(length=str(len(all_data[i])),ranges=[dict(offset='0',length=str(len(all_data[i])))],state='complete');value['tracks'][i]['state']='complete'
        if extra['state']=='downloading':extra['state']=resource['state']='cancelled'
        value['revision']=str(int(value['revision'])+1);value['writer']['state']='complete';preview.publish(value);deadline=time.monotonic()+25
        while time.monotonic()<deadline:
            state=preview.status();assert state.get('state')!='error' and not state.get('error'),state
            if state.get('state')=='finished' and state.get('released'):report['terminal']=state;break
            time.sleep(.1)
        else:raise AssertionError(('No natural finish with optional audio',state))
        assert preview.close(timeout=5)['released'];forget_closed_readers(client,value)
        report['result']='PASS';return report
    finally:
        if not preview.closed:preview.close(timeout=10)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile',required=True);p.add_argument('--fixtures',required=True);p.add_argument('--work',required=True);p.add_argument('--media-profile',choices=PROFILES,required=True);a=p.parse_args()
    print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.fixtures,a.work,a.media_profile)))
