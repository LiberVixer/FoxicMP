// Bounded AC-3 syncframes -> virtual fMP4, no re-encoding. GPL-3.0-or-later.
// ETSI TS 102 366: 48kHz, bsid 8, mono/stereo, six blocks, CRC1 + CRC2.
#pragma once
#include "HarvesterAudioProjection.h"
namespace harvester { namespace ac3 {
using Bytes=muxmp4::Bytes;
inline void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Waiting {};
inline uint16_t Crc(const uint8_t* p,size_t n){uint16_t c=0;for(size_t i=0;i<n;++i){c^=uint16_t(p[i])<<8;for(unsigned k=0;k<8;++k)c=uint16_t((c<<1)^((c&0x8000)?0x8005:0));}return c;}
struct Info {
    unsigned rate=48000,channels=0,length=0,bitrateCode=0,bsmod=0,acmod=0;uint64_t total=0;
    void parse(const uint8_t* p){
        Check(p[0]==0x0b && p[1]==0x77,"Expected big-endian AC-3 syncword");
        Check((p[4]>>6)==0 && (p[4]&63)<38 && (p[5]>>3)==8,"AC-3 requires 48 kHz and bsid 8");
        static const unsigned kbps[]={32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640};
        bitrateCode=(p[4]&63)>>1;length=kbps[bitrateCode]*4;bsmod=p[5]&7;acmod=p[6]>>5;
        Check(acmod==1 || acmod==2,"AC-3 profile requires mono or stereo");
        if(acmod==2)Check(((p[6]>>3)&3)!=3,"Reserved AC-3 surround mode");
        const bool lfe=acmod==2?(p[6]&4)!=0:(p[6]&16)!=0;
        Check(!lfe,"AC-3 LFE/multichannel is outside this profile");channels=acmod==1?1:2;
    }
};
inline Bytes Init(const Info& i){using namespace audiomp4;auto entry=Z(28);entry[7]=1;P(entry,16,(i.channels<<16)|16);P(entry,24,i.rate<<16);
    const uint32_t bits=(8u<<17)|(i.bsmod<<14)|(i.acmod<<11)|(i.bitrateCode<<5);Bytes config{uint8_t(bits>>16),uint8_t(bits>>8),uint8_t(bits)};J(entry,B("dac3",config));return audiomp4::Init(i.rate,B("ac-3",entry));}
class Layout:public audiomp4::Projection {
    uint64_t physical=0,frames=0,samples=0;bool initialized=false;
public:
    Info info;
    bool Initialized()const{return initialized;}uint64_t Frames()const{return frames;}uint64_t Samples()const{return samples;}
    void Update(const FragmentIndex::Read& read,uint64_t prefix,bool complete){
        Check(prefix>=physical,"AC-3 prefix regressed");
        if(!initialized){
            if(prefix<7){Check(!complete,"Truncated complete AC-3 header");throw Waiting{};}
            uint8_t h[7];Check(read(0,h,7),"AC-3 header read failed");info.parse(h);
            if(prefix<info.length){Check(!complete,"Truncated initial AC-3 frame");throw Waiting{};}
            memory(Init(info));initialized=true;
        }
        while(RangeReady(physical,7,prefix)){
            uint8_t h[7];Check(read(physical,h,7),"AC-3 frame header read failed");Info next;next.parse(h);
            Check(next.bitrateCode==info.bitrateCode && next.acmod==info.acmod && next.bsmod==info.bsmod,"AC-3 stream configuration changed");
            if(!RangeReady(physical,next.length,prefix))break;Check(frames<250000,"AC-3 frame count exceeds limit");
            Bytes frame(next.length);Check(read(physical,frame.data(),frame.size()),"AC-3 frame read failed");
            const auto words=frame.size()/2,split=2*((words>>1)+(words>>3));
            Check(Crc(frame.data()+2,split-2)==0,"AC-3 CRC1 mismatch");Check(Crc(frame.data()+split,frame.size()-split)==0,"AC-3 CRC2 mismatch");
            append(frames+1,samples,1536,physical,next.length);physical+=next.length;samples+=1536;++frames;
        }
        if(complete)Check(frames>0 && physical==prefix,"Truncated complete AC-3 frame or trailing data");
    }
};
}}
