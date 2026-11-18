"""Ordinary M4A admission, growing projection, exact compressed bytes and PCM.

Prepare synthetic fixtures once with --prepare; the same corpus can be checked
by native GNU, MSVC and MinGW runners without an encoder on the target machine.
"""
import argparse, copy, hashlib, json, subprocess, tempfile
from pathlib import Path
from progressive_m4a_fixture import fixture, ffmpeg_fixture, publish
from progressive_tail_mp4_fixture import publish as publish_tail
from progressive_mux_ts_conformance import Rpc

def prepare(root):
    root=Path(root);root.mkdir(parents=True,exist_ok=True);cases=[]
    for rate in (44100,48000):
        for channels,aot in ((1,2),(2,2),(1,5),(2,5),(2,29)):
            for tail in (False,True):
                name=f'{rate}-{channels}-{aot}-'+('tail' if tail else 'head');p=root/name
                model,raw,_,info=fixture(p,rate,channels,aot,tail=tail)
                (p/'model.json').write_text(json.dumps(model));(p/'info.json').write_text(json.dumps(info));cases.append(name)
    for rate,channels,aot,samples in ((48000,1,2,1),(44100,2,2,37),(48000,2,5,137),(44100,2,29,37)):
        name=f'short-{rate}-{channels}-{aot}';p=root/name
        model,raw,_,info=fixture(p,rate,channels,aot,samples=samples)
        (p/'model.json').write_text(json.dumps(model));(p/'info.json').write_text(json.dumps(info));cases.append(name)
    for rate,channels,tail in ((44100,1,False),(44100,2,True),(48000,1,True),(48000,2,False)):
        name=f'ffmpeg-{rate}-{channels}-'+('tail' if tail else 'head');p=root/name
        model,raw,_,info=ffmpeg_fixture(p,rate,channels,tail)
        (p/'model.json').write_text(json.dumps(model));(p/'info.json').write_text(json.dumps(info));cases.append(name)
    (root/'cases.json').write_text(json.dumps(cases,indent=2))

