"""Player-only catalog producer and fixtures; no downloader integration."""
import argparse,copy,hashlib,json,os,time
from pathlib import Path
from progressive_segment_playback import fixture,publish_resources
from progressive_integrity import protect_snapshot
from progressive_client import ProgressiveClient,ApiError
from progressive_v2 import V2PreviewSession
from progressive_v2_playback_conformance import forget_closed_readers

def catalog_fixture(fixtures,profile='h264'):
    root=Path(fixtures)/'progressive-seek'
    video='video-hevc10.mp4' if profile.startswith('hevc10') else 'video-av1-10.mp4' if profile.startswith('av1-10') else 'video.mp4'
    value,data,expected=fixture(fixtures,profile,paths=dict(video=root/video,audio=root/('opus.mp4' if '-opus' in profile else 'audio.m4a')),duration=90,resource_limit=None)
    value['metadata']['chapters']=[dict(id='chapter-'+str(t),time=str(t*10000000),title='At '+str(t)) for t in (0,2,35,68,87)]
    return value,data,expected

def catalog_snapshot(value,data,root,seconds,*,previous=None,complete=False):
    """Publish complete prefixes, archive stable groups of 16 segments/track.

    Future not-yet-published segment descriptors stay in the producer plan;
    pages contain only complete resources. The root has a bounded inline tail.
    """
    value=copy.deepcopy(value);root=Path(root)
    publish_resources(value,data,root,seconds)
    used=set()
    for track in value['tracks']:
        parts=[s for s in track['source']['segments'] if int(s['start'])<seconds*10000000]
        track['source']['segments']=parts;used.add(track['initializationResourceId']);used.update(s['resourceId'] for s in parts)
        if complete:track['state']='complete'
    value['resources']=[r for r in value['resources'] if r['id'] in used]
    if complete:value['writer']['state']='complete'
    expanded=protect_snapshot(value,root,previous=previous)
    result=copy.deepcopy(expanded);result['segmentCatalog']=[]
    tracks=result['tracks'];count=min(len(t['source']['segments']) for t in tracks)//16
    resources={r['id']:r for r in result['resources']};archived=set()
    for i in range(count):
        page=dict(version=1,sessionId=value['sessionId'],generation=value['generation'],writerInstanceId=value['writer']['instanceId'],pageId='page-'+str(i),resources=[],tracks=[])
        for track in tracks:
            parts=track['source']['segments'][i*16:(i+1)*16]
            page['tracks'].append(dict(trackId=track['id'],segments=parts))
            for part in parts:
                rid=part['resourceId'];page['resources'].append(resources[rid]);archived.add(rid)
        raw=json.dumps(page,separators=(',',':'),ensure_ascii=False).encode('utf-8')
        assert len(raw)<=65536
        name='page-'+str(i)+'.json';path=root/name
        if path.exists():assert path.read_bytes()==raw,'Published page changed'
        else:path.write_bytes(raw)
        result['segmentCatalog'].append(dict(id=page['pageId'],name=name,length=str(len(raw)),sha256=hashlib.sha256(raw).hexdigest()))
    result['resources']=[r for r in result['resources'] if r['id'] not in archived]
    for track in tracks:track['source']['segments']=track['source']['segments'][count*16:]
    assert len(result['resources'])<=64 and all(len(t['source']['segments'])<=128 for t in tracks)
    assert len(json.dumps(result,separators=(',',':')).encode())<=65536
    return result,expanded

