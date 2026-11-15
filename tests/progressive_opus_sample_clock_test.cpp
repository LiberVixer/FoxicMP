// Production Opus count arithmetic/framing/terminal guards; no decoder or GUI.
#include "../src/DSUtil/HarvesterOpusSampleClock.h"
#include <iostream>
static void check(bool b){if(!b)throw std::runtime_error("Opus clock check failed");}
template<class F>static void reject(F fn){try{fn();}catch(const std::exception&){return;}throw std::runtime_error("Invalid Opus clock admitted");}
int main()try{
 const uint8_t dtx[]={0xf8},ten[]={0xf0},broken[]={0xfb,0};
 harvester::OpusSampleClock c;reject([&]{c.Finish();});reject([&]{c.Add(1,dtx,1,0);});
 c.Add(0,dtx,1,0);check(c.Samples()==648 && !c.Terminal());reject([&]{c.Finish();});
 auto original=c;reject([&]{c.Add(0,dtx,1,0);});reject([&]{c.Add(20,ten,1,0);});reject([&]{c.Add(20,broken,2,0);});
 for(auto padding:{int64_t(-1),int64_t(1),int64_t(20000000)})reject([&]{c.Add(20,dtx,1,padding);});
 c.Add(21,dtx,1,19979167);check(c.Packets()==2 && c.Samples()==649 && c.Terminal());c.Finish();reject([&]{c.Add(40,dtx,1,0);});
 auto floor=original;floor.Add(20,dtx,1,19979166);check(floor.Samples()==649);floor.Finish();
 harvester::OpusSampleClock full;for(unsigned i=0;i<50000;++i)full.Add(i*20,dtx,1,0);full.Finish();check(full.Packets()==50000 && full.Samples()==47999688);reject([&]{full.Add(1000000,dtx,1,0);});
 std::cout<<"{\"result\":\"PASS\",\"derivedOpusClock\":true,\"boundedPackets\":true}"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
