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
        if prior is not None and set(prior['integrity'])!={'algorithm','blocks'}:
            raise ValueError('Retain the inline snapshot when hashing appends; externalize only the wire copy')
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


def externalize_integrity(snapshot: dict, directory) -> dict:
    """Export immutable authenticated LE block tables; keep inline state for hashing appends.

    Retain these sidecars until the reader's filesClosed receipt. No downloader
    or network operations are performed; existing files are never overwritten.
    """
    import struct
    result=copy.deepcopy(snapshot);root=Path(directory).resolve(strict=True)
    for resource in result['resources']:
        integrity=resource.get('integrity',{})
        if integrity.get('algorithm')!='sha256' or set(integrity)!={'algorithm','blocks'}:
            raise ValueError('Externalize an inline SHA-256 snapshot')
        blocks=integrity['blocks']
        if not isinstance(blocks,list) or len(blocks)>MAX_BLOCKS:raise ValueError('Too many integrity records')
        payload=bytearray()
        for block in blocks:
            at=int(unsigned64(int(block['offset']),byte_offset=True));length=int(unsigned64(int(block['length']),byte_offset=True,positive=True))
            if str(at)!=block['offset'] or str(length)!=block['length'] or length>MAX_BLOCK_BYTES:raise ValueError('Invalid integrity block bounds')
            digest=block['sha256']
            if not isinstance(digest,str) or len(digest)!=64 or any(c not in '0123456789abcdef' for c in digest):raise ValueError('Invalid SHA-256')
            payload+=struct.pack('<QI',at,length)+bytes.fromhex(digest)
        digest=hashlib.sha256(payload).hexdigest();name='integrity-'+digest+'.idx';path=root/name
        flags=os.O_WRONLY|os.O_CREAT|os.O_EXCL|getattr(os,'O_BINARY',0)|getattr(os,'O_NOFOLLOW',0)
        try:fd=os.open(path,flags,0o600)
        except FileExistsError:
            st=path.lstat()
            if not stat.S_ISREG(st.st_mode) or st.st_nlink!=1 or getattr(st,'st_file_attributes',0)&0x400 or path.read_bytes()!=payload:
                raise ValueError('Existing immutable integrity index differs or is unsafe')
        else:
            try:
                with os.fdopen(fd,'wb') as out:out.write(payload);out.flush();os.fsync(out.fileno())
            except BaseException:
                path.unlink(missing_ok=True);raise
        resource['integrity']=dict(algorithm='sha256',index=dict(format='sha256-blocks-le-v1',name=name,bytes=str(len(payload)),sha256=digest))
    return result
