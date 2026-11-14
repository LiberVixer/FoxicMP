// Header/index admission; real two-pass fixtures separately verify decoding.
#include "DSUtil/HarvesterVP9.h"
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
int main(){
    using Bytes=std::vector<uint8_t>;
    const Bytes key{0x92,0x49,0x83,0x42,0,4,0xf8,2,0xcb,6,0x1c,0x12,0x0e,0x0c,0x49,0};
    const Bytes inter{0x96,0,0x40,0x92,0xf1,0x11,0x50,0,0,0x28,0x68,7,0xd8,2,0x4c,0xef};
    const uint8_t record[]{1,0,0,0,2,10,0xa2,2,2,2,0,0};
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto config=harvester::VP9Configuration(record,sizeof(record),check);
    auto accepts=[&](const Bytes& p,bool initial){try{harvester::VP9ContinuousSuperframePacket(p.data(),p.size(),config,160,90,initial,check);return true;}catch(const std::runtime_error&){return false;}};
    unsigned cases=0;auto expect=[&](const Bytes& p,bool ok,bool initial=false){if(accepts(p,initial)!=ok)throw std::runtime_error("Indexed VP9 mismatch at "+std::to_string(cases));++cases;};
    auto indexed=[](const std::vector<Bytes>& frames,unsigned magnitude=1){
        Bytes result;for(const auto& frame:frames)result.insert(result.end(),frame.begin(),frame.end());
        const auto marker=uint8_t(0xc0 | ((magnitude-1)<<3) | (frames.size()-1));result.push_back(marker);
        for(const auto& frame:frames)for(unsigned j=0;j<magnitude;++j)result.push_back(uint8_t(frame.size()>>(j*8)));
        result.push_back(marker);return result;
    };
    // Insert intra_only=0 at bit8 after converting a shown inter header to hidden.
    Bytes hidden(inter.size()+1);for(size_t bit=0;bit<inter.size()*8;++bit){
        const size_t dest=bit<8?bit:bit+1;if(inter[bit/8]&(1u<<(7-bit%8)))hidden[dest/8]|=uint8_t(1u<<(7-dest%8));
    }hidden[0]&=uint8_t(~2);
    expect(key,true,true);expect(indexed({key}),true,true);expect(inter,true);expect(inter,false,true);
    expect(indexed({hidden,inter}),true);expect(indexed({hidden,inter}),false,true);
    for(unsigned magnitude=1;magnitude<=4;++magnitude){expect(indexed({hidden,hidden,inter},magnitude),true);}
    auto eight=std::vector<Bytes>(7,hidden);eight.push_back(inter);expect(indexed(eight,4),true);
    expect(hidden,false);expect(indexed({hidden}),false);expect(indexed({inter,inter}),false);
    expect(indexed({hidden,hidden}),false);expect(indexed({key,inter}),false);
    auto wrong=hidden;wrong[1]|=128;expect(indexed({wrong,inter}),false); //intra_only
    wrong=hidden;wrong[0]|=8;expect(indexed({wrong,inter}),false); //show_existing
    wrong=hidden;wrong[0]^=16;expect(indexed({wrong,inter}),false); //profile
    wrong=key;wrong[4]|=128;expect(wrong,false,true); //12bit
    auto bad=indexed({hidden,inter},4);const size_t at=bad.size()-9;
    for(unsigned i=0;i<4;++i)bad[at+i]=255;expect(bad,false);
    bad=indexed({hidden,inter},4);for(unsigned i=0;i<4;++i)bad[at+i]=0;expect(bad,false);
    bad=indexed({hidden,inter});bad[bad.size()-3]--;expect(bad,false); //sum mismatch
    for(size_t n=0;n<4;++n){expect(Bytes(inter.begin(),inter.begin()+n),false);expect(Bytes(key.begin(),key.begin()+n),false,true);}
    std::mt19937 random(194);const unsigned fuzz=30000;
    for(unsigned i=0;i<fuzz;++i){Bytes p(random()%512);for(auto& b:p)b=uint8_t(random());accepts(p,i%2==0);}
    std::cout<<"{\"result\":\"PASS\",\"admissionCases\":"<<cases<<",\"randomBoundsCases\":"<<fuzz<<"}\n";
}
