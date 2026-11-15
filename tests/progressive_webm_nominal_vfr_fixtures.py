"""VFR SimpleBlocks with an explicit bounded nominal DefaultDuration hint."""
from fractions import Fraction
from progressive_webm_vfr_preflight import PROFILES,fixture
from progressive_webm_opus_remux_preflight import probe,packets
from progressive_webm_remux_preflight import scan
from progressive_simple_vfr_preflight import head_without_default,decorate
from progressive_simple_vfr_read_budget_test import with_default
CASES=[(p[0]+('-unknown' if unknown else '-finite'),p,unknown) for p in PROFILES for unknown in (False,True)]
def media(folder,case,nominal_duration=None):
 name,p,unknown=case;original,pixels=fixture(folder,p);meta=probe(original,folder,'source-probe');video=packets(meta,'video');duration=scan(original.read_bytes())['defaultDurationNs']
 end=int(Fraction(video[-1][1])*10000000)+round(Fraction(duration,1000000))*10000
 raw,_=decorate(with_default(head_without_default(original.read_bytes()),duration if nominal_duration is None else nominal_duration),unknown)
 source=folder/'nominal-vfr.webm';source.write_bytes(raw);meta=probe(source,folder,'nominal-probe');profile=(name,False,p[3],'20','audio',1152000,False)
 return source,pixels,len(video),end,meta,profile
