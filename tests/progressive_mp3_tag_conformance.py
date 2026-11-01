"""Independent CRC oracle, real FFmpeg/LAME tags and production trim diagnostics."""
import argparse
import copy
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

from progressive_mp3_indexed_fixture import fixture, frames, PROFILES
from progressive_mp3_lame_fixture import encode
from progressive_standalone_fixture import publish
from progressive_mux_ts_conformance import Rpc
from progressive_mp3_tag_fixture import crc,metadata

# Table implementation independent of the production bit-at-a-time loop.
def run(runner, tag_runner):
    assert crc(b'123456789')==0xbb3d
    checks=[];profiles=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-mp3-tag-') as temp:
        root=Path(temp)
        for profile in list(PROFILES)+['lame-'+str(r)+'-'+str(c) for r in (44100,48000) for c in (1,2)]:
            folder=root/profile;model,raw,original,probe=fixture(folder,PROFILES[0] if profile.startswith('lame-') else profile,4)
            encoder_version='FFmpeg'
            if profile.startswith('lame-'):
                _,r,c=profile.split('-');original,raw,encoder_version=encode(folder/'encoder',int(r),int(c))
                count=len(frames(raw))-1;model['timeline']['duration']=str(count*1152*10000000//int(r)+1)
                model['resources'][0]['length']=str(len(raw))
            header,cursor=metadata(raw);rate=48000 if header[2]&4 else 44100;channels=1 if header[3]>>6==3 else 2
            known=cursor is not None
            stored=int.from_bytes(header[cursor+34:cursor+36],'big') if known else None
            calculated=crc(header[:cursor+34]) if known else None
            valid=known and stored==calculated
            data=folder/'data';data.mkdir();rpc=Rpc([runner,'--registry',str(folder/'registry')])
            try:
                # Header is confirmed but the physical resource is not complete.
                prefix=frames(raw)[min(40,len(frames(raw))-1)][0]
                value=publish(model,raw,data,prefix);assert rpc('open',session=value,directory=str(data))['ok']
                index=rpc('status')['mp3Index'];tag=index['encoderTag'];plan=index['trimPlan']
                assert index['gaplessTrimApplied'] is False and index['timeline']=='encoded-samples'
                assert index['encoderTagCrcVerified']==valid and tag['crcVerified']==valid
                assert tag['musicCrcVerified'] is False and plan['analysisOnly'] is True and plan['ready']==valid
                if known:
                    assert tag['state']==('valid' if valid else 'crc-mismatch') and tag['crcChecked'] and tag['storedCrc16']==stored and tag['calculatedCrc16']==calculated and tag['coveredBytes']==cursor+34
                else:assert tag['state']=='absent' and not tag['crcChecked'] and plan['reasonCode']=='encoder-delay-unavailable'
                checks.append(profile+'-independent-crc-and-production-status')
                value=publish(model,raw,data,len(raw),previous=value,complete=True);assert rpc('publish',session=value)['ok']
                assert rpc('status')['mp3Index']==index
                checks.append(profile+'-plan-stable-across-growing-complete')
                if valid:
                    size=int(rpc('layout')['tracks'][0]['available']);projected=bytearray()
                    for offset in range(0,size,65536):
                        answer=rpc('read',decoder=True,track=0,offset=str(offset),length=str(min(65536,size-offset)))
                        projected+=bytes.fromhex(answer['hex'])
                    path=folder/'projection.mp4';path.write_bytes(projected)
                    def pcm(path):return subprocess.check_output(['ffmpeg','-v','error','-i',str(path),'-c:a','pcm_s16le','-f','s16le','-'])
                    coded,native=pcm(path),pcm(original);begin,end=int(plan['beginSample']),int(plan['endSample'])
                    assert len(native)==int(plan['audibleSamples'])*channels*2
                    assert coded[begin*channels*2:end*channels*2]==native
                    assert int(plan['audibleDuration100ns'])==int(plan['audibleSamples'])*10000000//rate
                    checks.append(profile+'-validated-window-native-pcm-exact')
                rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
                assert original.read_bytes()==raw
                checks.append(profile+'-reader-released-original-preserved')
                profiles.append(dict(profile=profile,encoderVersion=encoder_version,sampleRate=rate,channels=channels,fixtureSha256=hashlib.sha256(raw).hexdigest(),index=index))
            finally:rpc.close()

        folder=root/'errors';model,raw,_,_=fixture(folder,PROFILES[1],4);header,cursor=metadata(raw)
        native=Rpc([tag_runner]);bridge=Rpc([runner,'--registry',str(folder/'registry')])
        try:
            good=native('parse',hex=header.hex());assert good['ok'] and good['crcVerified'] and good['ready']
            def scalar(name,body,expected_state,ready=False,**options):
                result=native('parse',hex=bytes(body).hex(),**options)
                assert result['ok'] and result['state']==expected_state and result['ready']==ready,(name,result)
                checks.append(name);return result
            def repaired(body):
                body=bytearray(body);body[cursor+34:cursor+36]=crc(body[:cursor+34]).to_bytes(2,'big');return body
            def damaged(name,offset):
                b=bytearray(header);b[offset]^=1;result=scalar(name,b,'crc-mismatch')
                assert result['crcChecked'] and not result['crcVerified']
                # Corrupt optional tag metadata must not break the encoded profile.
                mutated=raw[:frames(raw)[0][0]]+bytes(b)+raw[frames(raw)[0][0]+len(header):]
                data=folder/name;data.mkdir();value=publish(copy.deepcopy(model),mutated,data,len(mutated),complete=True)
                assert bridge('open',session=value,directory=str(data))['ok']
                index=bridge('status')['mp3Index'];assert not index['encoderTagCrcVerified'] and not index['trimPlan']['ready'] and not index['gaplessTrimApplied']
                bridge('release');assert not bridge('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
            for name,at in [('delay-bit-corruption',cursor+22),('padding-bit-corruption',cursor+23),('music-crc-bit-corruption',cursor+32),('tag-crc-bit-corruption',cursor+34),('quality-bit-corruption',cursor-1)]:damaged(name,at)
            b=bytearray(header);b[cursor:cursor+9]=b'Other1.00';scalar('unknown-encoder-not-trusted',repaired(b),'unsupported-encoder')
            b=bytearray(header);b[cursor+9]|=16;scalar('unknown-revision-not-trusted',repaired(b),'unsupported-revision')
            b=bytearray(header);b[cursor+22]&=0xf0;b[cursor+23]=1;result=scalar('padding-less-than-decoder-delay',repaired(b),'valid');assert result['reasonCode']=='unproven-trim-window'
            for delay in (0,528,530):
                result=scalar('decoder-delay-'+str(delay)+'-not-guessed',header,'valid',decoderDelay=delay);assert result['reasonCode']=='unproven-decoder-delay'
            result=scalar('unknown-sample-rate-not-guessed',header,'valid',planRate=32000);assert result['reasonCode']=='unsupported-sample-rate'
            b=bytearray(header);b[-1]^=1;scalar('outside-tag-crc-is-not-music-integrity',b,'valid',True)
            b=bytearray(header);b[2]=(b[2]&15)|0x40;n=144000*56//44100+((b[2]>>1)&1);b=b[:n]
            scalar('truncated-full-tag-is-not-ready',b,'truncated')
            # A stored zero CRC is valid when the independently calculated CRC is zero.
            b=bytearray(header);initial=crc(b[:cursor+32]);pair=None
            for n in range(65536):
                if crc(n.to_bytes(2,'big'),initial)==0:pair=n.to_bytes(2,'big');break
            assert pair is not None;b[cursor+32:cursor+34]=pair;b[cursor+34:cursor+36]=b'\0\0'
            result=scalar('zero-crc-can-be-valid',b,'valid',True);assert result['storedCrc16']==result['calculatedCrc16']==0
            b=bytearray(header);b[cursor:cursor+9]=b'Other1.00'
            result=scalar('reparse-clears-prior-validation',b,'unsupported-encoder',previousHex=header.hex());assert not result['crcChecked'] and result['storedCrc16']==0
        finally:native.close();bridge.close()
    return dict(result='PASS',count=len(checks),checks=checks,profiles=profiles,analysisOnly=True,gaplessPlaybackImplemented=False,harvesterModified=False)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);p.add_argument('--tag-runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner,a.tag_runner)))
