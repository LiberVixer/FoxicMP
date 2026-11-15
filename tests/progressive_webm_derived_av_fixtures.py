"""Production derived A/V count fixtures; counts below are test oracles only."""
from fractions import Fraction
from progressive_audio_count_preflight import PROFILES
from progressive_simple_vfr_preflight import head_without_default,decorate
from progressive_webm_opus_remux_preflight import fixture,probe,packets
from progressive_webm_remux_preflight import scan
from progressive_webm_vfr_preflight import fixture as vfr_fixture,PROFILES as VFR_PROFILES
CASES=[(p[0]+('-unknown' if unknown else '-finite'),p,unknown) for p in PROFILES for unknown in (False,True)]
def media(folder,case):
 name,p,unknown=case
 original,pixels=vfr_fixture(folder,VFR_PROFILES[0]) if p[0]=='vfr-mono' else fixture(folder,p)
 meta=probe(original,folder,'source-probe');video=packets(meta,'video');duration=scan(original.read_bytes())['defaultDurationNs']
 end=int(Fraction(video[-1][1])*10000000)+round(Fraction(duration,1000000))*10000
 raw,_=decorate(head_without_default(original.read_bytes()),unknown)
 source=folder/'derived-av.webm';source.write_bytes(raw);meta=probe(source,folder,'derived-probe')
 profile=(name,False,p[2],p[3],p[4],p[5],p[6])
 return source,pixels,len(video),end,meta,profile
