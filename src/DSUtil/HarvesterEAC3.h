// Bounded independent E-AC-3 syncframes -> virtual fMP4. GPL-3.0-or-later.
// ETSI TS 102 366 Annex E/F: bsid 16, 48kHz, six blocks, mono/stereo.
#pragma once
#include "HarvesterAC3.h"
namespace harvester { namespace eac3 {
using ac3::Bytes;using ac3::Check;
struct Waiting {};
struct Bits {
    const uint8_t* data;size_t bytes,at=0;
    unsigned get(unsigned n){Check(n<=32 && at<=bytes*8 && n<=bytes*8-at,"Truncated E-AC-3 BSI");unsigned v=0;while(n--){v=(v<<1)|((data[at/8]>>(7-at%8))&1);++at;}return v;}
};
struct Info {
    unsigned rate=48000,channels=0,length=0,bitrate=0,bsmod=0,acmod=0;uint64_t total=0;
    void header(const uint8_t* p){
        Check(p[0]==0x0b && p[1]==0x77,"Expected big-endian E-AC-3 syncword");
        Check((p[2]&0xf8)==0,"E-AC-3 requires independent stream type 0 and substream ID 0");
        length=2*((unsigned(p[2]&7)<<8)|p[3])+2;
        Check((p[4]>>6)==0 && ((p[4]>>4)&3)==3 && (p[5]>>3)==16,"E-AC-3 requires 48 kHz, six blocks and bsid 16");
        acmod=(p[4]>>1)&7;Check((acmod==1 || acmod==2) && !(p[4]&1),"E-AC-3 requires mono/stereo without LFE");channels=acmod==1?1:2;
        Check(length==256 || length==512 || length==768 || length==1536 || length==2560 || length==4096,"E-AC-3 frame bitrate outside checked profile");bitrate=length/4;
    }
    void bsi(const Bytes& frame){
        Bits b{frame.data(),frame.size(),45};b.get(5);if(b.get(1))b.get(8); // dialnorm/compression remain in payload
        Check(b.get(1)==0,"E-AC-3 mixing metadata is outside this profile");
        bsmod=0;if(b.get(1)){
            bsmod=b.get(3);b.get(2);
            if(acmod==2){Check(b.get(2)!=3,"Reserved E-AC-3 surround mode");Check(b.get(2)!=3,"Reserved E-AC-3 headphone mode");}
            if(b.get(1))b.get(8);b.get(1); // production info/source sample rate
        }
        Check(b.get(1)==0,"E-AC-3 additional BSI/JOC is outside this profile");
    }
};
inline Bytes Init(const Info& i){using namespace audiomp4;auto entry=Z(28);entry[7]=1;P(entry,16,(i.channels<<16)|16);P(entry,24,i.rate<<16);
    const auto dataRate=i.bitrate<<3;const uint32_t substream=(16u<<17)|(i.bsmod<<12)|(i.acmod<<9);
    Bytes config{uint8_t(dataRate>>8),uint8_t(dataRate),uint8_t(substream>>16),uint8_t(substream>>8),uint8_t(substream)};J(entry,B("dec3",config));return audiomp4::Init(i.rate,B("ec-3",entry));}
class Layout:public audiomp4::Projection {
    uint64_t physical=0,frames=0,samples=0;bool initialized=false;
public:
    Info info;
    bool Initialized()const{return initialized;}uint64_t Frames()const{return frames;}uint64_t Samples()const{return samples;}
    void Update(const FragmentIndex::Read& read,uint64_t prefix,bool complete){
        Check(prefix>=physical,"E-AC-3 prefix regressed");
        if(!initialized){
            if(prefix<7){Check(!complete,"Truncated complete E-AC-3 header");throw Waiting{};}
            uint8_t h[7];Check(read(0,h,7),"E-AC-3 header read failed");info.header(h);
            if(prefix<info.length){Check(!complete,"Truncated initial E-AC-3 frame");throw Waiting{};}
            Bytes frame(info.length);Check(read(0,frame.data(),frame.size()),"E-AC-3 initialization read failed");Check(ac3::Crc(frame.data()+2,frame.size()-2)==0,"E-AC-3 CRC mismatch");info.bsi(frame);memory(Init(info));initialized=true;
        }
        while(RangeReady(physical,7,prefix)){
            uint8_t h[7];Check(read(physical,h,7),"E-AC-3 frame header read failed");Info next;next.header(h);
            Check(next.length==info.length && next.acmod==info.acmod,"E-AC-3 stream configuration changed");
            if(!RangeReady(physical,next.length,prefix))break;Check(frames<250000,"E-AC-3 frame count exceeds limit");
            Bytes frame(next.length);Check(read(physical,frame.data(),frame.size()),"E-AC-3 frame read failed");Check(ac3::Crc(frame.data()+2,frame.size()-2)==0,"E-AC-3 CRC mismatch");next.bsi(frame);
            Check(next.bsmod==info.bsmod,"E-AC-3 bitstream mode changed");append(frames+1,samples,1536,physical,next.length);physical+=next.length;samples+=1536;++frames;
        }
        if(complete)Check(frames>0 && physical==prefix,"Truncated complete E-AC-3 frame or trailing data");
    }
};
}}
