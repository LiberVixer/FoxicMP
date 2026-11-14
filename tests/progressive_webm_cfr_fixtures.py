"""Canonical CFR fixtures and independent exact rational timeline arithmetic."""
from fractions import Fraction
from progressive_webm_opus_remux_preflight import PROFILES,fixture,probe
RATES=((24000,1001),(24,1),(25,1),(30000,1001),(30,1),(50,1),(60000,1001),(60,1))
CASES=[(f'{n}-{d}',n,d,PROFILES[1] if n==60000 else PROFILES[4] if n==24 else PROFILES[0]) for n,d in RATES]
def media(folder,case):
 name,n,d,profile=case;original,pixels=fixture(folder,profile,rate=f'{n}/{d}');metadata=probe(original,folder,'cfr-probe');stream=next(s for s in metadata['streams'] if s['codec_type']=='video');frames=[p for p in metadata['packets'] if p['stream_index']==stream['index']]
 count=len(frames);expected=(24*n+d-1)//d;assert count==expected,(case,count,expected)
 clock=Fraction(stream['time_base']);assert clock==Fraction(1,1000)
 deviations=[int(p['pts'])*clock-Fraction(i*d,n) for i,p in enumerate(frames)];assert max(abs(v) for v in deviations)<=Fraction(1,2000),(case,min(deviations),max(deviations))
 ticks=Fraction(count*d*10000000,n);duration=ticks.numerator//ticks.denominator
 return original,pixels,count,duration,metadata
