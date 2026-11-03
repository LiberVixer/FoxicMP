// Bounded RIFF/WAVE PCM16/24 -> virtual fMP4; no sample conversion.
// GPL-3.0-or-later.
#pragma once
#include "HarvesterAudioProjection.h"
namespace harvester { namespace wav {
using Bytes=muxmp4::Bytes;
inline void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
inline unsigned U16(const uint8_t* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
inline uint32_t U32(const uint8_t* p){return uint32_t(U16(p))|(uint32_t(U16(p+2))<<16);}
inline uint64_t U64(const uint8_t* p){return uint64_t(U32(p))|(uint64_t(U32(p+4))<<32);}
struct Waiting {};
struct Info {unsigned rate=0,channels=0,align=0,bits=16;uint64_t total=0;};
inline Bytes Init(const Info& i){using namespace audiomp4;Bytes entry=Z(i.bits==24?44:28);entry[7]=1;P(entry,16,(i.channels<<16)|16);P(entry,24,i.rate<<16);
    if(i.bits==24){P(entry,8,1u<<16);P(entry,28,1);P(entry,32,3);P(entry,36,i.align);P(entry,40,3);J(entry,B("wave",B("enda",Bytes{0,1})));}
    return audiomp4::Init(i.rate,B(i.bits==24?"in24":"sowt",entry));}
template<unsigned BitsPerSample,bool Extensible=false,bool Rf64=false> class PcmLayout:public audiomp4::Projection {
    uint64_t physical=0,dataStart=0,frames=0,samples=0,dataEnd=0,fileEnd=0;bool initialized=false;
public:
    static constexpr unsigned PacketSamples=Rf64?16384:1024;
    Info info;
    uint64_t FileBytes()const{return fileEnd;}
    uint64_t DataOffset()const{return dataStart;}
    bool Initialized()const{return initialized;}uint64_t Frames()const{return frames;}uint64_t Samples()const{return samples;}
    void Update(const FragmentIndex::Read& read,uint64_t prefix,bool complete){
        const auto wait=[&]{Check(!complete,"Truncated complete WAV header/data");throw Waiting{};};
        if(!initialized){
            if(prefix<12)wait();uint8_t h[40]={};Check(read(0,h,12),"WAV header read failed");
            Check(!std::memcmp(h,Rf64?"RF64":"RIFF",4) && !std::memcmp(h+8,"WAVE",4),"Expected profile-matching little-endian WAVE");
            uint64_t at=12,rfData=0,rfSamples=0;const auto riff=U32(h+4);
            if(Rf64){
                Check(riff==UINT32_MAX,"RF64 requires a RIFF size sentinel");
                if(prefix<48)wait();Check(read(12,h,36),"RF64 ds64 read failed");
                Check(!std::memcmp(h,"ds64",4) && U32(h+4)==28 && U32(h+32)==0,"RF64 requires the first ds64 chunk with no size table");
                const auto size=U64(h+8);rfData=U64(h+16);rfSamples=U64(h+24);
                Check(size>=72 && size<=(uint64_t(1)<<34)-8 && rfData && rfData<=size && rfSamples,"Invalid or oversized RF64 ds64 sizes/count");
                fileEnd=size+8;at=48;
            }else{Check(riff>=36 && riff!=UINT32_MAX,"WAV requires known RIFF size");fileEnd=uint64_t(riff)+8;}
            bool format=false,found=false;
            for(unsigned chunk=0;chunk<64;++chunk){
                Check(at<=1048576 && RangeReady(at,8,fileEnd),"WAV metadata bounds/limit");
                if(!RangeReady(at,8,prefix))wait();Check(read(at,h,8),"WAV chunk read failed");
                uint64_t size=U32(h+4);
                if(Rf64 && !std::memcmp(h,"data",4)){Check(size==UINT32_MAX,"RF64 requires a data size sentinel");size=rfData;}
                const auto next=at+8+size+(size&1);Check(next<=fileEnd,"WAV chunk exceeds RIFF");
                if(!std::memcmp(h,"data",4)){
                    Check(format && size && size%info.align==0 && next==fileEnd,"WAV requires one final aligned data chunk after fmt");
                    dataStart=physical=at+8;dataEnd=physical+size;info.total=size/info.align;
                    Check(!Rf64 || info.total==rfSamples,"RF64 sample count disagrees with PCM data");
                    Check((info.total+PacketSamples-1)/PacketSamples<=250000,"WAV packet count exceeds limit");found=true;break;
                }
                Check(next<=1048576,"WAV metadata exceeds 1 MiB");
                if(!RangeReady(at,next-at,prefix))wait();
                if(!std::memcmp(h,"fmt ",4)){
                    Check(!format && (Extensible?size==40:(size==16 || size==18)),"Expected one profile-matching WAV fmt chunk");Check(read(at+8,h,size_t(size)),"WAV fmt read failed");
                    info.bits=BitsPerSample;info.channels=U16(h+2);info.rate=U32(h+4);info.align=U16(h+12);
                    Check(U16(h)==(Extensible?0xfffe:1) && (info.channels==1 || info.channels==2) && (info.rate==44100 || info.rate==48000) && U16(h+14)==BitsPerSample,"WAV bit depth/profile or mono/stereo 44.1/48 kHz mismatch");
                    Check(info.align==info.channels*(BitsPerSample/8) && U32(h+8)==info.rate*info.align,"Invalid PCM WAV byte rate/block alignment");
                    if(Extensible){
                        static const uint8_t pcmGuid[16]={1,0,0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                        Check(U16(h+16)==22 && U16(h+18)==BitsPerSample && U32(h+20)==(info.channels==1?4u:3u) && !std::memcmp(h+24,pcmGuid,16),"Unsupported extensible WAV valid bits/channel mask/subformat");
                    }else Check(size==16 || U16(h+16)==0,"Invalid PCM WAV extension");
                    format=true;
                } else Check(!std::memcmp(h,"JUNK",4) || !std::memcmp(h,"LIST",4) || !std::memcmp(h,"bext",4),"Unsupported WAV metadata chunk");
                at=next;
            }
            Check(found,"WAV chunk count exceeds limit");
            if(!RangeReady(physical,(std::min)(uint64_t(PacketSamples),info.total)*info.align,prefix))wait();
            memory(Init(info));initialized=true;
        }
        Check(prefix>=physical && prefix<=fileEnd,"WAV prefix regressed or exceeds RIFF");
        while(samples<info.total){
            const auto count=unsigned((std::min)(uint64_t(PacketSamples),info.total-samples));const auto n=size_t(count*info.align);
            if(!RangeReady(physical,n,prefix))break;
            append(++frames,samples,count,physical,n);physical+=n;samples+=count;
        }
        if(complete)Check(prefix==fileEnd && physical==dataEnd && samples==info.total,"Truncated complete WAV or trailing bytes");
    }
};
using Layout=PcmLayout<16>;
using Layout24=PcmLayout<24>;
using LayoutExt24=PcmLayout<24,true>;
using LayoutRf16=PcmLayout<16,false,true>;
using LayoutRf24=PcmLayout<24,true,true>;
}}
