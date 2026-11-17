"""Packet-clock production fixtures. Counts are independent test oracles only."""
import os
from progressive_opus_geometry_preflight import PROFILES, fixture
from progressive_simple_vfr_preflight import decorate
from progressive_webm_opus_remux_preflight import probe,configuration,children
from progressive_webm_remux_preflight import scan
from progressive_webm_vfr_preflight import ebml,unsigned
from progressive_webm_fixtures import width
from progressive_webm_remux_preflight import ok
from progressive_webm_derived_av_fixtures import CASES as OLD_CASES, media as old_media

CASES = [(p[0]+('-unknown' if u else '-finite'), p, u)
         for p in PROFILES for u in (False, True)]
EXTRA = [('long-zero-trim','120',2),('short-packet-audio','2.5',1),('mixed-short','mixed-short',2)]
CASES += [(p[0]+('-unknown' if u else '-finite'),p,u) for p in EXTRA for u in (False,True)]
CASES += [(name, p, u) for name, p, u in OLD_CASES
          if p[0] in ('stereo-short', 'p2-stereo-zero-trim', 'mono-649-audio-first', 'vfr-mono')]
if os.environ.get('FOXICMP_PACKET_CASES'):
    selected = os.environ['FOXICMP_PACKET_CASES'].split(',')
    assert set(selected) <= {c[0] for c in CASES}, selected
    CASES = [c for c in CASES if c[0] in selected]


def early_audio(raw):
    # A controlled minimal fixture: make all mandatory audio bytes available in
    # the first video Cluster. Codec packets/clocks are unchanged; do this before
    # source hashes/oracles. Late audio availability is tested separately.
    layout=scan(raw);audio=configuration(raw)[2]['number'];sound=[];clusters=[]
    for c in layout['clusters']:
        video=[]
        for tag,_,a,z in children(raw,c['body'],c['end']):
            if tag not in (0xa3,0xa0):continue
            fields=list(children(raw,a,z)) if tag==0xa0 else []
            block=next(raw[x:y] for key,_,x,y in fields if key==0xa1) if fields else raw[a:z]
            w=width(block[0]);track=int.from_bytes(block[:w],'big') & ((1<<(7*w))-1)
            if track!=audio:video.append(ebml(tag,raw[a:z]));continue
            ticks=c['timestamp']+int.from_bytes(block[w:w+2],'big',signed=True)
            assert 0<=ticks<1000
            block=block[:w]+ticks.to_bytes(2,'big',signed=True)+block[w+2:]
            body=block if tag==0xa3 else b''.join(ebml(key,block if key==0xa1 else raw[x:y]) for key,_,x,y in fields if key!=0xbf)
            sound.append(ebml(tag,body))
        clusters.append((c['timestamp'],video))
    assert clusters[0][0]==0
    out=bytearray(raw[:layout['headBytes']])
    for i,(ticks,video) in enumerate(clusters):
        if video:out+=ebml(0x1f43b675,unsigned(0xe7,ticks)+(b''.join(sound) if i==0 else b'')+b''.join(video))
    return bytes(out)


def media(folder, case):
    name, p, unknown = case
    if p[0] not in {x[0] for x in PROFILES+EXTRA}:return old_media(folder, case)
    folder.mkdir(parents=True, exist_ok=True);video = folder/'video.webm'
    ok(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i',
        'testsrc2=size=160x90:rate=25:duration=24', '-an', '-c:v', 'libvpx-vp9',
        '-threads', '2', '-pix_fmt', 'yuv420p', '-g', '25', '-cpu-used', '8',
        '-lag-in-frames', '0', '-b:v', '300k', '-live', '1', video], folder, 'encode-video')
    samples = 1151688 if p[0]=='long-zero-trim' else 649 if p[0]=='short-packet-audio' else 1151983
    source = fixture(folder, p, video, samples=samples, seconds=24, audio_first=p[0]=='short-packet-audio')
    raw=source.read_bytes()
    if p[0]=='short-packet-audio':raw=early_audio(raw)
    raw, _ = decorate(raw, unknown);source = folder/'packet-opus.webm';source.write_bytes(raw)
    profile = (name, False, p[2], p[1], 'audio', samples, p[0]=='short-packet-audio')
    return source, 'yuv420p', 600, 240000000, probe(source, folder, 'source-probe'), profile
