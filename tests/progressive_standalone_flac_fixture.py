"""FLAC fMP4 with an independent integer PCM oracle, never a downloader."""
import array,hashlib,json,subprocess,sys,uuid
from fractions import Fraction
from pathlib import Path

def fixture(root,profile,seconds=20):
    root=Path(root);root.mkdir(parents=True,exist_ok=True);_,_,layout,bits,rate=profile.split('-');bits=int(bits);rate=int(rate);channels=2 if layout=='stereo' else 1
    pcm=array.array('h' if bits==16 else 'i');maximum=1<<(bits-1)
    for i in range(seconds*rate):
        for c in range(channels):pcm.append((((i*(137+c*74))%(maximum*2))-maximum)<<(8 if bits==24 else 0))
    if sys.byteorder!='little':pcm.byteswap()
    data=pcm.tobytes();source=root/'input.pcm';source.write_bytes(data);fmt='s16le' if bits==16 else 's32le';original=root/'original.mp4'
    subprocess.run(['ffmpeg','-v','error','-y','-f',fmt,'-ar',str(rate),'-ac',str(channels),'-i',str(source),'-vn','-c:a','flac','-sample_fmt','s16' if bits==16 else 's32','-bits_per_raw_sample',str(bits),'-compression_level','5','-strict','-2','-frag_duration','1000000','-movflags','+empty_moov+default_base_moof+frag_keyframe',str(original)],check=True,capture_output=True)
    raw=original.read_bytes();probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(original)]));assert len(probe['streams'])==1
    stream=probe['streams'][0];assert stream['codec_name']=='flac' and int(stream['bits_per_raw_sample'])==bits and int(stream['sample_rate'])==rate and int(stream['channels'])==channels
    probe.update(_pcm_sha256=hashlib.sha256(data).hexdigest(),_pcm_bytes=len(data),_pcm_format=fmt)
    frames=json.loads(subprocess.check_output(['ffprobe','-v','error','-select_streams','a:0','-show_frames','-of','json',str(original)]))['frames'];assert len(frames)==len(probe['packets'])
    for packet,frame in zip(probe['packets'],frames):
        assert packet['pos']==frame['pkt_pos'];packet.setdefault('duration',frame['nb_samples'])
    clock=Fraction(stream['time_base']);duration=max((int(p['pts'])+int(p['duration']))*clock for p in probe['packets'])
    model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(int(duration*10000000)+1),timestampOffset='0'),resources=[dict(id='media',name='mux.part',length=None if channels==1 else str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id='main',kind='audio',required=True,codecHint='flac',containerHint='fmp4',initializationReady=True,state='downloading',source=dict(resourceId='media'))],metadata=dict(revision='1',title='Standalone '+profile,chapters=[dict(id='c'+str(t),time=str(t*10000000),title=str(t)) for t in ((0,2,17) if seconds>=18 else (0,2))]))
    return model,raw,original,probe
