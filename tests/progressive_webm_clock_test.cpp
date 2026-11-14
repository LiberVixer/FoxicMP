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
 std::cout<<"{\"result\":\"PASS\",\"ntscOriginAndRounding\":true,\"old25fpsUnchanged\":true,\"boundedArithmetic\":true}"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
