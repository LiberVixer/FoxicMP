"""Player SDK: SHA-256 for confirmed local ranges; no downloader integration.

Call after writing/flushing the bytes. Keep the returned snapshot and supply it
as previous on the next call: published block boundaries/digests are immutable.
Old snapshots without integrity stay supported by the player.
"""
from __future__ import annotations
import copy
import hashlib
import os
from pathlib import Path
import stat
from progressive_schema import unsigned64

MAX_BLOCK_BYTES=4*1024*1024
MAX_BLOCKS=4096


def protect_snapshot(snapshot: dict, directory, *, previous: dict | None=None,
                     block_bytes: int=262144) -> dict:
    if type(block_bytes) is not int or not 1<=block_bytes<=MAX_BLOCK_BYTES:
        raise ValueError('Integrity block size must be 1..4194304 bytes')
    result=copy.deepcopy(snapshot)
    if result.get('schemaVersion')!=2:raise ValueError('Integrity requires schema 2')
    if previous is not None and any(result[k]!=previous[k] for k in ('sessionId','generation')):
        raise ValueError('Previous integrity belongs to another session')
    old={r['id']:r for r in previous['resources']} if previous is not None else {}
    root=Path(directory).resolve(strict=True)
    def decimal(text):
        if not isinstance(text,str) or not text.isascii() or not text.isdecimal():raise ValueError('Expected an exact decimal string')
        value=int(text)
        if unsigned64(value,byte_offset=True)!=text:raise ValueError('Non-canonical byte count')
        return value
    for resource in result['resources']:
        prior=old.get(resource['id'])
        if prior is not None and (prior['name']!=resource['name'] or 'integrity' not in prior):
            raise ValueError('Cannot enable integrity or change resource identity mid-session')
        kept=copy.deepcopy(prior['integrity']['blocks']) if prior is not None else []
        if prior is not None and prior['integrity']['algorithm']!='sha256':raise ValueError('Unsupported integrity algorithm')
        name=resource['name']
        if not isinstance(name,str) or not name or name in ('.','..') or any(c in name for c in '/\\:\x00'):
            raise ValueError('Expected a plain local resource filename')
        ranges=[];last_end=0
        for item in resource['ranges']:
            start,count=decimal(item['offset']),decimal(item['length'])
            if not count or start<last_end or start+count>2**63-1:raise ValueError('Invalid confirmed ranges')
            ranges.append((start,start+count));last_end=start+count
        # Empty late resources need neither a file nor an open handle yet.
        blocks=[];position=0;stream=None
        try:
            for start,end in ranges:
                at=start
                while at<end:
                    if position<len(kept) and decimal(kept[position]['offset'])<at:
                        raise ValueError('Published integrity bytes were withdrawn')
                    if position<len(kept) and decimal(kept[position]['offset'])==at:
                        block=kept[position];length=decimal(block['length'])
                        if not length or at+length>end:raise ValueError('Published integrity block was withdrawn')
                        blocks.append(block);position+=1;at+=length;continue
                    stop=min(end,at+block_bytes,decimal(kept[position]['offset']) if position<len(kept) else end)
                    if stream is None:
                        path=root/name;info=path.lstat()
                        if not stat.S_ISREG(info.st_mode) or info.st_nlink!=1 or getattr(info,'st_file_attributes',0)&0x400:
                            raise ValueError('Integrity source must be a regular file without links')
                        fd=os.open(path,os.O_RDONLY|getattr(os,'O_BINARY',0)|getattr(os,'O_NOFOLLOW',0)|getattr(os,'O_NONBLOCK',0))
                        stream=os.fdopen(fd,'rb')
                        pinned=os.fstat(stream.fileno())
                        if not stat.S_ISREG(pinned.st_mode) or (info.st_dev,info.st_ino)!=(pinned.st_dev,pinned.st_ino):
                            raise ValueError('Integrity source changed while opening')
                    stream.seek(at);hash_value=hashlib.sha256();remaining=stop-at
                    while remaining:
                        data=stream.read(min(65536,remaining))
                        if not data:raise ValueError('Confirmed integrity bytes have not been written')
                        hash_value.update(data);remaining-=len(data)
                    blocks.append(dict(offset=str(at),length=str(stop-at),sha256=hash_value.hexdigest()));at=stop
                    if len(blocks)>MAX_BLOCKS:raise ValueError('Too many integrity blocks')
            if position!=len(kept):raise ValueError('Published integrity blocks were withdrawn')
            resource['integrity']=dict(algorithm='sha256',blocks=blocks)
        finally:
            if stream is not None:stream.close()
    return result
