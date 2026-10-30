"""Shared v2 metadata conformance corpus, executable or player-owned validation.

No media, URL fetching or Harvester dependency. Validation is not playback.
"""
import argparse
import copy
import json
from pathlib import Path
import subprocess

BASE = Path(__file__).parent/'fixtures'/'progressive-v2'/'vod.json'

def sample():
    return json.loads(BASE.read_text())

def cases():
    base = sample()
    yield 'valid-muxed-metadata', dict(session=base), True, None
    protected=copy.deepcopy(base)
    protected['resources'][0]['integrity']=dict(algorithm='sha256',blocks=[dict(offset=r['offset'],length=r['length'],sha256='a'*64) for r in protected['resources'][0]['ranges']])
    yield 'valid-integrity-metadata-without-file-io',dict(session=protected),True,None
    for name,mutate in {
        'algorithm':lambda r:r['integrity'].update(algorithm='sha1'),
        'uppercase':lambda r:r['integrity']['blocks'][0].update(sha256='A'*64),
        'digest-length':lambda r:r['integrity']['blocks'][0].update(sha256='a'*63),
        'numeric-offset':lambda r:r['integrity']['blocks'][0].update(offset=0),
        'coverage-hole':lambda r:r['integrity']['blocks'][0].update(length='1'),
        'overlap':lambda r:r['integrity']['blocks'].append(copy.deepcopy(r['integrity']['blocks'][0])),
        'unconfirmed':lambda r:r['integrity']['blocks'][0].update(length='99'),
        'zero-length':lambda r:r['integrity']['blocks'][0].update(length='0'),
        'block-size-limit':lambda r:r['integrity']['blocks'][0].update(length='4194305'),
    }.items():
        bad=copy.deepcopy(protected);mutate(bad['resources'][0])
        yield 'integrity-invalid-'+name,dict(session=bad),False,'invalid_snapshot'
    for name,mutate in {
        'withdraw':lambda r:r.pop('integrity'),
        'replace-digest':lambda r:r['integrity']['blocks'][0].update(sha256='b'*64),
    }.items():
        bad=copy.deepcopy(protected);bad['revision']='2';mutate(bad['resources'][0])
        yield 'integrity-advance-'+name,dict(session=bad,previous=protected),False,'invalid_snapshot'
    upgraded=copy.deepcopy(protected);upgraded['revision']='2'
    yield 'integrity-cannot-enable-mid-session',dict(session=upgraded,previous=base),False,'invalid_snapshot'
    extended=copy.deepcopy(protected);extended['revision']='2';r=extended['resources'][0]
    end=int(r['ranges'][-1]['offset'])+int(r['ranges'][-1]['length'])
    r['ranges'].append(dict(offset=str(end),length='1'));r['integrity']['blocks'].append(dict(offset=str(end),length='1',sha256='b'*64))
    yield 'integrity-append-new-confirmed-block',dict(session=extended,previous=protected),True,None
    for kind in ('audio','video'):
        value=copy.deepcopy(base);value['tracks']=[t for t in value['tracks'] if t['kind']==kind]
        yield kind+'-only',dict(session=value),True,None
    value=copy.deepcopy(base);value['generation']='18446744073709551615';value['revision']='9007199254740993'
    yield 'full-precision-counters',dict(session=value),True,None
    value=copy.deepcopy(base);value['timeline']['timestampOffset']='-9223372036854775808'
    yield 'signed-minimum',dict(session=value),True,None
    for n in ('0','9007199254740993','9223372036854775807'):
        value=copy.deepcopy(base);value['resources'][0].update(length=n,ranges=[])
        yield 'large-byte-offset-'+n,dict(session=value,range=dict(resourceId='media',offset=n,length='0')),True,None
    value=copy.deepcopy(base);value['resources'][0].update(length='9007199254741010',ranges=[dict(offset='9007199254740993',length='8')])
    yield 'large-range-exact',dict(session=value,range=dict(resourceId='media',offset='9007199254740993',length='8')),True,None
    yield 'large-range-hole',dict(session=value,range=dict(resourceId='media',offset='9007199254740992',length='8')),True,None
    value=copy.deepcopy(base);value['resources'][0]['ranges']=[dict(offset='0',length='10'),dict(offset='10',length='10')]
    yield 'adjacent-ranges-cover',dict(session=value,range=dict(resourceId='media',offset='5',length='15')),True,None
    value=copy.deepcopy(base);value['resources'][0]['ranges']=[dict(offset='0',length='10'),dict(offset='20',length='10')]
    yield 'sparse-hole-unavailable',dict(session=value,range=dict(resourceId='media',offset='5',length='20')),True,None
    yield 'reader-lifecycle',dict(session=base,readerExercise=True),True,None
    value=copy.deepcopy(base);value['timeline'].update(live=True,duration=None,window=dict(start='-10',end='100'))
    yield 'live-window-metadata-only',dict(session=value),True,None
    value=copy.deepcopy(base);value['resources'].append(dict(id='s1',name='s1.part',length=None,ranges=[],state='downloading'))
    value['tracks'][0]['source']=dict(segments=[dict(resourceId='s1',sequence='9007199254740993',start='0',duration='100',timestampOffset='-10',discontinuity=False,configurationId='init1')])
    yield 'segment-sequence-exact',dict(session=value),True,None
    previous=copy.deepcopy(base);value=copy.deepcopy(base);value['revision']='2';value['resources'][0]['ranges']=[dict(offset='0',length='20')]
    yield 'advance-prefix',dict(session=value,previous=previous),True,None
    value=copy.deepcopy(base);value['revision']='2';value['metadata']['revision']='2';value['metadata']['title']='Updated title'
    yield 'metadata-only-update',dict(session=value,previous=base),True,None
    value=copy.deepcopy(base);value['revision']='2';value['metadata']=dict(reversed(list(value['metadata'].items())))
    yield 'metadata-key-order-is-not-a-change',dict(session=value,previous=base),True,None
    segmented=copy.deepcopy(base)
    segmented['tracks'][0]['source']=dict(segments=[dict(resourceId='media',sequence='1',start='-10',duration='10',timestampOffset='0',discontinuity=False,configurationId='c1')])
    value=copy.deepcopy(segmented);value['revision']='2'
    value['tracks'][0]['source']['segments'].append(dict(resourceId='media',sequence='2',start='0',duration='10',timestampOffset='-10',discontinuity=True,configurationId='c2',initializationResourceId='media'))
    yield 'configuration-change-with-init',dict(session=value,previous=segmented),True,None
    bad=copy.deepcopy(value);bad['tracks'][0]['source']['segments'][1].pop('initializationResourceId')
    yield 'configuration-change-without-init',dict(session=bad),False,'invalid_snapshot'
    bad=copy.deepcopy(value);bad['tracks'][0]['source']['segments'][1]['start']='-5'
    yield 'overlapping-segment-timeline',dict(session=bad),False,'invalid_snapshot'
    bad=copy.deepcopy(value);bad['tracks'][0]['source']['segments'][1]['sequence']='1'
    yield 'duplicate-segment-sequence',dict(session=bad),False,'invalid_snapshot'
    bad=copy.deepcopy(segmented);bad['revision']='2';bad['tracks'][0]['source']['segments'][0]['timestampOffset']='1'
    yield 'changed-published-segment',dict(session=bad,previous=segmented),False,'invalid_snapshot'
    value=copy.deepcopy(base);value['revision']='2';value['resources'][0]['ranges']=[dict(offset='0',length='100')];value['resources'][0]['state']='complete'
    for track in value['tracks']:track['state']='complete'
    value['writer']['state']='complete'
    yield 'complete-confirmed-resource',dict(session=value,previous=base),True,None
    finished=copy.deepcopy(value)
    later=copy.deepcopy(finished);later['revision']='3';later['metadata'].update(revision='2',title='Finished metadata correction')
    yield 'metadata-after-writer-complete',dict(session=later,previous=finished),True,None
    bad=copy.deepcopy(finished);bad['revision']='3';bad['resources'].append(dict(id='late',name='late.bin',length='0',ranges=[],state='complete'))
    yield 'new-resource-after-writer-complete',dict(session=bad,previous=finished),False,'invalid_snapshot'
    retired=copy.deepcopy(base);retired['resources'][0]['retired']=True
    yield 'new-reader-cannot-attach-retired-resource',dict(session=retired,readerExercise=True),False,'resource_retired'
    bad=copy.deepcopy(base);bad['resources']*=65
    yield 'resource-count-limit',dict(session=bad),False,'invalid_snapshot'
    for field,numbers in [('generation',[1,1.5,'01','-1','0','18446744073709551616']),('revision',[True,1.0,'+1','1e3','0'])]:
        for i,number in enumerate(numbers):
            value=copy.deepcopy(base);value[field]=number
            yield field+'-invalid-'+str(i),dict(session=value),False,'invalid_snapshot'
    mutations = {
        'unknown-field':lambda v:v.update(unexpected=True),
        'url-resource':lambda v:v['resources'][0].update(name='https://example.invalid/file'),
        'parent-escape':lambda v:v['resources'][0].update(name='../video'),
        'windows-device':lambda v:v['resources'][0].update(name='CON.mp4'),
        'hidden-extension-device':lambda v:v['resources'][0].update(name='lpt9.part'),
        'alternate-data-stream':lambda v:v['resources'][0].update(name='video:stream'),
        'null-path':lambda v:v['resources'][0].update(name='video\0.part'),
        'overlap':lambda v:v['resources'][0].update(ranges=[dict(offset='0',length='10'),dict(offset='5',length='10')]),
        'unsorted':lambda v:v['resources'][0].update(ranges=[dict(offset='10',length='10'),dict(offset='0',length='5')]),
        'zero-range':lambda v:v['resources'][0].update(ranges=[dict(offset='0',length='0')]),
        'overflow-range':lambda v:v['resources'][0].update(length=None,ranges=[dict(offset='9223372036854775807',length='1')]),
        'beyond-length':lambda v:v['resources'][0].update(ranges=[dict(offset='99',length='2')]),
        'unknown-reference':lambda v:v['tracks'][0].update(source=dict(resourceId='unknown')),
        'source-ambiguity':lambda v:v['tracks'][0].update(source=dict(resourceId='media',segments=[])),
        'duplicate-resource-id':lambda v:v['resources'].append(dict(v['resources'][0],name='other.part')),
        'duplicate-portable-name':lambda v:v['resources'].append(dict(v['resources'][0],id='other',name='MEDIA.PART')),
        'duplicate-track-id':lambda v:v['tracks'].append(copy.deepcopy(v['tracks'][0])),
        'negative-zero':lambda v:v['timeline'].update(timestampOffset='-0'),
        'time-overflow':lambda v:v['timeline'].update(timestampOffset='9223372036854775808'),
        'zero-timebase':lambda v:v['timeline']['timebase'].update(denominator=0),
        'timebase-float':lambda v:v['timeline']['timebase'].update(numerator=1.5),
        'live-with-duration':lambda v:v['timeline'].update(live=True),
        'chapter-out-of-bounds':lambda v:v['metadata']['chapters'][0].update(time='9999999999'),
    }
    for name,mutate in mutations.items():
        value=copy.deepcopy(base);mutate(value);yield name,dict(session=value),False,'invalid_snapshot'
    value=copy.deepcopy(base);value['resources'][0]['state']='complete'
    yield 'complete-with-hole',dict(session=value),False,'incomplete_resource'
    value=copy.deepcopy(base);value['tracks'][0]['state']='complete'
    yield 'track-complete-before-resource',dict(session=value),False,'incomplete_resource'
    value=copy.deepcopy(base);value['writer']['state']='complete'
    yield 'writer-complete-before-tracks',dict(session=value),False,'incomplete_resource'
    for name,mutate,code in [
        ('stale-revision',lambda v:None,'stale_revision'),
        ('changed-generation',lambda v:v.update(generation='2'),'generation_changed'),
        ('withdrawn-range',lambda v:v['resources'][0].update(ranges=[]),'invalid_snapshot'),
        ('changed-name',lambda v:v['resources'][0].update(name='renamed.part'),'invalid_snapshot'),
        ('changed-codec',lambda v:v['tracks'][0].update(codecHint='av1'),'invalid_snapshot'),
        ('metadata-without-revision',lambda v:v['metadata'].update(title='different'),'invalid_snapshot'),
    ]:
        value=copy.deepcopy(base);value['revision']='1' if name=='stale-revision' else '2';mutate(value)
        yield name,dict(session=value,previous=base),False,code

