// Structural parser bounds/fuzz driver; no media decoder, registry or GUI.
#include "../src/DSUtil/HarvesterWebMOpusContinuous.h"
#include <fstream>
#include <iostream>
#include <random>
static bool parse(const std::vector<uint8_t>& bytes,uint64_t samples){
 size_t at=0;try{harvester::ContinuousWebmOpus input([&](uint8_t* out,size_t n){const auto count=(std::min)(n,bytes.size()-at);if(count)std::memcpy(out,bytes.data()+at,count);at+=count;return count;},bytes.size(),600,samples);uint8_t out[65536];while(input.read(out,sizeof(out))){}input.finish();return true;}catch(const std::exception&){return false;}
}
int main(int argc,char** argv){
 if(argc<3 || argc>4)return 2;std::ifstream file(argv[1],std::ios::binary);if(!file)return 2;const std::vector<uint8_t> raw{std::istreambuf_iterator<char>(file),{}};const auto samples=std::stoull(argv[2]);if(!parse(raw,samples))return 1;
 if(argc==4){std::mt19937 random(0x4f505553);for(unsigned i=0;i<2000;++i){auto bytes=raw;if(i%4==0)bytes.resize(random()%raw.size());else for(unsigned n=0;n<1+i%5;++n)bytes[random()%bytes.size()]^=uint8_t(1u<<(random()%8));parse(bytes,samples);}}
 std::cout<<"{\"result\":\"PASS\",\"mutations\":"<<(argc==4?2000:0)<<"}"<<std::endl;return 0;
}
