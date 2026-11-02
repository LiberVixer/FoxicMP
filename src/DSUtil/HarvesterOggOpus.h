// Bounded Ogg Opus -> single-track WebM, without re-encoding.
// RFC 3533 / RFC 7845. GPL-3.0-or-later.
#pragma once
#include "HarvesterAudioProjection.h"
#include "HarvesterWebMMuxed.h"
namespace harvester { namespace oggopus {
using Bytes=std::vector<uint8_t>;
inline void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
inline uint32_t LE32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline uint64_t LE64(const uint8_t* p){return LE32(p)|(uint64_t(LE32(p+4))<<32);}
inline uint32_t Crc(const Bytes& b){uint32_t c=0;for(size_t i=0;i<b.size();++i){c^=uint32_t(i>=22 && i<26?0:b[i])<<24;for(unsigned bit=0;bit<8;++bit)c=(c<<1)^((c&0x80000000)?0x04c11db7:0);}return c;}
struct Waiting {};
struct Piece {uint64_t at=0;size_t size=0;};
struct Packet {std::vector<Piece> pieces;Bytes data;unsigned samples=0;};
inline unsigned Duration(const Bytes& b){
    Check(!b.empty(),"Empty Ogg Opus packet");const unsigned config=b[0]>>3,code=b[0]&3;
    const unsigned silk[]={480,960,1920,2880};const unsigned frame=config>=16?120u<<(config&3):config>=12?480u<<(config&1):silk[config&3];
    size_t at=1,end=b.size();unsigned count=code==0?1:2;bool vbr=code==2;
    if(code==3){Check(at<end,"Truncated Opus frame count");const auto flags=b[at++];count=flags&63;vbr=(flags&128)!=0;Check(count>0 && count<=48,"Invalid Opus frame count");
        if(flags&64){size_t padding=0;unsigned n;do{Check(at<end,"Truncated Opus padding");n=b[at++];padding+=n==255?254:n;Check(padding<=end-at,"Opus padding exceeds packet");}while(n==255);end-=padding;}
    }
    Check(frame*count<=5760,"Opus packet exceeds 120 ms");
    if(vbr){size_t sum=0;for(unsigned i=1;i<count;++i){Check(at<end,"Truncated Opus VBR lengths");unsigned n=b[at++];if(n>=252){Check(at<end,"Truncated Opus VBR length");n+=4u*b[at++];}Check(n<=1275 && sum<=end-at && n<=end-at-sum,"Opus VBR frame exceeds packet");sum+=n;}Check(sum<=end-at && end-at-sum<=1275,"Invalid final Opus VBR frame");}
    else Check(at<=end && (end-at)%count==0 && (end-at)/count<=1275,"Invalid Opus CBR framing");
    return frame*count;
}
inline Bytes E(uint32_t id,const Bytes& body){auto b=muxwebm::Header(id,body.size());muxwebm::Join(b,body);return b;}
inline Bytes N(uint32_t id,uint64_t value){unsigned n=1;while(n<8 && value>=(uint64_t(1)<<(8*n)))++n;Bytes b(n);for(unsigned i=0;i<n;++i)b[n-i-1]=uint8_t(value>>(8*i));return E(id,b);}
inline Bytes Text(uint32_t id,const char* s){return E(id,Bytes(s,s+std::strlen(s)));}
struct Info {unsigned rate=48000,channels=0,skip=0;uint64_t total=0;};
inline Bytes Init(const Info& i,const Bytes& head){
    Bytes ebml;for(const auto& b:{N(0x4286,1),N(0x42f7,1),N(0x42f2,4),N(0x42f3,8),Text(0x4282,"webm"),N(0x4287,4),N(0x4285,2)})muxwebm::Join(ebml,b);
    Bytes out=E(0x1a45dfa3,ebml);muxwebm::Join(out,Bytes{0x18,0x53,0x80,0x67,1,255,255,255,255,255,255,255});
    Bytes info;for(const auto& b:{N(0x2ad7b1,500000),Text(0x4d80,"FoxicMP"),Text(0x5741,"FoxicMP")})muxwebm::Join(info,b);muxwebm::Join(out,E(0x1549a966,info));
    // IEEE754 binary64, 48000 Hz, big endian.
    Bytes audio=E(0xb5,Bytes{0x40,0xe7,0x70,0,0,0,0,0});muxwebm::Join(audio,N(0x9f,i.channels));
    Bytes entry;for(const auto& b:{N(0xd7,1),N(0x73c5,1),N(0x83,2),N(0x9c,0),Text(0x86,"A_OPUS"),E(0x63a2,head),N(0x56aa,uint64_t(i.skip)*1000000000/48000),N(0x56bb,80000000),E(0xe1,audio)})muxwebm::Join(entry,b);
    muxwebm::Join(out,E(0x1654ae6b,E(0xae,entry)));return out;
}
class Layout:public audiomp4::Projection {
    uint64_t physical=0,scanned=0,decoded=0,frames=0,pages=0,packets=0,lastGranule=0;uint32_t serial=0;
    bool started=false,initialized=false,eos=false,pending=false;Packet partial;Bytes head;
    void HeaderPacket(const Bytes& b){
        if(packets==0){Check(b.size()==19 && !std::memcmp(b.data(),"OpusHead",8) && b[8]==1 && (b[9]==1 || b[9]==2) && b[18]==0,"Expected OpusHead v1 mono/stereo mapping 0");info.channels=b[9];info.skip=unsigned(b[10])|(unsigned(b[11])<<8);Check(info.skip<=3840 && info.skip%3==0,"Opus pre-skip outside exact-nanosecond 0..80ms profile");head=b;}
        else {Check(b.size()>=16 && !std::memcmp(b.data(),"OpusTags",8),"Expected OpusTags");size_t at=8;const auto length=[&]{Check(at<=b.size() && b.size()-at>=4,"Truncated OpusTags length");const auto n=LE32(b.data()+at);at+=4;return n;};const auto skip=[&](size_t n){Check(n<=b.size()-at,"OpusTags length exceeds packet");at+=n;};skip(length());const auto count=length();Check(count<=65536,"Too many OpusTags comments");for(uint32_t k=0;k<count;++k)skip(length());memory(Init(info,head));initialized=true;}
    }
    void Project(const Packet& packet,uint64_t before,uint64_t padding){
        const auto clock=N(0xe7,before/24);size_t payload=0;for(const auto& p:packet.pieces)payload+=p.size;
        Bytes tail;if(padding){Bytes n(8);const auto ns=padding*1000000000/48000;for(unsigned k=0;k<8;++k)n[7-k]=uint8_t(ns>>(8*k));tail=E(0x75a2,n);}
        auto block=muxwebm::Header(0xa1,payload+4);muxwebm::Join(block,Bytes{0x81,0,0,0});const auto group=muxwebm::Header(0xa0,block.size()+payload+tail.size());
        auto header=muxwebm::Header(0x1f43b675,clock.size()+group.size()+block.size()+payload+tail.size());for(const auto& b:{clock,group,block})muxwebm::Join(header,b);memory(std::move(header));
        for(const auto& p:packet.pieces){view.spans.push_back({view.bytes,p.size,p.at,{}});view.bytes+=p.size;}if(!tail.empty())memory(std::move(tail));
    }
public:
    Info info;
    bool Initialized()const{return initialized && frames>0;}uint64_t Frames()const{return frames;}uint64_t Samples()const{return eos?info.total:decoded>info.skip?decoded-info.skip:0;}
    void Update(const FragmentIndex::Read& read,uint64_t prefix,bool complete){
        Check(prefix>=scanned,"Ogg prefix regressed");scanned=prefix;
        while(physical<prefix){
            Check(!eos,"Ogg chaining/trailing bytes are outside profile");if(!RangeReady(physical,27,prefix))break;
            uint8_t h[27];Check(read(physical,h,27),"Ogg header read failed");Check(!std::memcmp(h,"OggS",4) && h[4]==0 && !(h[5]&~7) && h[26]>0,"Invalid/unsupported Ogg page header");
            const auto count=h[26];if(!RangeReady(physical+27,count,prefix))break;Bytes page(27+count);std::memcpy(page.data(),h,27);Check(read(physical+27,page.data()+27,count),"Ogg lacing read failed");size_t body=0;for(unsigned k=0;k<count;++k)body+=page[27+k];const auto size=page.size()+body;if(!RangeReady(physical,size,prefix))break;
            page.resize(size);Check(read(physical+27+count,page.data()+27+count,body),"Ogg page body read failed");Check(Crc(page)==LE32(h+22),"Ogg page CRC mismatch");
            Check(pages<250000 && LE32(h+18)==pages,"Ogg page sequence missing/reordered");if(!started){Check(h[5]==2 && LE64(h+6)==0 && count==1 && page[27]==19,"OpusHead must occupy the first BOS page");serial=LE32(h+14);started=true;}else Check(!(h[5]&2) && LE32(h+14)==serial,"Ogg multiplexing/chaining is outside profile");
            Check(bool(h[5]&1)==pending,"Ogg continued packet flag mismatch");const auto firstPackets=packets;std::vector<Packet> audio;size_t at=27+count;
            for(unsigned k=0;k<count;++k){const auto n=page[27+k];Check(partial.data.size()+n<=1048576,"Ogg packet exceeds 1 MiB");if(n){partial.pieces.push_back({physical+at,n});partial.data.insert(partial.data.end(),page.begin()+at,page.begin()+at+n);}at+=n;pending=n==255;
                if(!pending){Check(packets<250002,"Ogg packet count exceeds limit");if(packets<2)HeaderPacket(partial.data);else {partial.samples=Duration(partial.data);decoded+=partial.samples;audio.push_back(std::move(partial));}partial={};++packets;}
            }
            const auto granule=LE64(h+6);const bool last=(h[5]&4)!=0;
            if(firstPackets<2){Check(audio.empty() && !last && physical+size<=1048576,"Ogg headers must finish before audio pages, within 1 MiB");Check(granule==(pending?UINT64_MAX:0),"Invalid Opus header granule");}
            else if(audio.empty())Check(!last && granule==UINT64_MAX,"Ogg page without completed audio must have no granule");
            else {Check(granule!=UINT64_MAX && granule>=lastGranule,"Invalid/regressing Opus granule");uint64_t padding=0;
                if(last){Check(!pending && granule>=info.skip && granule<=decoded && decoded-granule<audio.back().samples,"Invalid Opus EOS granule/end trim");padding=decoded-granule;info.total=granule-info.skip;eos=true;}else Check(granule==decoded,"Nonzero origin/discontinuous Opus granule is outside profile");
                uint64_t before=decoded;for(const auto& p:audio)before-=p.samples;
                for(size_t k=0;k<audio.size();++k){Project(audio[k],before,k+1==audio.size()?padding:0);before+=audio[k].samples;++frames;}lastGranule=granule;
            }
            physical+=size;++pages;
        }
        if(complete)Check(eos && !pending && physical==prefix && Frames()>0,"Complete Ogg requires full EOS without partial packets");
        if(!Initialized()){Check(!complete,"Incomplete Ogg Opus initialization");throw Waiting{};}
    }
};
}}
