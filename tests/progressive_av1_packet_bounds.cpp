// Packet framing admission; decoding is covered by the real-media worker tests.
#include "DSUtil/HarvesterAV1.h"
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

int main() {
    const std::vector<uint8_t> sequence{0,0x10};
    const std::vector<uint8_t> keyed{0x0a,2,0,0x10,0x32,1,0x14};
    auto accepts=[&](const std::vector<uint8_t>& bytes,bool initial=true){
        try { harvester::AV1ContinuousPacket(bytes.data(),bytes.size(),sequence,initial,
            [](bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}); return true;
        } catch(const std::runtime_error&) { return false; }
    };
    unsigned checks=0;
    auto expect=[&](const std::vector<uint8_t>& bytes,bool accepted,bool initial=true){
        if(accepts(bytes,initial)!=accepted)throw std::runtime_error("AV1 packet admission mismatch at case "+std::to_string(checks));
        ++checks;
    };
    expect(keyed,true);
    expect({0x32,1,0x34},true,false); // displayed inter frame
    expect({0x12,0,0x0a,2,0,0x10,0x32,1,0x14},true); // delimiter
    expect({0x0e,0,2,0,0x10,0x36,0,1,0x14},true); // layer0 extension
    for(size_t n=0;n<keyed.size();++n)expect({keyed.begin(),keyed.begin()+n},false);
    expect({0x32,1,0x14},false); // no initial sequence
    expect({0x0a,2,0,0x11,0x32,1,0x14},false); // changed sequence
    expect({0x0a,2,0,0x10,0x32,1,0x94},false); // show_existing_frame
    expect({0x0a,2,0,0x10,0x32,1,0x04},false); // hidden frame
    expect({0x0a,2,0,0x10,0x32,1,0x34},false); // first frame not key
    expect({0x0a,2,0,0x10,0x32,1,0x14,0x32,1,0x34},false); // multiple frames
    expect({0x0e,1,2,0,0x10,0x32,1,0x14},false); // reserved layer bit
    expect({0x0a,2,0,0x10,0x30,1,0x14},false); // absent size field
    expect({0x0a,2,0,0x10,0xb2,1,0x14},false); // forbidden bit
    expect({0x0a,2,0,0x10,0x33,1,0x14},false); // reserved bit
    expect({0x0a,2,0,0x10,0x32,0},false); // empty frame
    expect({0x0a,2,0,0x10,0x32,2,0x14},false); // length overrun
    expect({0x0a,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80},false); // unterminated LEB128
    expect({0x0a,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x7f},false); // huge bounded length
    auto many=keyed;for(unsigned i=0;i<128;++i)many.insert(many.end(),{0x7a,1,0});expect(many,false);
    // Reproducible malformed inputs exercise all size/header boundaries under ASan/UBSan.
    std::mt19937 random(190);const unsigned fuzzCases=20000;
    for(unsigned i=0;i<fuzzCases;++i){std::vector<uint8_t> bytes(random()%256);for(auto& b:bytes)b=uint8_t(random());accepts(bytes,i%2==0);}
    std::cout<<"{\"result\":\"PASS\",\"admissionCases\":"<<checks<<",\"randomBoundsCases\":"<<fuzzCases<<"}\n";
}
