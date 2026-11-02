"""Real LAME-tagged MP3, audible timeline, no modifications to the encoder tag."""
import hashlib,json,subprocess,uuid
from pathlib import Path
from progressive_mp3_lame_fixture import encode
from progressive_mp3_tag_fixture import crc,metadata
from progressive_mp3_fixture import frames,prefix_at_seconds

PROFILES=tuple('audio-mp3-gapless-'+c+'-'+str(r) for r in (44100,48000) for c in ('mono','stereo'))
def fixture(root,profile,seconds=20):
    root=Path(root);rate=int(profile.split('-')[-1]);channels=2 if '-stereo-' in profile else 1
    original,raw,version=encode(root,rate,channels,seconds)
    header,cursor=metadata(raw);assert cursor is not None and crc(header[:cursor+34])==int.from_bytes(header[cursor+34:cursor+36],'big')
    delay=(header[cursor+21]<<4)|(header[cursor+22]>>4);padding=((header[cursor+22]&15)<<8)|header[cursor+23]
    count=len(frames(raw))-1;audible=count*1152-delay-padding;assert audible==seconds*rate
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-of','json',str(original)]))
    pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(original),'-c:a','pcm_s16le','-f','s16le','-']);assert len(pcm)==audible*channels*2
    probe.update(_pcm_sha256=hashlib.sha256(pcm).hexdigest(),_pcm_bytes=len(pcm),_gapless=dict(begin=delay+529,end=count*1152-padding+529,audibleSamples=audible,encoderVersion=version))
    model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(audible*10000000//rate),timestampOffset='0'),resources=[dict(id='media',name='mux.part',length=None if channels==1 else str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id='main',kind='audio',required=True,codecHint='mp3',containerHint='mp3-gapless',initializationReady=True,state='downloading',source=dict(resourceId='media'))],metadata=dict(revision='1',title='Gapless '+profile,chapters=[dict(id='c'+str(t),time=str(t*10000000),title=str(t)) for t in ((0,2,17) if seconds>=18 else (0,2))]))
    return model,raw,original,probe

if __name__=='__main__':
    import argparse
    from progressive_standalone_fixture import publish
    p=argparse.ArgumentParser();p.add_argument('--root',required=True);p.add_argument('--profile',choices=PROFILES,required=True);a=p.parse_args()
    root=Path(a.root).resolve();model,raw,original,probe=fixture(root,a.profile,4)
    initial=publish(model,raw,root,prefix_at_seconds(raw,probe,2));full=publish(model,raw,root,len(raw),previous=initial);complete=publish(model,raw,root,len(raw),previous=full,complete=True)
    for name,v in [('initial',initial),('full',full),('complete',complete)]:
        v['sessionDirectory']=str(root);(root/(name+'.json')).write_text(json.dumps(v),encoding='utf-8')
    pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(original),'-c:a','pcm_s16le','-f','s16le','-']);(root/'native.pcm').write_bytes(pcm)
