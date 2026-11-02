// Synthetic 64-bit address oracle: production layout/index, no large physical file.
#pragma once
#include "DSUtil/HarvesterWav.h"
namespace rf64_geometry {
inline void check(bool value){if(!value)throw std::runtime_error("RF64 64-bit geometry mismatch");}
inline void le(harvester::wav::Bytes& b,size_t at,uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b.at(at+i)=uint8_t(v>>(8*i));}
template<unsigned Bits> void one(unsigned channels,unsigned rate){
    using namespace harvester;using Layout=wav::PcmLayout<Bits,Bits==24,true>;
    const uint64_t align=channels*(Bits/8),total=((uint64_t(1)<<32)+131071)/align+1,dataBytes=total*align;
    const size_t offset=Bits==16?80:104;const uint64_t fileBytes=offset+dataBytes+(dataBytes&1);
    wav::Bytes h(offset,0);auto tag=[&](size_t at,const char* value){std::memcpy(h.data()+at,value,4);};
    tag(0,"RF64");le(h,4,UINT32_MAX,4);tag(8,"WAVE");tag(12,"ds64");le(h,16,28,4);le(h,20,fileBytes-8,8);le(h,28,dataBytes,8);le(h,36,total,8);
    tag(48,"fmt ");le(h,52,Bits==16?16:40,4);le(h,56,Bits==16?1:65534,2);le(h,58,channels,2);le(h,60,rate,4);le(h,64,rate*align,4);le(h,68,align,2);le(h,70,Bits,2);
    if(Bits==24){le(h,72,22,2);le(h,74,24,2);le(h,76,channels==1?4:3,4);const uint8_t guid[16]={1,0,0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};std::memcpy(h.data()+80,guid,16);}
    tag(offset-8,"data");le(h,offset-4,UINT32_MAX,4);bool highRead=false;
    auto raw=[&](uint64_t at,void* out,size_t n){if(!RangeReady(at,n,fileBytes))return false;auto* p=static_cast<uint8_t*>(out);for(size_t k=0;k<n;++k){const auto pos=at+k;p[k]=pos<h.size()?h[size_t(pos)]:uint8_t((pos>>32)+(pos&255));}highRead|=at>UINT32_MAX;return true;};
    Layout layout;layout.Update(raw,fileBytes,true);check(layout.Samples()==total && layout.Frames()==(total+16383)/16384);
    FragmentIndex index;index.Update([&](uint64_t at,void* out,size_t n){return layout.ReadAt(raw,at,out,n);},layout.view.bytes,1,true,0,false,false,false,false,false,false,Bits==16,false,false,Bits==24);
    check(index.Samples().size()==layout.Frames());const auto& last=index.Samples().back();check(last.offset>UINT32_MAX && last.end==int64_t(total*10000000/rate));
    check(AudioSeekSample(index.Samples(),last.start)==index.Samples().size()-1);
    uint8_t bytes[17];check(layout.ReadAt(raw,last.offset,bytes,sizeof(bytes)) && highRead);const auto physical=offset+(layout.Frames()-1)*16384*align;
    for(size_t k=0;k<sizeof(bytes);++k)check(bytes[k]==uint8_t(((physical+k)>>32)+((physical+k)&255)));
    check(!layout.ReadAt(raw,layout.view.bytes,bytes,1));
    // Independent size overflow and inconsistent sample count rejection.
    auto bad=h;le(bad,20,UINT64_MAX,8);Layout broken;bool rejected=false;
    try{broken.Update([&](uint64_t at,void* out,size_t n){if(!RangeReady(at,n,bad.size()))return false;std::memcpy(out,bad.data()+size_t(at),n);return true;},fileBytes,true);}catch(const std::exception&){rejected=true;}
    check(rejected);
}
inline int run(){try{for(auto rate:{44100u,48000u})for(auto channels:{1u,2u}){one<16>(channels,rate);one<24>(channels,rate);}std::cout<<"{\"result\":\"PASS\",\"cases\":8,\"scope\":\"production layout and FragmentIndex with synthetic addresses above 4 GiB\",\"physicalLargeFileCreated\":false}"<<std::endl;return 0;}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
}
