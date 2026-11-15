// Fixed-clock admission/rounding boundaries, independent known timestamps.
#include "../src/DSUtil/HarvesterWebMContinuousClock.h"
#include <iostream>
#include <stdexcept>
#include <climits>
static void check(bool ok){if(!ok)throw std::runtime_error("WebM clock boundary failed");}
int main()try{
 harvester::WebmContinuousClock old(false),ntsc(true);
 check(old.Frames(240000000)==600 && old.Frames(240240000)==0);
 check(ntsc.Frames(240240000)==720 && ntsc.Frames(240000000)==0);
 check(ntsc.Frames(333666)==1 && ntsc.Frames(333667)==1 && ntsc.Frames(333665)==0 && ntsc.Frames(333668)==0);
 check(ntsc.End100ns(720)==240240000 && ntsc.End100ns(1)==333666);
 const uint64_t ms[]={0,33,67,100,133,167,200,234,267,300,334,367,400,434,467,501};
 for(unsigned i=0;i<sizeof(ms)/sizeof(*ms);++i){check(ntsc.Packet(ms[i],i));check(!ntsc.Packet(ms[i]+2,i));}
 check(ntsc.Packet(23991,719) && !ntsc.Packet(23990,719));
 check(old.Packet(40,1) && !old.Packet(33,1));
 check(ntsc.DefaultDuration(33366666) && ntsc.DefaultDuration(33366667) && !ntsc.DefaultDuration(40000000));
 check(old.DefaultDuration(40000000) && !old.DefaultDuration(33366666));
 check(!ntsc.Packet(UINT64_MAX,1) && !ntsc.Packet(1,UINT64_MAX));
 check(!ntsc.Frames(INT64_MAX) && !ntsc.Frames(-1) && !ntsc.Frames(0));
 const unsigned rates[][2]={{24000,1001},{24,1},{25,1},{30000,1001},{30,1},{50,1},{60000,1001},{60,1}};
 for(const auto& rate:rates){
  harvester::WebmContinuousClock c(rate[0],rate[1]);check(c.Valid());
  for(uint64_t frame: {0ULL,1ULL,2ULL,599ULL,1438ULL,49999ULL}){
   const auto ms=(frame*rate[1]*1000+rate[0]/2)/rate[0];
   check(c.Packet(ms,frame));check(!c.Packet(ms+2,frame));
   if(frame){const auto duration=c.End100ns(frame);check(c.Frames(duration)==frame && c.Frames(duration-2)==0);}
  }
  const auto lower=uint64_t(1000000000)*rate[1]/rate[0];
  check(c.DefaultDuration(lower) && c.DefaultDuration(lower-1) && !c.DefaultDuration(lower-2));
  check(!c.Packet(UINT64_MAX,1) && !c.Packet(1,UINT64_MAX) && !c.Frames(INT64_MAX));
 }
 check(!harvester::WebmContinuousClock(48000,2002).Valid());
 auto vfr=harvester::WebmContinuousClock::Vfr(733,240000000);
 check(vfr.Valid() && vfr.Frames(240000000)==733 && vfr.Frames(240000001)==0);
 check(vfr.DefaultDuration(0) && vfr.DefaultDuration(20000000) && !vfr.DefaultDuration(1));
 check(!vfr.Packet(40,1) && vfr.End100ns(1)==0);
 check(!harvester::WebmContinuousClock::Vfr(0,240000000).Valid());
 check(!harvester::WebmContinuousClock::Vfr(50001,240000000).Valid());
 check(!harvester::WebmContinuousClock::Vfr(733,240000001).Valid());
 check(!harvester::WebmContinuousClock::Vfr(733,UINT64_MAX).Valid());
 auto derived=harvester::WebmContinuousClock::VfrAutoCount(240000000);
 check(derived.Valid() && derived.variable && derived.derivedCount && derived.expectedFrames==0 && derived.Frames(240000000)==0);
 check(!harvester::WebmContinuousClock::VfrAutoCount(0).Valid() && !harvester::WebmContinuousClock::VfrAutoCount(240000001).Valid());
 derived.expectedFrames=1;check(!derived.Valid());derived.expectedFrames=0;derived.variable=false;check(!derived.Valid());
 std::cout<<"{\"result\":\"PASS\",\"ntscOriginAndRounding\":true,\"old25fpsUnchanged\":true,\"boundedArithmetic\":true}"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
