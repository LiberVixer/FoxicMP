// Bounded MPEG-4 ADTS AAC-LC -> virtual fMP4, without re-encoding. GPL-3.0-or-later.
#pragma once
#include "HarvesterAudioProjection.h"
namespace harvester { namespace adts {
using Bytes=muxmp4::Bytes;
constexpr unsigned MaxFrame=8191,MaxFrames=250000;
inline void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Waiting {};
struct Info {
    unsigned rate=0,channels=0,index=0,length=0;uint64_t total=0;
    void parse(const uint8_t* p){
        Check(p[0]==255 && p[1]==0xf1,"Expected MPEG-4 ADTS without CRC, layer zero");
        index=(p[2]>>2)&15;channels=((p[2]&1)<<2)|(p[3]>>6);
        Check((p[2]>>6)==1 && (index==3 || index==4) && (channels==1 || channels==2),"ADTS requires AAC-LC, 44.1/48 kHz, mono/stereo");
        Check((p[6]&3)==0,"ADTS requires one raw data block per frame");
        rate=index==3?48000:44100;length=((p[3]&3)<<11)|(unsigned(p[4])<<3)|(p[5]>>5);
        Check(length>7 && length<=MaxFrame,"Invalid ADTS frame length");
    }
};
inline Bytes Init(const Info& i){
    using namespace audiomp4;
    const unsigned asc=(2u<<11)|(i.index<<7)|(i.channels<<3);
    Bytes dec{0x40,0x15,0,0,0,0,0,0,0,0,0,0,0};J(dec,Bytes{5,2,uint8_t(asc>>8),uint8_t(asc)});
    Bytes es{0,1,0,4,uint8_t(dec.size())};J(es,dec);J(es,Bytes{6,1,2});
    Bytes esds=Z(4);J(esds,Bytes{3,uint8_t(es.size())});J(esds,es);
    Bytes entry=Z(28);entry[7]=1;P(entry,16,(i.channels<<16)|16);P(entry,24,i.rate<<16);J(entry,B("esds",esds));
    return audiomp4::Init(i.rate,B("mp4a",entry));
}
class Layout:public audiomp4::Projection {
    uint64_t physical=0,frames=0,samples=0;bool initialized=false;
public:
    Info info;
    bool Initialized()const{return initialized;}uint64_t Frames()const{return frames;}uint64_t Samples()const{return samples;}
    void Update(const FragmentIndex::Read& read,uint64_t prefix,bool complete){
        Check(prefix>=physical,"ADTS prefix regressed");
        if(!initialized){
            if(prefix<7){Check(!complete,"Truncated complete ADTS header");throw Waiting{};}
            uint8_t h[7];Check(read(0,h,7),"ADTS initialization read failed");info.parse(h);
            if(prefix<info.length){Check(!complete,"Truncated initial ADTS frame");throw Waiting{};}
            memory(Init(info));initialized=true;
        }
        while(RangeReady(physical,7,prefix)){
            uint8_t h[7];Check(read(physical,h,7),"ADTS frame header read failed");Info next;next.parse(h);
            Check(next.rate==info.rate && next.channels==info.channels,"ADTS configuration changed");
            if(!RangeReady(physical,next.length,prefix))break;
            Check(frames<MaxFrames,"ADTS frame count exceeds limit");
            append(frames+1,samples,1024,physical+7,next.length-7);physical+=next.length;samples+=1024;++frames;
        }
        if(complete)Check(frames>0 && physical==prefix,"Truncated ADTS frame or trailing data");
    }
};
}} // namespace harvester::adts