def check(name,message,reply,ok,code):
    assert reply.get('ok') is ok,(name,reply)
    if code: assert reply.get('code')==code,(name,reply)
    if ok:
        if name=='full-precision-counters':
            assert reply['generation']=='18446744073709551615' and reply['revision']=='9007199254740993',reply
        if name in ('large-range-exact','adjacent-ranges-cover') and 'rangeCovered' in reply:
            assert reply['rangeCovered'],(name,reply)
        if name in ('large-range-hole','sparse-hole-unavailable') and 'rangeCovered' in reply:
            assert not reply['rangeCovered'],(name,reply)


def check_player(client):
    caps=client.request(dict(version=1,action='capabilities'))
    assert 1 in caps['playbackSchemaVersions'] and caps['validationSchemaVersions']==[2],caps
    count=0
    for name,message,ok,code in cases():
        if message.get('readerExercise') or 'range' in message:
            continue
        request=dict(version=1,action='validate',session=message['session'])
        if 'previous' in message:request['previous']=message['previous']
        try:reply=client.request(request)
        except RuntimeError as error:
            if not hasattr(error,'reply'):raise
            reply=error.reply
        check(name,message,reply,ok,code)
        if ok:
            assert reply['playbackSupported'] is False and reply['mode']=='validation-only',reply
        count+=1
    for action in ('start','probe'):
        try:
            reply=client.request(dict(version=1,action=action,session=sample()))
            assert action=='probe' and reply['supported'] is False and reply['reasonCode']=='unsupported_media',reply
        except RuntimeError as error:
            assert getattr(error,'code',None) in {'unsupported_schema','unsupported_media'},error
    return count


