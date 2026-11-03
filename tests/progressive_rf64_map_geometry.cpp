// Independent synthetic address cases for the production RF64 packet mapper.
#include "../shared/progressiverf64rangemap.h"
#include "DSUtil/HarvesterWav.h"
#include <iostream>
#include <random>
using namespace foxicmp::progressive;
static void check(bool ok){if(!ok)throw std::runtime_error("RF64 range map geometry mismatch");}
static void le(std::vector<uint8_t>& h,size_t at,uint64_t value,unsigned n){for(unsigned i=0;i<n;++i)h.at(at+i)=uint8_t(value>>(8*i));}
template<unsigned Bits> unsigned run(unsigned channels,unsigned rate){
 using Layout=harvester::wav::PcmLayout<Bits,Bits==24,true>;const uint64_t align=channels*(Bits/8),offset=Bits==16?80:104;
 const uint64_t frames=((uint64_t(1)<<32)/align+12345)|1,bytes=frames*align,fileEnd=offset+bytes+(bytes&1);std::vector<uint8_t> h(size_t(offset),0);
 auto tag=[&](size_t at,const char* s){std::memcpy(h.data()+at,s,4);};tag(0,"RF64");le(h,4,UINT32_MAX,4);tag(8,"WAVE");tag(12,"ds64");le(h,16,28,4);le(h,20,fileEnd-8,8);le(h,28,bytes,8);le(h,36,frames,8);tag(48,"fmt ");le(h,52,Bits==16?16:40,4);le(h,56,Bits==16?1:65534,2);le(h,58,channels,2);le(h,60,rate,4);le(h,64,rate*align,4);le(h,68,align,2);le(h,70,Bits,2);
 if(Bits==24){le(h,72,22,2);le(h,74,24,2);le(h,76,channels==1?4:3,4);const uint8_t guid[16]={1,0,0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};std::memcpy(h.data()+80,guid,16);}
 tag(offset-8,"data");le(h,offset-4,UINT32_MAX,4);const uint64_t packetBytes=16384*align,prefix=offset+packetBytes;unsigned reads=0;
 Layout layout;layout.Update([&](uint64_t at,void* out,size_t n){++reads;if(at>h.size() || n>h.size()-at)return false;std::memcpy(out,h.data()+at,n);return true;},prefix,false);check(layout.DataOffset()==offset && layout.FileBytes()==fileEnd);const auto originalReads=reads;
 const auto highFrame=(frames-1)/16384*16384;auto map=Rf64PacketRanges(layout,{{0,prefix},{offset+highFrame*align,frames*align-highFrame*align}},prefix);
 check(map.size()==2 && map[0].prefix && !map[1].prefix && map[1].offset>UINT32_MAX && map[1].end==frames && map[1].endTicks==int64_t(frames*10000000/rate));
 auto padded=Rf64PacketRanges(layout,{{0,prefix},{offset+highFrame*align,fileEnd-offset-highFrame*align}},prefix);
 check(padded.size()==2 && padded[1].bytes==map[1].bytes && padded[1].offset+padded[1].bytes==offset+bytes);
 std::mt19937 random(Bits+channels+rate);unsigned cases=1;
 for(unsigned i=0;i<200;++i){
  const auto a=uint64_t(random())%(frames*align-packetBytes)+offset;
  const auto z=(std::min)(offset+frames*align,a+packetBytes*3+random()%unsigned(packetBytes));
  std::vector<Range> ranges{{0,prefix}};if(a>prefix)ranges.push_back({a,z-a});else continue;
  map=Rf64PacketRanges(layout,ranges,prefix);check(!map.empty() && map[0].first==0 && map[0].end==16384);
  // Independent packet enumeration near the randomly selected physical range.
  uint64_t first=UINT64_MAX,last=0;for(uint64_t p=(a-offset)/packetBytes;p<=(z-offset)/packetBytes && p*16384<frames;++p){auto begin=offset+p*packetBytes,end=offset+(std::min)(frames,(p+1)*16384)*align;if(begin>=a && end<=z){first=(std::min)(first,p*16384);last=(std::min)(frames,(p+1)*16384);}}
  check(map.size()==(last?2u:1u));if(last){check(map[1].first==first && map[1].end==last && map[1].offset==offset+first*align && map[1].bytes==(last-first)*align && !map[1].prefix);}
  ++cases;
 }
 check(reads==originalReads);return cases;
}
int main(){try{unsigned cases=0;for(auto channels:{1u,2u})for(auto rate:{44100u,48000u}){cases+=run<16>(channels,rate);cases+=run<24>(channels,rate);}std::cout<<"{\"result\":\"PASS\",\"cases\":"<<cases<<",\"profiles\":8,\"addressesAbove4GiB\":true,\"physicalLargeFileCreated\":false,\"mapperPerformsIo\":false,\"riffPaddingExcluded\":true}"<<std::endl;return 0;}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
