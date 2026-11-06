// Exact AAC-LC decoder/presentation sample geometry. GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include "HarvesterPcmWindow.h"

namespace harvester { namespace pcm {
class AacWindow {
    uint64_t audible=0, leading=0, packets=0;
    uint32_t rate=0, channels=0;
public:
    bool Configure(uint32_t sampleRate,uint32_t channelCount,uint64_t samples,uint32_t skip) {
        if((sampleRate!=44100 && sampleRate!=48000) || channelCount<1 || channelCount>2 ||
           samples==0 || samples>uint64_t(sampleRate)*3600 || skip>1024) return false;
        rate=sampleRate;channels=channelCount;audible=samples;leading=skip;packets=0;return true;
    }
    bool Valid()const{return rate!=0;}
    uint32_t Rate()const{return rate;}
    uint32_t Channels()const{return channels;}
    uint64_t Samples()const{return audible;}
    uint64_t RawBegin()const{return leading;}
    uint64_t RawEnd()const{return leading+audible;}
    uint64_t PacketCount()const{return (RawEnd()+1023)/1024;}
    uint64_t EncodedEnd()const{return PacketCount()*1024;}
    uint64_t EndPadding()const{return EncodedEnd()-RawEnd();}
    uint64_t ObservedPackets()const{return packets;}
    // A demuxer may shorten the final packet duration or leave the full coded
    // duration and express the audible bound only through the edit list.
    bool Packet(int64_t pts,int64_t dts,uint64_t duration,uint32_t skip=0,uint32_t discard=0) {
        if(!Valid() || packets>=PacketCount() || duration<1 || duration>1024 ||
           pts!=int64_t(packets*1024)-int64_t(leading) || dts!=pts ||
           skip!=(packets==0?leading:0)) return false;
        const bool last=packets+1==PacketCount();
        if((!last && (duration!=1024 || discard!=0)) ||
           (last && duration!=1024 && pts+int64_t(duration)!=int64_t(audible)) ||
           (last && discard!=0 && discard!=EndPadding())) return false;
        ++packets;return true;
    }
    bool Complete()const{return Valid() && packets==PacketCount();}
    bool RawWindow(Window& out,uint64_t target=0,uint64_t decodeFirst=0)const {
        return Valid() && target<audible && out.Configure(leading+target,RawEnd(),rate,decodeFirst);
    }
    // Timestamp-aware decoders already consume priming via the container edit.
    // Their absolute bounds are [target, audible), never [leading+target, ...).
    bool TimestampWindow(uint64_t target,uint64_t& begin,uint64_t& end)const {
        if(!Valid() || target>=audible)return false;
        begin=target;end=audible;return true;
    }
};
}}
