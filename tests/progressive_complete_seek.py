"""Seek across a completed 90-second source first opened while incomplete."""
import argparse,copy,hashlib,json,subprocess,sys,time,uuid
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_client import ProgressiveClient
from progressive_integrity import protect_snapshot
from progressive_v1_conformance import fragment_prefix
from progressive_v2_playback_conformance import forget_closed_readers

PROFILES=('v1-indexed','v1-fragmented','v2-separate','v2-head','v2-tail')


def fixture(root,fixtures,profile):
    root=Path(root);root.mkdir(parents=True,exist_ok=True);v1=profile.startswith('v1-')
    if profile in ('v2-head','v2-tail'):
        from progressive_indexed_mp4_fixture import fixture as head,publish as head_publish,prefix_at
        from progressive_tail_mp4_fixture import fixture as tail,publish as tail_publish
        model,raw,_,groups,scales=(tail if profile=='v2-tail' else head)(root,seconds=90)
        write=tail_publish if profile=='v2-tail' else head_publish
        def publish(previous=None,complete=False):return write(model,raw,root,len(raw) if complete else prefix_at(groups,scales,12),previous=previous,complete=complete)
    else:
        fragmented=profile!='v1-indexed';inputs=[];prefixes=[]
        for i,name in enumerate(('video.mp4','audio.m4a')):
            source=Path(fixtures)/'progressive-seek'/name
            if not fragmented:
                target=root/('original-'+name)
                subprocess.run(['ffmpeg','-v','error','-y','-i',str(source),'-map','0','-c','copy','-movflags','+faststart',str(target)],check=True,capture_output=True);source=target
            content=source.read_bytes();inputs.append(content)
            if fragmented:prefixes.append(fragment_prefix(content,12))
            else:
                packets=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-of','json',str(source)]))['packets']
                prefixes.append(max(int(p['pos'])+int(p['size']) for p in packets if float(p['pts_time'])<12))
        model=dict(schemaVersion=1 if v1 else 2,sessionId=uuid.uuid4().hex,generation=1 if v1 else '1',revision=1 if v1 else '1')
        if v1:model.update(state='downloading',duration100ns=900000000,tracks={})
        else:model.update(writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='900000000',timestampOffset='0'),resources=[],tracks=[])
        for i,kind in enumerate(('video','audio')):
            name=kind+'.part';length=len(inputs[i])
            if v1:model['tracks'][kind]=dict(path=name,container='mp4' if i==0 else 'm4a',fragmented=fragmented,initializationReady=True,availablePrefixBytes=prefixes[i],expectedTotalBytes=length,state='downloading')
            else:
                model['resources'].append(dict(id=kind,name=name,length=str(length),state='downloading',ranges=[]))
                model['tracks'].append(dict(id=kind,kind=kind,required=True,codecHint='h264' if i==0 else 'aac-lc',containerHint='fmp4',initializationReady=True,state='downloading',source=dict(resourceId=kind)))
        def publish(previous=None,complete=False):
            value=copy.deepcopy(model)
            if previous:value['revision']=int(previous['revision'])+1 if v1 else str(int(previous['revision'])+1)
            for i,kind in enumerate(('video','audio')):
                stop=len(inputs[i]) if complete else prefixes[i];path=root/(kind+'.part')
                with path.open('ab') as stream:stream.write(inputs[i][stream.tell():stop]);stream.flush()
                if v1:value['tracks'][kind].update(availablePrefixBytes=stop,state='complete' if complete else 'downloading')
                else:
                    value['resources'][i].update(ranges=[dict(offset='0',length=str(stop))],state='complete' if complete else 'downloading')
                    value['tracks'][i]['state']='complete' if complete else 'downloading'
            if v1:value['state']='complete' if complete else 'downloading'
            else:value['writer']['state']='complete' if complete else 'downloading'
            return value if v1 else protect_snapshot(value,root,previous=previous)
    if not v1:model['metadata']=dict(revision='1',title='Completed seek '+profile,chapters=[dict(id='c'+str(t),time=str(t*10000000),title=str(t)) for t in (0,2,45,65,88)])
    return publish


