// Bounded Ogg Vorbis I -> single-track WebM, no re-encoding.
// Ogg framing and Vorbis I encapsulation. GPL-3.0-or-later.
#pragma once
#include "HarvesterOggOpus.h"
namespace harvester { namespace oggvorbis {
using oggopus::Bytes;using oggopus::Piece;using oggopus::Check;using oggopus::LE32;using oggopus::LE64;
using oggopus::E;using oggopus::N;using oggopus::Text;
struct Waiting {};
struct Packet {std::vector<Piece> pieces;Bytes data;unsigned samples=0;};
struct Info {unsigned rate=48000,channels=0;uint64_t total=0;};
inline Bytes Init(const Info& i,const Bytes& config){
    Bytes ebml;for(const auto& b:{N(0x4286,1),N(0x42f7,1),N(0x42f2,4),N(0x42f3,8),Text(0x4282,"webm"),N(0x4287,4),N(0x4285,2)})muxwebm::Join(ebml,b);
    auto out=E(0x1a45dfa3,ebml);muxwebm::Join(out,Bytes{0x18,0x53,0x80,0x67,1,255,255,255,255,255,255,255});
    Bytes info;for(const auto& b:{N(0x2ad7b1,100),Text(0x4d80,"FoxicMP"),Text(0x5741,"FoxicMP")})muxwebm::Join(info,b);muxwebm::Join(out,E(0x1549a966,info));
    auto audio=E(0xb5,Bytes{0x40,0xe7,0x70,0,0,0,0,0});muxwebm::Join(audio,N(0x9f,i.channels));Bytes entry;
    for(const auto& b:{N(0xd7,1),N(0x73c5,1),N(0x83,2),N(0x9c,0),Text(0x86,"A_VORBIS"),E(0x63a2,config),N(0x56aa,0),N(0x56bb,0),E(0xe1,audio)})muxwebm::Join(entry,b);
    muxwebm::Join(out,E(0x1654ae6b,E(0xae,entry)));return out;
}
class Layout:public audiomp4::Projection {
    uint64_t physical=0,scanned=0,decoded=0,frames=0,pages=0,packets=0,lastGranule=0;uint32_t serial=0;
    bool started=false,initialized=false,eos=false,pending=false;Packet partial;std::vector<Bytes> headers;VorbisInfo codec;
    void HeaderPacket(const Bytes& b){
        Check(b.size()>=7 && b[0]==(packets==0?1:packets==1?3:5) && !std::memcmp(b.data()+1,"vorbis",6),"Expected ordered Vorbis identification/comment/setup headers");
        if(packets==0)Check(b.size()==30,"Vorbis identification must have 30 bytes");headers.push_back(b);
        if(packets==2){Bytes config{2};for(unsigned k=0;k<2;++k){size_t n=headers[k].size();while(n>=255){config.push_back(255);n-=255;}config.push_back(uint8_t(n));}
            for(const auto& h:headers)muxwebm::Join(config,h);Check(config.size()<=1048576,"Vorbis headers exceed 1 MiB");codec=VorbisConfiguration(config.data(),config.size());codec.previous=0;info.channels=codec.channels;memory(Init(info,config));headers.clear();initialized=true;}
    }
    unsigned Duration(const Bytes& b){
        using namespace vorbis;Bits bits{b.data(),b.size()};Check(bits.Read(1)==0,"Unexpected Vorbis header in audio packet");const auto mode=bits.Read(ILog(uint32_t(codec.modes.size()-1)));Check(mode<codec.modes.size(),"Invalid Vorbis packet mode");
        const unsigned block=codec.blocks[codec.modes[mode]?1:0],before=codec.previous;
        if(codec.modes[mode]){const auto previous=codec.blocks[bits.Read(1)];bits.Read(1);Check(!before || before==previous,"Vorbis window transition disagrees with previous packet");}
        Check(b.size()*8>bits.at,"Empty Vorbis audio payload");codec.previous=block;return before?(before+block)/4:0;
    }
    void Project(const Packet& packet,uint64_t before,uint64_t padding){
        const auto clock=N(0xe7,before*10000000/48000);size_t payload=0;for(const auto& p:packet.pieces)payload+=p.size;
        Bytes tail;if(padding){Bytes n(8);const auto ns=padding*1000000000/48000;for(unsigned k=0;k<8;++k)n[7-k]=uint8_t(ns>>(8*k));tail=E(0x75a2,n);}
        auto block=muxwebm::Header(0xa1,payload+4);muxwebm::Join(block,Bytes{0x81,0,0,0});const auto group=muxwebm::Header(0xa0,block.size()+payload+tail.size());
        auto header=muxwebm::Header(0x1f43b675,clock.size()+group.size()+block.size()+payload+tail.size());for(const auto& b:{clock,group,block})muxwebm::Join(header,b);memory(std::move(header));
        for(const auto& p:packet.pieces){view.spans.push_back({view.bytes,p.size,p.at,{}});view.bytes+=p.size;}if(!tail.empty())memory(std::move(tail));
    }
public:
    Info info;
    bool Initialized()const{return initialized && frames>1;}uint64_t Frames()const{return frames;}uint64_t Samples()const{return eos?info.total:decoded;}
    void Update(const FragmentIndex::Read& read,uint64_t prefix,bool complete){
        Check(prefix>=scanned,"Ogg prefix regressed");scanned=prefix;
        while(physical<prefix){
            Check(!eos,"Ogg chaining/trailing bytes are outside profile");if(!RangeReady(physical,27,prefix))break;
            uint8_t h[27];Check(read(physical,h,27),"Ogg header read failed");Check(!std::memcmp(h,"OggS",4) && h[4]==0 && !(h[5]&~7) && h[26]>0,"Invalid/unsupported Ogg page header");
            const auto count=h[26];if(!RangeReady(physical+27,count,prefix))break;Bytes page(27+count);std::memcpy(page.data(),h,27);Check(read(physical+27,page.data()+27,count),"Ogg lacing read failed");size_t body=0;for(unsigned k=0;k<count;++k)body+=page[27+k];const auto size=page.size()+body;if(!RangeReady(physical,size,prefix))break;
            page.resize(size);Check(read(physical+27+count,page.data()+27+count,body),"Ogg page body read failed");Check(oggopus::Crc(page)==LE32(h+22),"Ogg page CRC mismatch");
            Check(pages<250000 && LE32(h+18)==pages,"Ogg page sequence missing/reordered");if(!started){Check(h[5]==2 && LE64(h+6)==0 && count==1 && page[27]==30,"Vorbis identification must occupy the first BOS page");serial=LE32(h+14);started=true;}else Check(!(h[5]&2) && LE32(h+14)==serial,"Ogg multiplexing/chaining is outside profile");
            Check(bool(h[5]&1)==pending,"Ogg continued packet flag mismatch");const auto firstPackets=packets;std::vector<Packet> audio;size_t at=27+count;
            for(unsigned k=0;k<count;++k){const auto n=page[27+k];Check(partial.data.size()+n<=1048576,"Ogg packet exceeds 1 MiB");if(n){partial.pieces.push_back({physical+at,n});partial.data.insert(partial.data.end(),page.begin()+at,page.begin()+at+n);}at+=n;pending=n==255;
                if(!pending){Check(packets<250003,"Ogg packet count exceeds limit");if(packets<3){HeaderPacket(partial.data);if(packets==2)Check(k+1==count,"Vorbis setup header must finish its Ogg page");}else {partial.samples=Duration(partial.data);decoded+=partial.samples;audio.push_back(std::move(partial));}partial={};++packets;}
            }
            const auto granule=LE64(h+6);const bool last=(h[5]&4)!=0;
            if(firstPackets<3){Check(audio.empty() && !last && physical+size<=1048576,"Vorbis headers must finish before audio pages, within 1 MiB");Check(granule==0 || (pending && granule==UINT64_MAX),"Invalid Vorbis header granule");}
            else if(audio.empty())Check(!last && granule==UINT64_MAX,"Ogg page without completed audio must have no granule");
            else {Check(granule!=UINT64_MAX && granule>=lastGranule,"Invalid/regressing Vorbis granule");uint64_t padding=0;
                if(last){Check(!pending && granule>0 && granule<=decoded && decoded-granule<audio.back().samples,"Invalid Vorbis EOS granule/end trim");padding=decoded-granule;info.total=granule;eos=true;}else Check(granule==decoded,"Nonzero origin/initial trimming/discontinuous Vorbis granule is outside profile");
                uint64_t before=decoded;for(const auto& p:audio)before-=p.samples;
                for(size_t k=0;k<audio.size();++k){Project(audio[k],before,k+1==audio.size()?padding:0);before+=audio[k].samples;++frames;}lastGranule=granule;
            }
            physical+=size;++pages;
        }
        if(complete)Check(eos && !pending && physical==prefix && Frames()>1,"Complete Ogg requires full EOS without partial packets");
        if(!Initialized()){Check(!complete,"Incomplete Ogg Vorbis initialization");throw Waiting{};}
    }
};
}}
