// Bounded MPEG-1 Layer III framing and virtual fMP4. GPL-3.0-or-later.
#pragma once
#include "HarvesterAudioProjection.h"
namespace harvester { namespace mp3 {
using Bytes=muxmp4::Bytes;
constexpr unsigned MaxFrame=1441,MaxFrames=250000,MaxMetadata=1048576;
inline void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Waiting {};
struct Info {
    unsigned rate=0,channels=0,length=0,side=0;uint64_t total=0;
    void parse(const uint8_t* p){
        Check(p[0]==255 && p[1]==0xfb,"Expected unprotected MPEG-1 Layer III");
        const unsigned bit=p[2]>>4,index=(p[2]>>2)&3;const unsigned kbps[]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
        Check(kbps[bit] && index<=1 && (p[3]&3)!=2,"MP3 bitrate/rate/emphasis outside profile");
        rate=index?48000:44100;channels=(p[3]>>6)==3?1:2;side=channels==1?17:32;
        length=144000*kbps[bit]/rate+((p[2]>>1)&1);Check(length>4+side && length<=MaxFrame,"MP3 frame length outside bounds");
    }
};
inline Bytes Init(const Info& i){
    using namespace audiomp4;
    Bytes dec{0x6b,0x15,0,0,0,0,0,0,0,0,0,0,0};
    Bytes es{0,1,0,4,uint8_t(dec.size())};J(es,dec);J(es,Bytes{6,1,2});
    Bytes esds=Z(4);J(esds,Bytes{3,uint8_t(es.size())});J(esds,es);
    Bytes entry=Z(28);entry[7]=1;P(entry,16,(i.channels<<16)|16);P(entry,24,i.rate<<16);J(entry,B("esds",esds));
    return audiomp4::Init(i.rate,B("mp4a",entry));
}
class Layout:public audiomp4::Projection {
    uint64_t physical=0,frames=0,samples=0,reservoir=0;bool initialized=false,trailer=false;
    void need(bool ready,bool complete,const char* why){if(!ready){Check(!complete,why);throw Waiting{};}}
public:
    Info info;
    bool Initialized()const{return initialized;}uint64_t Frames()const{return frames;}uint64_t Samples()const{return samples;}
    void Update(const FragmentIndex::Read& read,uint64_t prefix,bool complete){
        Check(prefix>=physical,"MP3 prefix regressed");
        if(!initialized){
            need(prefix>=4,complete,"Truncated MP3 header");uint8_t h[10]={};Check(read(0,h,4),"MP3 initial read failed");
            if(!std::memcmp(h,"ID3",3)){
                need(prefix>=10,complete,"Truncated ID3 header");Check(read(0,h,10),"ID3 header read failed");
                Check((h[3]==3 || h[3]==4) && h[4]!=255 && !(h[5]&(h[3]==3?31:15)),"Unsupported ID3 version/flags");
                uint64_t size=0;for(unsigned i=6;i<10;++i){Check(!(h[i]&128),"Invalid ID3 synchsafe size");size=(size<<7)|h[i];}
                const bool footer=h[3]==4 && (h[5]&16);const uint64_t end=10+size+(footer?10:0);Check(end<=MaxMetadata,"MP3 ID3 prefix exceeds 1 MiB");need(prefix>=end,complete,"Truncated ID3 tag");
                if(footer){uint8_t f[10];Check(read(end-10,f,10) && !std::memcmp(f,"3DI",3) && !std::memcmp(f+3,h+3,7),"ID3 footer/header mismatch");}
                physical=end;
            }
            need(RangeReady(physical,4,prefix),complete,"Truncated first MP3 header");Check(read(physical,h,4),"MP3 header read failed");info.parse(h);
            need(RangeReady(physical,info.length,prefix),complete,"Truncated initial MP3 frame");memory(Init(info));initialized=true;
        }
        if(trailer){Check(prefix==physical,"Data follows ID3v1 trailer");return;}
        while(RangeReady(physical,4,prefix)){
            uint8_t h[4];Check(read(physical,h,4),"MP3 header read failed");
            if(!std::memcmp(h,"TAG",3)){
                Check(prefix-physical<=128,"Data follows ID3v1 trailer");if(prefix-physical<128)break;
                physical+=128;trailer=true;break;
            }
            Info next;next.parse(h);Check(next.rate==info.rate && next.channels==info.channels,"MP3 configuration changed");
            if(!RangeReady(physical,next.length,prefix))break;
            Check(frames<MaxFrames,"MP3 frame count exceeds limit");uint8_t data[40]={};Check(read(physical+4,data,36),"MP3 side information read failed");
            const unsigned back=(unsigned(data[0])<<1)|(data[1]>>7);Check(back<=reservoir,"MP3 bit reservoir points before confirmed audio");
            if(!frames){Check(std::memcmp(data+next.side,"Xing",4) && std::memcmp(data+next.side,"Info",4) && std::memcmp(data+32,"VBRI",4),"Xing/Info/VBRI gapless/index frame requires another profile");}
            append(frames+1,samples,1152,physical,next.length);reservoir=(std::min)(uint64_t(511),reservoir+next.length-4-next.side);physical+=next.length;samples+=1152;++frames;
        }
        if(complete)Check(frames>0 && physical==prefix,"Truncated MP3 frame or trailing data");
    }
};
}}
