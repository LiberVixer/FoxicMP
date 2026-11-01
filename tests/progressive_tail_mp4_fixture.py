"""Tail-moov MP4 with a sparse physical file and independently confirmed metadata."""
import copy
from pathlib import Path
from progressive_indexed_mp4_fixture import fixture as indexed_fixture, prefix_at, boxes
from progressive_integrity import protect_snapshot


def fixture(root, profile='main-bframes', seconds=20):
    model,raw,original,groups,scales=indexed_fixture(root,profile,seconds,faststart=False)
    for track in model['tracks']:track['containerHint']='mp4-tail'
    model['metadata']['title']='Tail-index MP4'
    assert metadata_start(raw)<len(raw)
    return model,raw,original,groups,scales


def metadata_start(raw):
    return next(at+size for at,size,kind,_ in boxes(raw) if kind==b'mdat')


def publish(model,raw,directory,prefix,*,previous=None,complete=False,tail_bytes=None):
    value=copy.deepcopy(model)
    if previous is not None:value['revision']=str(int(previous['revision'])+1)
    resource=value['resources'][0];start=metadata_start(raw)
    if tail_bytes is None:tail_bytes=len(raw)-start
    assert 0<=prefix<=len(raw) and 0<=tail_bytes<=len(raw)-start
    if previous:
        prior=previous['resources'][0]['ranges']
        old=int(prior[0]['length']) if prior and prior[0]['offset']=='0' else 0
        assert prefix>=old
        for r in prior:
            if int(r['offset'])>=start:assert start+tail_bytes>=int(r['offset'])+int(r['length'])
    path=Path(directory)/resource['name']
    with path.open('r+b' if path.exists() else 'w+b') as stream:
        stream.truncate(len(raw))
        stream.seek(0);stream.write(raw[:prefix])
        if tail_bytes:stream.seek(start);stream.write(raw[start:start+tail_bytes])
        stream.flush()
    end=max(prefix,start+tail_bytes) if prefix>=start else prefix
    resource['ranges']=([dict(offset='0',length=str(end))] if end else [])
    if prefix<start and tail_bytes:resource['ranges'].append(dict(offset=str(start),length=str(tail_bytes)))
    if complete:
        assert end==len(raw)
        resource.update(length=str(len(raw)),state='complete');value['writer']['state']='complete'
        for track in value['tracks']:track['state']='complete'
    return protect_snapshot(value,directory,previous=previous)
