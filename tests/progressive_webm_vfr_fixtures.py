"""Canonical explicit-duration VFR fixtures; original packets/clocks retained."""
from progressive_webm_vfr_preflight import PROFILES,fixture,explicit_durations
from progressive_webm_opus_remux_preflight import probe,packets
CASES=[(p[0]+('-no-default' if missing else '-default'),p,missing) for p in PROFILES for missing in (False,True)]
def media(folder,case):
 name,p,missing=case;source,pixels=fixture(folder,p);raw,durations=explicit_durations(source.read_bytes(),remove_default=missing);source=folder/'vfr.webm';source.write_bytes(raw);meta=probe(source,folder,'vfr-probe');video=packets(meta,'video');from fractions import Fraction
 end=int(Fraction(video[-1][1])*10000000)+durations[-1]*10000
 assert len(video)==len(durations) and all(int((Fraction(b[1])-Fraction(a[1]))*1000)==duration for a,b,duration in zip(video,video[1:],durations))
 legacy=(name,p[2],p[3],'20','audio',1152000,False)
 return source,pixels,len(video),end,meta,legacy
