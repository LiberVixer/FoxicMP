// Exact AAC-LC decoder/presentation sample geometry. GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include "HarvesterPcmWindow.h"

namespace harvester { namespace pcm {
class AacWindow {
    uint64_t audible=0, leading=0, packets=0, encodedPackets=0;
    uint32_t rate=0, channels=0,frame=1024;
    bool ordinaryM4A=false;
public:
    bool Configure(uint32_t sampleRate,uint32_t channelCount,uint64_t samples,uint32_t skip) {
        if(skip>1024)return false;
        if(!ConfigureM4A(sampleRate,channelCount,samples,skip,1024))return false;
        ordinaryM4A=false;return true;
    }
    bool ConfigureM4A(uint32_t sampleRate,uint32_t channelCount,uint64_t samples,uint32_t skip,uint32_t frameSamples,uint64_t declaredPackets=0) {
        if((sampleRate!=44100 && sampleRate!=48000) || channelCount<1 || channelCount>2 ||
           samples==0 || samples>uint64_t(sampleRate)*3600 || (frameSamples!=1024 && frameSamples!=2048) || skip>16384) return false;
        const auto minimum=(samples+skip+frameSamples-1)/frameSamples;
        if(declaredPackets && (declaredPackets<minimum || declaredPackets>minimum+4))return false;
        rate=sampleRate;channels=channelCount;frame=frameSamples;audible=samples;leading=skip;packets=0;encodedPackets=declaredPackets;ordinaryM4A=true;return true;
    }
    bool Valid()const{return rate!=0;}
    uint32_t Rate()const{return rate;}
    uint32_t Channels()const{return channels;}
    uint32_t FrameSamples()const{return frame;}
    bool IsM4A()const{return ordinaryM4A;}
    uint64_t Samples()const{return audible;}
    uint64_t RawBegin()const{return leading;}
    uint64_t RawEnd()const{return leading+audible;}
    uint64_t PacketCount()const{return encodedPackets?encodedPackets:(RawEnd()+frame-1)/frame;}
    uint64_t EncodedEnd()const{return PacketCount()*frame;}
    uint64_t EndPadding()const{return EncodedEnd()-RawEnd();}
    uint64_t ObservedPackets()const{return packets;}
    // A demuxer may shorten the final packet duration or leave the full coded
    // duration and express the audible bound only through the edit list.
    bool Packet(int64_t pts,int64_t dts,uint64_t duration,uint32_t skip=0,uint32_t discard=0) {
        if(!Valid() || packets>=PacketCount() || duration<1 || duration>frame ||
           pts!=int64_t(packets*frame)-int64_t(leading) || dts!=pts ||
           skip!=(packets==0?leading:0)) return false;
        const bool last=packets+1==PacketCount();
        if((!last && (duration!=frame || discard!=0)) ||
           (last && duration!=frame && pts+int64_t(duration)!=int64_t(audible)) ||
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
