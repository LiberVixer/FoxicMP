// Bounded packet-derived Opus clock. GPL-3.0-or-later.
#pragma once
#include "HarvesterOggOpus.h"
namespace harvester {
class OpusSampleClock {
    uint64_t packets=0,decoded=0,trim=0;
    bool terminal=false;
    const bool variable;
    unsigned skip;
    std::vector<uint64_t> offsets{0};
    static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
public:
    explicit OpusSampleClock(bool packetDuration=false):variable(packetDuration),skip(packetDuration?0:312){}
    void Configure(unsigned preSkip){check((variable?(preSkip==120 || preSkip==312):preSkip==312) && (!packets || skip==preSkip),"unsupported-stream-opus-configuration");skip=preSkip;}
    void Add(int64_t ticks,const uint8_t* data,size_t size,int64_t padding){
        check(!terminal,"unsupported-stream-opus-after-trim");
        check(data && size,"unsupported-stream-opus-packet");
        unsigned duration=0;try{duration=oggopus::Duration(std::vector<uint8_t>(data,data+size));}catch(...){throw std::runtime_error("unsupported-stream-opus-packet");}
        check(skip && (variable?duration>=120 && duration<=5760 && duration%120==0:duration==960) && packets<50000,"unsupported-stream-opus-packet");
        check(ticks>=0 && ticks<=3600000 && (!packets?ticks==0:std::abs(ticks*48-int64_t(decoded))<=48),"unsupported-stream-opus-clock");
        check(padding>=0 && padding<=120000000,"unsupported-stream-opus-end-trim");
        const auto tail=(uint64_t(padding)*48000+500000000)/1000000000;
        check(tail<duration && (uint64_t(padding)==tail*1000000000/48000 || uint64_t(padding)==(tail*1000000000+47999)/48000),"unsupported-stream-opus-end-trim");
        check(decoded+duration<=48000000ULL+skip+duration-1,"stream-opus-sample-limit");
        ++packets;decoded+=duration;offsets.push_back(decoded);trim=tail;terminal=padding>0;
    }
    uint64_t Packets()const{return packets;}
    uint64_t Samples()const{return decoded>skip+trim?decoded-skip-trim:0;}
    unsigned PreSkip()const{return skip;}
    uint64_t Decoded()const{return decoded;}
    uint64_t PacketStart(size_t index)const{check(index<offsets.size(),"invalid-opus-packet-index");return offsets[index];}
    bool Terminal()const{return terminal;}
    void Finish()const{check(packets>=2 && Samples()>=649 && Samples()<=48000000,"incomplete-stream-opus-samples");}
};
}
