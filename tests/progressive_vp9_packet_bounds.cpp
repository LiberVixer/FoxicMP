// Framing admission tests; real-media worker tests cover full VP9 decoding.
#include "DSUtil/HarvesterVP9.h"
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
int main(){
    const std::vector<uint8_t> key{0x92,0x49,0x83,0x42,0,4,0xf8,2,0xcb,6,0x1c,0x12,0x0e,0x0c,0x49,0};
    const std::vector<uint8_t> inter{0x96,0,0x40,0x92,0xf1,0x11,0x50,0,0,0x28,0x68,7,0xd8,2,0x4c,0xef};
    const uint8_t record[]{1,0,0,0,2,10,0xa2,2,2,2,0,0};
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    const auto config=harvester::VP9Configuration(record,sizeof(record),check);
    auto accepts=[&](const std::vector<uint8_t>& packet,bool first){try{harvester::VP9ContinuousPacket(packet.data(),packet.size(),config,160,90,first,check);return true;}catch(const std::runtime_error&){return false;}};
    unsigned cases=0;auto expect=[&](const std::vector<uint8_t>& p,bool ok,bool first=true){if(accepts(p,first)!=ok)throw std::runtime_error("VP9 packet admission mismatch at "+std::to_string(cases));++cases;};
    expect(key,true);expect(inter,true,false);expect(inter,false);
    for(size_t n=0;n<9;++n)expect({key.begin(),key.begin()+n},false);
    for(unsigned bit:{3u,1u}){auto p=key;p[0]^=uint8_t(1u<<bit);expect(p,false);}
    auto p=key;p[0]^=16;expect(p,false); // profile change
    p=key;p[2]^=1;expect(p,false); // key sync
    p=key;p[4]|=128;expect(p,false); //12-bit declaration
    p=key;p[4]^=8;expect(p,false); // range mismatch at header bit36
    auto set=[&](std::vector<uint8_t>& bytes,unsigned bit,bool value){const auto mask=uint8_t(1u<<(7-bit%8));if(value)bytes[bit/8]|=mask;else bytes[bit/8]&=uint8_t(~mask);};
    p=key;for(unsigned b=37;b<53;++b)set(p,b,false);expect(p,false); // coded width1
    p=key;set(p,69,true);for(unsigned b=70;b<102;++b)set(p,b,false);expect(p,false); // render1x1
    p=key;p.insert(p.end(),inter.begin(),inter.end());p.insert(p.end(),{0xc1,16,16,0xc1});expect(p,false); //two-frame superframe
    p=key;p.insert(p.end(),{0xc0,16,0xc0});expect(p,false); //indexed singleton
    std::mt19937 random(192);const unsigned fuzzCases=20000;
    for(unsigned i=0;i<fuzzCases;++i){std::vector<uint8_t> data(random()%256);for(auto& x:data)x=uint8_t(random());accepts(data,i%2==0);}
    std::cout<<"{\"result\":\"PASS\",\"admissionCases\":"<<cases<<",\"randomBoundsCases\":"<<fuzzCases<<"}\n";
}