def run(runner,corpus):
    corpus=Path(corpus);checks=[];reports=[]
    for name in json.loads((corpus/'cases.json').read_text()):
        case=corpus/name;model=json.loads((case/'model.json').read_text());info=json.loads((case/'info.json').read_text());raw=(case/'original.m4a').read_bytes();original=case/'original.m4a'
        publisher=publish_tail if info['tailOffset'] else publish
        with tempfile.TemporaryDirectory(prefix='foxicmp-m4a-') as temp:
            root=Path(temp);data=root/'data';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'readers')])
            def dump(path,decoder=True):
                n=int(rpc('layout')['tracks'][0]['available']);parts=[]
                for at in range(0,n,65536):
                    r=rpc('read',decoder=decoder,track=0,offset=str(at),length=str(min(65536,n-at)));assert r['bytesRead']>0,r;parts.append(bytes.fromhex(r['hex']))
                value=b''.join(parts);assert len(value)==n;path.write_bytes(value);return value
            try:
                prefix=info['ends'][min(len(info['ends'])-1,max(0,8*info['rate']//info['frame']))]
                value=publisher(model,raw,data,prefix);answer=rpc('open',directory=str(data),session=value);assert answer['ok'],(name,answer)
                assert len(rpc('layout')['tracks'])==1;before=dump(root/'prefix.m4a')
                assert rpc('read',decoder=True,track=0,offset=str(len(before)),length='1')['bytesRead']==-1
                for stop in sorted(set([min(len(raw),prefix+1),len(raw)])):
                    value=publisher(model,raw,data,stop,previous=value);answer=rpc('publish',session=value);assert answer['ok'],(name,answer)
                assert rpc('index')['tracks'][0]['samples']==info['packets']
                value=publisher(model,raw,data,len(raw),previous=value,complete=True);answer=rpc('publish',session=value);assert answer['ok'],(name,answer)
                path=root/'final.m4a';view=dump(path);assert view.startswith(before)
                internal=dump(root/'index.mp4',decoder=False)
                def probe(file):return json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(file)]))
                a,b=probe(path),probe(original);assert len(a['streams'])==1 and len(a['packets'])==len(b['packets']) and int(a['streams'][0]['nb_frames'])==info['packets'],(name,len(a['packets']),len(b['packets']),info['packets'])
                for p,q in zip(a['packets'],b['packets']):
                    assert (p['data_hash'],p['pts'],p['dts'],p['duration'])==(q['data_hash'],q['pts'],q['dts'],q['duration']),(name,p,q)
                    at=int(p['pos']);n=int(p['size']);assert internal[at:at+n]==view[at:at+n]
                def pcm(file):return subprocess.check_output(['ffmpeg','-v','error','-i',str(file),'-af',f"atrim=end_sample={info['samples']}",'-f','s16le','-'],stderr=subprocess.PIPE)
                expected=pcm(original);actual=pcm(path);assert actual==expected and len(actual)==info['samples']*info['channels']*2,(name,len(actual),info)
                assert rpc('read',decoder=True,track=0,offset=str(len(view)),length='1')['bytesRead']==0
                status=rpc('status');assert status['mediaMode']=='standalone-indexed-m4a-aac' and status['audioCodec']==model['tracks'][0]['codecHint'] and status['physicalResources']==1,status
                assert status['m4aWindow']['samples']==str(info['samples']);rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
                assert (data/'audio.part').read_bytes()==raw
                reports.append(dict(profile=name,packets=info['packets'],samples=info['samples'],pcmSha256=hashlib.sha256(actual).hexdigest(),packetHashAndClockExact=True,immutablePrefix=True))
            finally:rpc.close()
    # Failures must release the grouped lease and must not become decoder EOF.
    case=corpus/'48000-2-2-head';model=json.loads((case/'model.json').read_text());raw=(case/'original.m4a').read_bytes()
    mutations=[]
    def patch(tag,delta,value):
        b=bytearray(raw);at=b.index(tag)+delta;b[at:at+len(value)]=value;return bytes(b)
    mutations += [('foreign-codec',patch(b'mp4a',0,b'alac'),None),('oversized-sample-count',patch(b'stsz',12,(250001).to_bytes(4,'big')),None),('external-reference',patch(b'url ',4,b'\0'*4),None),('non-unit-edit',patch(b'elst',20,b'\0'*4),None),('invalid-packet-clock',patch(b'stts',16,(512).to_bytes(4,'big')),None),('offset-outside-mdat',patch(b'stco',12,b'\xff'*4),None),('unknown-length',raw,lambda v:v['resources'][0].update(length=None)),('codec-hint-mismatch',raw,lambda v:v['tracks'][0].update(codecHint='he-aac'))]
    for name,changed,edit in mutations:
        with tempfile.TemporaryDirectory(prefix='foxicmp-m4a-negative-') as temp:
            root=Path(temp);data=root/'data';data.mkdir();value=publish(model,changed,data,len(changed),complete=True)
            if edit:edit(value)
            rpc=Rpc([runner,'--registry',str(root/'readers')])
            try:
                answer=rpc('open',directory=str(data),session=value);assert not answer['ok'],(name,answer)
                assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(name)
            finally:rpc.close()
    for stop in (17,raw.index(b'moov')+20):
        with tempfile.TemporaryDirectory(prefix='foxicmp-m4a-wait-') as temp:
            root=Path(temp);data=root/'data';data.mkdir();value=publish(model,raw,data,stop);rpc=Rpc([runner,'--registry',str(root/'readers')])
            try:
                answer=rpc('open',directory=str(data),session=value);assert not answer['ok'] and answer['code']=='waiting-index',answer
                assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('waiting-index-'+str(stop))
            finally:rpc.close()
    for name in ('sha-rewrite','truncated-source','cancel'):
        with tempfile.TemporaryDirectory(prefix='foxicmp-m4a-integrity-') as temp:
            root=Path(temp);data=root/'data';data.mkdir();value=publish(model,raw,data,len(raw));rpc=Rpc([runner,'--registry',str(root/'readers')])
            try:
                assert rpc('open',directory=str(data),session=value)['ok']
                if name=='cancel':rpc('cancel')
                else:
                    with (data/'audio.part').open('r+b') as f:
                        if name=='truncated-source':f.truncate(100)
                        else:f.seek(len(raw)//2);f.write(b'changed')
                assert rpc('read',decoder=True,track=0,offset='0',length='65536')['bytesRead']==-1
                rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(name+'-fails-without-eof')
            finally:rpc.close()
    tail=corpus/'48000-2-2-tail';model=json.loads((tail/'model.json').read_text());raw=(tail/'original.m4a').read_bytes();info=json.loads((tail/'info.json').read_text())
    with tempfile.TemporaryDirectory(prefix='foxicmp-m4a-tail-wait-') as temp:
        root=Path(temp);data=root/'data';data.mkdir();value=publish_tail(model,raw,data,info['ends'][10],tail_bytes=0);rpc=Rpc([runner,'--registry',str(root/'readers')])
        try:
            answer=rpc('open',directory=str(data),session=value);assert not answer['ok'] and answer['code']=='waiting-index',answer
            assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('unpublished-tail-is-not-authorized-by-file-size')
        finally:rpc.close()
    return dict(result='PASS',profiles=len(reports),realMedia=reports,negativeChecks=checks,playerDecoderTested=False,transcoding=False,originalsUnchanged=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--prepare');p.add_argument('--runner');p.add_argument('--corpus');p.add_argument('--report');args=p.parse_args()
    if args.prepare:prepare(args.prepare)
    else:
        result=run(args.runner,args.corpus);text=json.dumps(result,indent=2)
        if args.report:Path(args.report).write_text(text+'\n')
        print(text)