if __name__=='__main__':
    parser=argparse.ArgumentParser();mode=parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--runner');mode.add_argument('--player');parser.add_argument('--profile');parser.add_argument('--expected-pid',type=int)
    args=parser.parse_args()
    if args.player:
        import sys
        sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt'/'tools'))
        from progressive_client import ProgressiveClient
        client=ProgressiveClient(args.player,args.profile)
        if args.expected_pid is not None:
            assert client.request(dict(version=1,action='capabilities'))['pid']==args.expected_pid
        print(json.dumps(dict(passed=check_player(client),mode='player-api-validation-only')))
        raise SystemExit(0)
    vectors=list(cases());raw=''.join(json.dumps(m,separators=(',',':'))+'\n' for _,m,_,_ in vectors)
    process=subprocess.run([args.runner],input=raw,capture_output=True,text=True,timeout=20)
    assert process.returncode==0,process.stderr
    replies=[json.loads(line) for line in process.stdout.splitlines()]
    assert len(replies)==len(vectors),(len(replies),len(vectors))
    for (name,message,ok,code),reply in zip(vectors,replies):check(name,message,reply,ok,code)
    # These cannot be represented faithfully as Python dicts; exercise raw JSON.
    raw_session=json.dumps(sample(),separators=(',',':'))
    duplicate=raw_session.replace('"schemaVersion":2','"schemaVersion":2,"schemaVersion":2',1)
    raw_request='{"session":'+duplicate+'}\n'
    checked=subprocess.run([args.runner],input=raw_request,capture_output=True,text=True,timeout=5)
    assert checked.returncode==0 and json.loads(checked.stdout)['ok'] is False
    print(json.dumps(dict(passed=len(vectors),runner=args.runner,exitCode=process.returncode)))
