// Bounded packet-derived Opus clock. GPL-3.0-or-later.
#pragma once
#include "HarvesterOggOpus.h"
namespace harvester {
class OpusSampleClock {
    uint64_t packets=0,decoded=0,trim=0;
    bool terminal=false;
    static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
public:
    void Add(int64_t ticks,const uint8_t* data,size_t size,int64_t padding){
        check(!terminal,"unsupported-stream-opus-after-trim");
        unsigned duration=0;try{duration=oggopus::Duration(std::vector<uint8_t>(data,data+size));}catch(...){throw std::runtime_error("unsupported-stream-opus-packet");}
        check(duration==960 && packets<50000,"unsupported-stream-opus-packet");
        check(ticks>=0 && ticks<=3600000 && (!packets?ticks==0:std::abs(ticks*48-int64_t(decoded))<=48),"unsupported-stream-opus-clock");
        check(padding>=0 && padding<=20000000,"unsupported-stream-opus-end-trim");
        const auto tail=(uint64_t(padding)*48000+500000000)/1000000000;
        check(tail<960 && (uint64_t(padding)==tail*1000000000/48000 || uint64_t(padding)==(tail*1000000000+47999)/48000),"unsupported-stream-opus-end-trim");
        check(decoded+960-312<=48000000+959,"stream-opus-sample-limit");
        ++packets;decoded+=960;trim=tail;terminal=padding>0;
    }
    uint64_t Packets()const{return packets;}
    uint64_t Samples()const{return decoded>312+trim?decoded-312-trim:0;}
    bool Terminal()const{return terminal;}
    void Finish()const{check(packets>=2 && Samples()>=649 && Samples()<=48000000,"incomplete-stream-opus-samples");}
};
}
