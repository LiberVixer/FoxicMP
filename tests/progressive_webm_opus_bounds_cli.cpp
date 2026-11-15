// Structural parser bounds/fuzz driver; no media decoder, registry or GUI.
#include "../src/DSUtil/HarvesterWebMOpusContinuous.h"
#include <fstream>
#include <iostream>
#include <random>
static bool parse(const std::vector<uint8_t>& bytes,uint64_t samples,bool ntsc=false,harvester::WebmContinuousClock clock={},uint64_t frames=0,bool simple=false){
 size_t at=0;try{harvester::ContinuousWebmOpus input([&](uint8_t* out,size_t n){const auto count=(std::min)(n,bytes.size()-at);if(count)std::memcpy(out,bytes.data()+at,count);at+=count;return count;},bytes.size(),frames?frames:ntsc?720:600,samples,ntsc,clock,simple);uint8_t out[65536];while(input.read(out,sizeof(out))){}input.finish();return true;}catch(const std::exception&){return false;}
}
int main(int argc,char** argv){
 if(argc<3 || argc>9)return 2;bool ntsc=false,fuzz=false,simple=false;harvester::WebmContinuousClock clock;uint64_t frames=0;
 for(int i=3;i<argc;++i){const std::string arg=argv[i];if(arg=="--ntsc")ntsc=true;else if(arg=="--fuzz")fuzz=true;else if(arg=="--cfr" && i+3<argc){const auto n=std::stoul(argv[++i]),d=std::stoul(argv[++i]);clock=harvester::WebmContinuousClock(n,d);frames=std::stoull(argv[++i]);}else if((arg=="--vfr" || arg=="--simple-vfr") && i+2<argc){simple=arg=="--simple-vfr";frames=std::stoull(argv[++i]);const auto end=std::stoull(argv[++i]);clock=harvester::WebmContinuousClock::Vfr(frames,end);}else return 2;}
 std::ifstream file(argv[1],std::ios::binary);if(!file)return 2;const std::vector<uint8_t> raw{std::istreambuf_iterator<char>(file),{}};const auto samples=std::stoull(argv[2]);if(!parse(raw,samples,ntsc,clock,frames,simple))return 1;
 if(fuzz){std::mt19937 random(0x4f505553);for(unsigned i=0;i<2000;++i){auto bytes=raw;if(i%4==0)bytes.resize(random()%raw.size());else for(unsigned n=0;n<1+i%5;++n)bytes[random()%bytes.size()]^=uint8_t(1u<<(random()%8));parse(bytes,samples,ntsc,clock,frames,simple);}}

 std::cout<<"{\"result\":\"PASS\",\"mutations\":"<<(fuzz?2000:0)<<"}"<<std::endl;return 0;
}