def exercise(client,fixtures,work,pause,seek,profile='h264'):
    root=Path(work).resolve();root.mkdir(parents=True)
    plan,data,_=catalog_fixture(fixtures,profile)
    value,previous=catalog_snapshot(plan,data,root,20)
    caps=client.request(dict(version=1,action='capabilities'))['schema2Playback']['segmentedPlayback']['catalog']
    assert caps['mode']=='paged-fmp4-v1' and caps['maxOpenResources']==8 and caps['retainAllPublishedResourcesUntilRelease'],caps
    preview=V2PreviewSession(client,str(root),title='Paged fMP4 catalog')
    report=dict(profile=profile,phases=[],networkRequests=0,plannedResources=len(plan['resources']))
    def wait(predicate,label,timeout=30):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            state=preview.heartbeat()
            assert state.get('ok') and not state.get('error') and state.get('state')!='error',state
            if preview.reader_id:assert state.get('readerId')==preview.reader_id,state
            if predicate(state):report['phases'].append(dict(phase=label,status=state));return state
            time.sleep(.1)
        raise AssertionError((label,state))
    def publish(seconds,complete=False):
        nonlocal value,previous
        plan['revision']=str(int(plan['revision'])+1)
        candidate,expanded=catalog_snapshot(plan,data,root,seconds,previous=previous,complete=complete)
        response=preview.publish(candidate);value,previous=candidate,expanded;return response
    def active_readers():
        return [r for r in client.readers(plan['sessionId'],int(plan['generation'])) if not r.get('released')]
    try:
        # Admission must not turn a missing index into post-download fallback.
        path=root/value['segmentCatalog'][0]['name'];raw=path.read_bytes();path.unlink()
        probe=client.probe(value,root)
        assert not probe['supported'] and probe['reasonCode']=='waiting-index',probe
        answer=preview.publish(value)
        assert answer.get('state')=='waiting-index' and not preview.started and not preview.uncertain,answer
        path.write_bytes(raw)
        probe=client.probe(value,root);assert probe['supported'],probe
        forget_closed_readers(client,value)
        preview.publish(value);reader=preview.reader_id
        wait(lambda s:s['state']=='playing' and s['positionSeconds']>=2,'playing-before-complete')
        pause();state=wait(lambda s:s['state']=='paused','user-pause');position=state['positionSeconds']
        publish(40);time.sleep(.4)
        state=preview.heartbeat();assert state['state']=='paused' and abs(state['positionSeconds']-position)<.35,state
        assert state['segmentCatalog']['resourceCount']>64,state
        assert len(active_readers())<=9
        seek(35);wait(lambda s:s['state']=='paused' and 34.7<=s['positionSeconds']<36,'forward-seek-after-64-resources')
        pause();wait(lambda s:s['state']=='playing' and s['positionSeconds']>=36,'decode-after-forward-seek')
        pause();wait(lambda s:s['state']=='paused','pause-before-backward-seek')
        seek(2);wait(lambda s:s['state']=='paused' and 1.7<=s['positionSeconds']<3,'backward-seek-reopens-evicted-media')
        # New missing page/update is retryable; it does not poison old playback.
        plan['revision']=str(int(plan['revision'])+1)
        candidate,expanded=catalog_snapshot(plan,data,root,80,previous=previous)
        page=root/candidate['segmentCatalog'][-1]['name'];page_bytes=page.read_bytes();page.unlink()
        try:preview.publish(candidate)
        except ApiError as error:assert error.code=='waiting-index',error
        else:raise AssertionError('Missing appended page was accepted')
        assert preview.heartbeat()['revision']==value['revision']
        page.write_bytes(page_bytes);preview.publish(candidate);value,previous=candidate,expanded
        state=preview.heartbeat();assert state['segmentCatalog']['resourceCount']>128,state
        seek(68);wait(lambda s:s['state']=='paused' and 67.7<=s['positionSeconds']<69,'seek-after-128-resources')
        pause();wait(lambda s:s['state']=='playing' and s['positionSeconds']>=69,'decode-after-catalog-growth')
        pause();wait(lambda s:s['state']=='paused','pause-before-complete')
        publish(100,True)
        seek(87);wait(lambda s:s['state']=='paused' and 86.7<=s['positionSeconds']<88,'seek-near-completed-end')
        pause();wait(lambda s:s['state']=='finished' and s.get('released'),'complete-eof-and-release',timeout=20)
        assert preview.close(timeout=10)['released'];forget_closed_readers(client,value)
        # Files remain usable for a new owner after all old cache leases close.
        preview=V2PreviewSession(client,str(root),title='Paged catalog reopen')
        preview.publish(value);assert preview.reader_id!=reader
        wait(lambda s:s['state']=='playing' and s['positionSeconds']>=2,'reopen-completed-catalog')
        assert preview.close(timeout=10)['released'];forget_closed_readers(client,value)
        report.update(result='PASS',readerIdentityPreserved=True,playingBeforeComplete=True,forwardSeek=True,backwardSeek=True,
                      missingPageRetry=True,reopened=True,released=True,maxResources=len(previous['resources']))
        return report
    finally:
        if not preview.closed:preview.close(timeout=10)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--player',required=True);parser.add_argument('--profile',required=True)
    parser.add_argument('--fixtures',required=True);parser.add_argument('--work',required=True);parser.add_argument('--media-profile',choices=('h264','hevc10-opus','av1-10'),default='h264');a=parser.parse_args()
    sequence=0
    def command(action,value=0):
        global sequence
        sequence+=1;path=Path(a.work)/'control.json';temp=path.with_suffix('.tmp')
        temp.write_text(json.dumps(dict(sequence=sequence,action=action,value=value)))
        deadline=time.monotonic()+3
        while True:
            try:temp.replace(path);break
            except PermissionError:
                if time.monotonic()>=deadline:raise
                time.sleep(.01)
    print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.fixtures,a.work,lambda:command('pause'),lambda t:command('seek',t),a.media_profile)))
