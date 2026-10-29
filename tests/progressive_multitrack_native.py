"""Native multi-track/resource/cache contract, without decoder dependencies."""
import copy,json,subprocess,tempfile
from pathlib import Path
from progressive_multitrack_conformance import fixture

def run(runner,fixtures):
    names=[]
    def case(name,edit=None,check=None,error=None):
        with tempfile.TemporaryDirectory(prefix='foxicmp-multi-native-') as folder:
            root=Path(folder);value,data=fixture(fixtures,root/'data')
            if edit:edit(value,data,root/'data')
            process=subprocess.Popen([str(runner),'--registry',str(root/'registry')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
            def rpc(action,**kw):process.stdin.write(json.dumps(dict(action=action,**kw))+'\n');process.stdin.flush();return json.loads(process.stdout.readline())
            def readers():return rpc('readers',version=1,sessionId=value['sessionId'],generation='1')['readers']
            try:
                reply=rpc('open',directory=str(root/'data'),session=value)
                if error:assert not reply['ok'] and reply['code']==error,reply;assert readers()==[]
                else:
                    assert reply['ok'],reply
                    if check:check(value,data,root/'data',rpc,readers)
                    rpc('release');assert readers()==[]
                names.append(name)
            finally:
                process.stdin.close();process.wait(timeout=10);messages=process.stderr.read();assert process.returncode==0,messages
    def initialized(v,data,path,rpc,readers):
        state=rpc('status');assert len(state['mediaTracks'])==4 and [t['codec'] for t in state['mediaTracks']]==['h264','aac-lc','opus','vorbis'],state
        assert [t['label'] for t in state['mediaTracks']][1:]==['Русский AAC','English Opus','Commentary Vorbis']
        assert sum(t['ready'] for t in state['subtitleTracks'])==1;assert len(readers())==5
        for i in range(4):assert rpc('read',track=i,offset='0',length='8')['hex']==data[i][:8].hex()
        files=[Path(t['path']) for t in state['subtitleTracks'] if t['ready']];assert files[0].read_bytes().startswith(b'\xef\xbb\xbf')
        rpc('release');assert all(not p.exists() for p in files)
    case('multi-audio-discovery-labels-languages-optional-missing-srt-and-cache-cleanup',check=initialized)
    def late(v,data,path,rpc,readers):
        r=v['resources'][5];(path/r['name']).write_bytes(data[5]);r.update(length=str(len(data[5])),ranges=[dict(offset='0',length=str(len(data[5])))],state='complete');v['tracks'][5].update(state='complete',initializationReady=True);v['revision']='2'
        assert rpc('publish',session=v)['ok'];state=rpc('status');assert all(t['ready'] for t in state['subtitleTracks']);assert len(readers())==6
        files=[Path(t['path']) for t in state['subtitleTracks']];assert len(set(files))==2 and all(p.exists() for p in files);rpc('release');assert all(not p.exists() for p in files)
    case('late-srt-completion-creates-pinned-reader-and-private-copy',check=late)
    def reject_change(v,data,path,rpc,readers):
        v['revision']='2';v['tracks'][2]['default']=True;v['tracks'][1]['default']=False;assert not rpc('publish',session=v)['ok'];assert rpc('read',track=0,offset='0',length='8')['bytesRead']==8
    case('default-order-change-requires-reopen-and-preserves-current',check=reject_change)
    for name,edit in [('optional-audio',lambda v,d,p:v['tracks'][2].update(required=False)),('two-defaults',lambda v,d,p:v['tracks'][2].update(default=True)),('false-secondary-codec',lambda v,d,p:v['tracks'][3].update(codecHint='opus')),('required-subtitle',lambda v,d,p:v['tracks'][4].update(required=True)),('subtitle-nonzero-offset',lambda v,d,p:v['timeline'].update(timestampOffset='10000'))]:case(name,edit,error='unsupported_media')
    def audio_count(v,data,path,count):
        for r in v['resources'][2:]:
            target=path/r['name']
            if target.exists():target.unlink()
        v['resources']=v['resources'][:2];v['tracks']=v['tracks'][:2]
        for i in range(1,count):
            r=copy.deepcopy(v['resources'][1]);r['id']='audio-'+str(i);r['name']=r['id']+'.part';r['ranges']=copy.deepcopy(r['ranges']);(path/r['name']).write_bytes(data[1][:int(r['ranges'][0]['length'])]);v['resources'].append(r)
            t=copy.deepcopy(v['tracks'][1]);t.update(id=r['id'],default=False);t['source']=dict(resourceId=r['id']);v['tracks'].append(t)
    case('eight-audio-tracks-boundary',lambda v,d,p:audio_count(v,d,p,8),check=lambda v,d,p,r,rs: (len(r('status')['mediaTracks'])==9 or (_ for _ in ()).throw(AssertionError())))
    case('nine-audio-tracks-rejected',lambda v,d,p:audio_count(v,d,p,9),error='unsupported_media')
    def isolated(v,data,path,rpc,readers):
        state=rpc('status');bad=next(t for t in state['subtitleTracks'] if t['id']=='sub-0')
        assert bad['state']=='error' and bad['errorCode'] and bad['error'] and not bad['ready'] and not bad['path'],bad
        assert rpc('verify')['ok'];assert rpc('read',track=0,offset='0',length='8')['bytesRead']==8
        # A later valid track and the manifest revision remain usable.
        r=v['resources'][5];(path/r['name']).write_bytes(data[5]);r.update(length=str(len(data[5])),ranges=[dict(offset='0',length=str(len(data[5])))],state='complete');v['tracks'][5].update(state='complete',initializationReady=True);v['revision']='2'
        assert rpc('publish',session=v)['ok'];state=rpc('status');assert state['revision']=='2'
        assert [t['state'] for t in state['subtitleTracks']]==['error','ready'],state
        assert rpc('read',track=1,offset='0',length='8')['bytesRead']==8
    for name,text in [('utf8',b'\xff'),('nul',b'1\x00'),('clock',b'1\n00:00:00,000 --> 00:00:30,000\ntext\n'),('reverse',b'1\n00:00:02,000 --> 00:00:01,000\ntext\n'),('bare-cr',b'1\r'),('index',b'x\n00:00:00,000 --> 00:00:01,000\ntext\n')]:
        def corrupt(v,data,path,text=text):
            r=v['resources'][4];(path/r['name']).write_bytes(text);r.update(length=str(len(text)),ranges=[dict(offset='0',length=str(len(text)))])
        case('invalid-srt-'+name+'-isolated',corrupt,check=isolated)

    case('missing-completed-srt-isolated',lambda v,d,p:(p/v['resources'][4]['name']).unlink(),check=isolated)
    def oversized(v,d,p):
        r=v['resources'][4];text=b'x'*524289;(p/r['name']).write_bytes(text);r.update(length=str(len(text)),ranges=[dict(offset='0',length=str(len(text)))])
    case('oversized-srt-isolated-before-reading',oversized,check=isolated)
    def failed(v,d,p):
        v['resources'][4]['state']='failed';v['tracks'][4]['state']='failed'
    case('optional-writer-failure-isolated',failed,check=isolated)
    def cached(v,data,path,rpc,readers,publish=False):
        original=next(t for t in rpc('status')['subtitleTracks'] if t['id']=='sub-0');copy=Path(original['path']);before=copy.read_bytes()
        (path/v['resources'][4]['name']).write_bytes(b'changed')
        if publish:
            v['revision']='2';assert rpc('publish',session=v)['ok']
        else:assert rpc('verify')['ok']
        state=rpc('status');track=next(t for t in state['subtitleTracks'] if t['id']=='sub-0')
        assert track['state']=='cached' and track['ready'] and track['path']==original['path'] and track['errorCode'],track
        assert copy.read_bytes()==before;assert rpc('read',track=0,offset='0',length='8')['bytesRead']==8
    case('verified-copy-survives-original-truncation-on-verify',check=cached)
    case('verified-copy-survives-original-truncation-on-update',check=lambda *args:cached(*args,publish=True))
    def late_bad(v,data,path,rpc,readers):
        r=v['resources'][5];text=b'bad SRT';(path/r['name']).write_bytes(text);r.update(length=str(len(text)),ranges=[dict(offset='0',length=str(len(text)))],state='complete');v['tracks'][5].update(state='complete',initializationReady=True);v['revision']='2'
        assert rpc('publish',session=v)['ok'];state=rpc('status');assert state['revision']=='2'
        assert [t['state'] for t in state['subtitleTracks']]==['ready','error'],state
        assert rpc('verify')['ok'];assert rpc('read',track=0,offset='0',length='8')['bytesRead']==8
    case('late-malformed-srt-update-keeps-av-and-existing-subtitle',check=late_bad)
    return dict(result='PASS',passed=len(names),cases=names)
