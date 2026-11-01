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
struct IndexHeader {
    std::string kind,encoder;uint32_t frames=0,bytes=0;unsigned encoderDelay=0,encoderPadding=0;bool hasEncoderDelay=false;
    unsigned tagRevision=0,tagCrc=0,calculatedTagCrc=0;size_t tagCoveredBytes=0;
    bool tagComplete=false,tagCrcChecked=false,tagCrcVerified=false;
    // Reflected CRC-16/IBM, init 0, no final xor. Includes the MPEG header,
    // side info and Xing/LAME fields up to (but not including) the tag CRC.
    static uint16_t TagCrc(const uint8_t* data,size_t count){
        uint16_t crc=0;for(size_t i=0;i<count;++i){crc^=data[i];for(unsigned bit=0;bit<8;++bit)crc=uint16_t((crc>>1)^((crc&1)?0xa001:0));}return crc;
    }
    const char* TagState()const {
        return !hasEncoderDelay?(encoder.empty()?(kind=="VBRI"?"absent":"unsupported-encoder"):"absent"):
            !tagComplete?"truncated":tagRevision?"unsupported-revision":tagCrcVerified?"valid":"crc-mismatch";
    }
    void parse(const Bytes& b,const Info& info){
        *this=IndexHeader{};
        const auto word=[&](size_t at){Check(at<=b.size() && b.size()-at>=4,"Truncated MP3 index header");return BoxData{b.data(),b.size()}.U32(at);};
        Check(b.size()==info.length,"MP3 index frame length mismatch");for(size_t k=4;k<4+info.side;++k)Check(b[k]==0,"MP3 index frame contains nonzero side information");
        const size_t at=4+info.side;
        if(word(at)==FourCC("Xing") || word(at)==FourCC("Info")){
            kind=word(at)==FourCC("Xing")?"Xing":"Info";const auto flags=word(at+4);Check((flags&3)==3 && !(flags&~15),"MP3 Xing needs bounded frame/byte counters");
            frames=word(at+8);bytes=word(at+12);size_t cursor=at+16;
            if(flags&4){Check(cursor<=b.size() && b.size()-cursor>=100,"Truncated Xing TOC");for(size_t k=1;k<100;++k)Check(b[cursor+k]>=b[cursor+k-1],"Nonmonotonic Xing TOC");cursor+=100;}
            if(flags&8){word(cursor);cursor+=4;}
            if(cursor<=b.size() && b.size()-cursor>=9){
                const bool known=!std::memcmp(b.data()+cursor,"LAME",4) || !std::memcmp(b.data()+cursor,"Lavc",4) || !std::memcmp(b.data()+cursor,"Lavf",4);
                if(known){Check(b.size()-cursor>=24,"Truncated encoder delay metadata");for(size_t k=0;k<9;++k)Check(b[cursor+k]>=32 && b[cursor+k]<127,"Invalid encoder identifier");encoder.assign(reinterpret_cast<const char*>(b.data()+cursor),9);const auto p=b.data()+cursor+21;encoderDelay=(unsigned(p[0])<<4)|(p[1]>>4);encoderPadding=((unsigned(p[1])&15)<<8)|p[2];hasEncoderDelay=true;
                    tagRevision=b[cursor+9]>>4;tagComplete=b.size()-cursor>=36;
                    if(tagComplete && !tagRevision){tagCoveredBytes=cursor+34;tagCrc=(unsigned(b[cursor+34])<<8)|b[cursor+35];calculatedTagCrc=TagCrc(b.data(),tagCoveredBytes);tagCrcChecked=true;tagCrcVerified=tagCrc==calculatedTagCrc;}
                }
            }
        }else{
            Check(word(36)==FourCC("VBRI"),"Indexed MP3 requires Xing/Info/VBRI");kind="VBRI";
            Check(b.size()>=62 && b[40]==0 && b[41]==1,"VBRI version/length unsupported");bytes=word(46);frames=word(50);
            const auto u16=[&](size_t n){return (unsigned(b[n])<<8)|b[n+1];};const auto count=u16(54),scale=u16(56),width=u16(58),step=u16(60);
            Check(count>0 && count<=1000 && scale>0 && width>=1 && width<=4 && step>0 && uint64_t(count)*width<=b.size()-62 && uint64_t(count)*step>=frames && uint64_t(count-1)*step<frames,"Invalid/bounded VBRI TOC");
            uint64_t sum=0;for(unsigned k=0;k<count;++k){uint64_t n=0;for(unsigned j=0;j<width;++j)n=(n<<8)|b[62+k*width+j];Check(n>0,"Zero VBRI TOC entry");sum+=n*scale;Check(sum<=bytes,"VBRI TOC exceeds byte count");}Check(sum==bytes,"VBRI TOC does not cover declared bytes");
        }
        Check(frames>0 && frames<=MaxFrames && bytes>info.length && uint64_t(bytes)<=uint64_t(frames+1)*MaxFrame,"MP3 index counters outside bounds");
        Check(!hasEncoderDelay || uint64_t(encoderDelay)+encoderPadding<uint64_t(frames)*1152,"Invalid encoder delay/padding total");
    }
};
struct TrimPlan {
    bool ready=false;uint64_t codedSamples=0,begin=0,end=0,audibleSamples=0;
    unsigned rate=0,decoderDelay=0;const char* reason="unvalidated-encoder-tag";
    static TrimPlan Analyze(const IndexHeader& tag,unsigned sampleRate,unsigned decoderDelay){
        TrimPlan p;p.rate=sampleRate;p.decoderDelay=decoderDelay;p.codedSamples=uint64_t(tag.frames)*1152;
        if(!tag.hasEncoderDelay){p.reason="encoder-delay-unavailable";return p;}
        if(!tag.tagCrcVerified){p.reason=tag.TagState();return p;}
        if(sampleRate!=44100 && sampleRate!=48000){p.reason="unsupported-sample-rate";return p;}
        if(decoderDelay!=529){p.reason="unproven-decoder-delay";return p;}
        if(tag.encoderPadding<decoderDelay || uint64_t(tag.encoderDelay)+tag.encoderPadding>=p.codedSamples){p.reason="unproven-trim-window";return p;}
        p.begin=uint64_t(tag.encoderDelay)+decoderDelay;p.end=p.codedSamples-tag.encoderPadding+decoderDelay;
        p.audibleSamples=p.end-p.begin;p.ready=true;p.reason="validated-metadata";return p;
    }
    static uint64_t Ticks(uint64_t samples,unsigned rate){Check(rate>0,"Zero sample clock");return samples/rate*10000000+(samples%rate)*10000000/rate;}
};
template<bool Indexed> class BasicLayout:public audiomp4::Projection {
    uint64_t physical=0,frames=0,samples=0,reservoir=0,streamStart=0,mediaEnd=0;bool initialized=false,trailer=false;
    void need(bool ready,bool complete,const char* why){if(!ready){Check(!complete,why);throw Waiting{};}}
public:
    Info info;IndexHeader indexHeader;
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
            need(RangeReady(physical,info.length,prefix),complete,"Truncated initial MP3 frame");
            if constexpr(Indexed){Bytes frame(info.length);Check(read(physical,frame.data(),frame.size()),"MP3 index read failed");indexHeader.parse(frame,info);info.total=uint64_t(indexHeader.frames)*1152;streamStart=physical;physical+=info.length;mediaEnd=physical;}
            memory(Init(info));initialized=true;
        }
        if(trailer){Check(prefix==physical,"Data follows ID3v1 trailer");if constexpr(Indexed)if(complete)Check(frames==indexHeader.frames && mediaEnd-streamStart==indexHeader.bytes,"Complete MP3 disagrees with index counters");return;}
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
            if(!frames && !Indexed){Check(std::memcmp(data+next.side,"Xing",4) && std::memcmp(data+next.side,"Info",4) && std::memcmp(data+32,"VBRI",4),"Xing/Info/VBRI gapless/index frame requires another profile");}
            append(frames+1,samples,1152,physical,next.length);reservoir=(std::min)(uint64_t(511),reservoir+next.length-4-next.side);physical+=next.length;mediaEnd=physical;samples+=1152;++frames;
            if constexpr(Indexed)Check(frames<=indexHeader.frames && mediaEnd-streamStart<=indexHeader.bytes,"MP3 index counters exceeded by confirmed audio");
        }
        if(complete){Check(frames>0 && physical==prefix,"Truncated MP3 frame or trailing data");if constexpr(Indexed)Check(frames==indexHeader.frames && mediaEnd-streamStart==indexHeader.bytes,"Complete MP3 disagrees with index counters");}
    }
};
using Layout=BasicLayout<false>;
using IndexedLayout=BasicLayout<true>;
}}