def exercise(client,fixtures,work,control,profile):
    root=Path(work).resolve();publish=fixture(root,fixtures,profile);value=publish();reader=None
    report=dict(profile=profile,phases=[],durationSeconds=90)
    def status():return client.command('status' if value.get('state',value.get('writer',{}).get('state'))=='complete' else 'heartbeat',value['sessionId'],readerId=reader)
    def wait(predicate,label):
        deadline=time.monotonic()+25
        while time.monotonic()<deadline:
            s=status();assert s.get('ok') and s.get('state')!='error',s
            assert s['readerId']==reader,s
            if predicate(s):report['phases'].append(dict(phase=label,status=s));return s
            time.sleep(.05)
        raise AssertionError((label,s))
    try:
        started=client.start(value,session_directory=str(root),track_reader=True,activate=False,title='Completed seek '+profile);reader=started['readerId']
        wait(lambda s:s['state']=='playing' and s['positionSeconds']>=1,'playing-incomplete')
        control('tune',None);control('pause',None)
        before=wait(lambda s:s['state']=='paused','manual-pause-before-complete')
        value=publish(previous=value,complete=True);client.update(value,complete=True,session_directory=str(root),reader_id=reader)
        before=wait(lambda s:s['state']=='paused' and s['complete'],'complete-keeps-pause')
        names=[t['path'] for t in value['tracks'].values()] if value['schemaVersion']==1 else [r['name'] for r in value['resources']]
        published_hashes={name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in names}
        saved=before.get('controls');selection=before.get('audioSelection')
        if client.request(dict(version=1,action='capabilities')).get('completedSourceSeek'):
            assert saved and saved['speed']>1 and 0<=saved['volumePercent']<100 and saved['muted'],before
        for target in (65,2,45):
            accepted=control('seek',target);assert accepted is not False,('seek rejected',target)
            s=wait(lambda s:s['state']=='paused' and abs(s['positionSeconds']-target)<.25 and not s.get('seeking',False),'paused-seek-'+str(target))
            time.sleep(.35);stable=status();assert abs(stable['positionSeconds']-s['positionSeconds'])<.08,stable
            if saved is not None:assert stable['controls']==saved,(saved,stable)
            if selection is not None:assert stable['audioSelection']==selection,(selection,stable)
        control('pause',None);s=wait(lambda s:s['state']=='playing' and s['positionSeconds']>=46,'resume-at-selected-position')
        if saved is not None:assert s['controls']==saved,(saved,s)
        control('seek',88);wait(lambda s:s['positionSeconds']>=87.7,'seek-to-final-tail')
        wait(lambda s:s['state']=='finished' and s.get('released'),'natural-eof-and-release')
        assert client.release_reader(reader,timeout=10)['released'];forget_closed_readers(client,value);reader=None
        assert all(hashlib.sha256((root/name).read_bytes()).hexdigest()==digest for name,digest in published_hashes.items())
        report.update(result='PASS',originalsUnchanged=True,readerIdentityPreserved=True,pausePreserved=True,controlsObserved=saved,selectionPreserved=selection is not None)
        return report
    finally:
        if reader:
            client.release_reader(reader,timeout=10)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile');p.add_argument('--fixtures',required=True);p.add_argument('--work',required=True);p.add_argument('--media-profile',choices=PROFILES,required=True);a=p.parse_args();sequence=[0]
    def control(action,value):
        sequence[0]+=1;root=Path(a.work);tmp=root/'control.tmp';tmp.write_text(json.dumps(dict(sequence=sequence[0],action=action,value=value)),encoding='utf-8');deadline=time.monotonic()+5
        while True:
            try:tmp.replace(root/'control.json');break
            except PermissionError:
                if time.monotonic()>=deadline:raise
                time.sleep(.01)
        while time.monotonic()<deadline:
            try:reply=json.loads((root/'ack.json').read_text(encoding='utf-8'))
            except (OSError,json.JSONDecodeError):reply={}
            if reply.get('sequence')==sequence[0]:return reply['accepted']
            time.sleep(.02)
        raise TimeoutError('Test UI command not acknowledged')
    print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.fixtures,a.work,control,a.media_profile)))
