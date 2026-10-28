// Bounded Vorbis I header/mode parser for progressive WebM. GPL-3.0-or-later.
// Validates framing and setup references; residue decoding stays in the backend.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <array>
#include <set>
namespace harvester {
namespace vorbis {
inline void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
inline unsigned ILog(uint32_t n){unsigned bits=0;while(n){++bits;n>>=1;}return bits;}
struct Bits {
    const uint8_t* data;size_t size,at=0;
    uint32_t Read(unsigned n){Check(n<=32 && at<=size*8 && n<=size*8-at,"Truncated Vorbis setup/packet");uint32_t v=0;for(unsigned i=0;i<n;++i,++at)v|=uint32_t((data[at/8]>>(at%8))&1)<<i;return v;}
    void Skip(uint64_t n){Check(at<=size*8 && n<=size*8-at,"Vorbis lookup exceeds header");at+=size_t(n);}
    void End(){Check(size*8-at<8,"Trailing Vorbis setup bytes");while(at<size*8)Check(Read(1)==0,"Nonzero Vorbis header padding");}
};
struct Bytes {const uint8_t* data;size_t size;};
inline uint32_t LE(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline void Signature(Bytes b,uint8_t type){Check(b.size>=7 && b.data[0]==type && !std::memcmp(b.data+1,"vorbis",6),"Invalid Vorbis header signature");}
inline bool PowerAtMost(uint32_t base,unsigned power,uint32_t maximum){uint64_t value=1;for(unsigned i=0;i<power;++i){value*=base;if(value>maximum)return false;}return true;}
inline uint32_t LookupValues(uint32_t entries,unsigned dimensions){uint32_t lo=1,hi=entries;while(lo<hi){const auto mid=lo+(hi-lo+1)/2;if(PowerAtMost(mid,dimensions,entries))lo=mid;else hi=mid-1;}return lo;}
} // namespace vorbis
struct VorbisInfo {
    unsigned channels=0,rate=0,blocks[2]={0,0},previous=0;
    std::vector<bool> modes;
    int64_t Duration(const uint8_t* bytes,size_t size) {
        using namespace vorbis;Bits b{bytes,size};Check(b.Read(1)==0,"Unexpected Vorbis header in audio packet");
        const auto mode=b.Read(ILog(uint32_t(modes.size()-1)));Check(mode<modes.size(),"Invalid Vorbis packet mode");
        const auto current=blocks[modes[mode]?1:0];auto before=previous;
        if(modes[mode]){before=blocks[b.Read(1)];b.Read(1);}previous=current;
        Check(size*8>b.at,"Empty Vorbis audio payload");return int64_t((before+current)/4)*1000000000/rate;
    }
};
inline VorbisInfo VorbisConfiguration(const uint8_t* bytes,size_t size) {
    using namespace vorbis;Check(size>=4 && size<=1048576 && bytes[0]==2,"Invalid Vorbis Xiph header count");
    size_t at=1,lengths[2]={0,0};
    for(auto& length:lengths){uint8_t n;do{Check(at<size,"Truncated Vorbis Xiph lengths");n=bytes[at++];length+=n;}while(n==255);Check(length>0,"Empty Vorbis header");}
    Check(lengths[0]<=size-at && lengths[1]<=size-at-lengths[0],"Vorbis Xiph lengths exceed CodecPrivate");
    const Bytes id{bytes+at,lengths[0]},comment{bytes+at+lengths[0],lengths[1]},setup{bytes+at+lengths[0]+lengths[1],size-at-lengths[0]-lengths[1]};
    Signature(id,1);Signature(comment,3);Signature(setup,5);Check(id.size==30 && LE(id.data+7)==0 && id.data[29]==1,"Unsupported Vorbis identification/version");
    VorbisInfo out;out.channels=id.data[11];out.rate=LE(id.data+12);
    Check((out.channels==1 || out.channels==2) && out.rate==48000,"Only Vorbis mono/stereo 48 kHz is enabled");
    const unsigned smallBlock=id.data[28]&15,largeBlock=id.data[28]>>4;
    Check(smallBlock>=6 && largeBlock<=13 && smallBlock<=largeBlock,"Invalid Vorbis block sizes");out.blocks[0]=1u<<smallBlock;out.blocks[1]=1u<<largeBlock;
    size_t c=7;
    auto length=[&](){Check(c<=comment.size && comment.size-c>=4,"Truncated Vorbis comment length");const auto n=LE(comment.data+c);c+=4;return n;};
    auto skip=[&](size_t n){Check(c<=comment.size && n<=comment.size-c,"Vorbis comment exceeds header");c+=n;};
    skip(length());const auto comments=length();Check(comments<=65536,"Too many Vorbis comments");for(uint32_t i=0;i<comments;++i)skip(length());
    Check(c+1==comment.size && comment.data[c]==1,"Invalid Vorbis comment framing");
    Bits b{setup.data+7,setup.size-7};const auto books=b.Read(8)+1;
    for(unsigned book=0;book<books;++book){
        Check(b.Read(24)==0x564342,"Invalid Vorbis codebook sync");const auto dim=b.Read(16),entries=b.Read(24);
        Check(dim>0 && dim<=16 && entries>0 && entries<=65536,"Vorbis codebook exceeds bounded profile");
        std::array<uint32_t,33> counts{};
        if(b.Read(1)){
            unsigned length=b.Read(5)+1;uint32_t done=0;
            while(done<entries){Check(length<=32,"Invalid ordered Vorbis codebook");const auto n=b.Read(ILog(entries-done));Check(n<=entries-done,"Vorbis codebook run overflow");counts[length++]=n;done+=n;}
        }else{const bool sparse=b.Read(1)!=0;for(uint32_t i=0;i<entries;++i)if(!sparse || b.Read(1))++counts[b.Read(5)+1];}
        uint64_t leafSlots=1;uint32_t used=0;for(unsigned length=1;length<=32;++length){leafSlots*=2;Check(counts[length]<=leafSlots,"Oversubscribed Vorbis codebook");leafSlots-=counts[length];used+=counts[length];}Check(used>0,"Empty Vorbis codebook");
        const auto lookup=b.Read(4);Check(lookup<=2,"Unsupported Vorbis lookup type");
        if(lookup){b.Skip(64);const auto valueBits=b.Read(4)+1;b.Read(1);const uint64_t values=lookup==1?LookupValues(entries,dim):uint64_t(entries)*dim;b.Skip(values*valueBits);}
    }
    for(unsigned n=b.Read(6)+1;n;--n)Check(b.Read(16)==0,"Unsupported Vorbis time transform");
    const auto floors=b.Read(6)+1;
    for(unsigned f=0;f<floors;++f){
        Check(b.Read(16)==1,"Only Vorbis floor 1 is enabled in this profile");const auto partitions=b.Read(5);std::vector<unsigned> classes;int maximum=-1;
        for(unsigned p=0;p<partitions;++p){const auto k=b.Read(4);classes.push_back(k);if(int(k)>maximum)maximum=int(k);}
        unsigned dimensions[16]{};
        for(int k=0;k<=maximum;++k){dimensions[k]=b.Read(3)+1;const auto sub=b.Read(2);if(sub)Check(b.Read(8)<books,"Invalid Vorbis floor masterbook");for(unsigned n=0;n<(1u<<sub);++n){const auto ref=b.Read(8);Check(!ref || ref-1<books,"Invalid Vorbis floor subclass book");}}
        b.Read(2);const auto range=b.Read(4);Check(!partitions || range>0,"Invalid Vorbis floor range");std::set<uint32_t> points{0,1u<<range};
        for(const auto k:classes)for(unsigned n=0;n<dimensions[k];++n)Check(points.insert(b.Read(range)).second,"Repeated Vorbis floor point");
    }
    const auto residues=b.Read(6)+1;
    for(unsigned r=0;r<residues;++r){Check(b.Read(16)<=2,"Invalid Vorbis residue type");const auto begin=b.Read(24),end=b.Read(24);b.Read(24);Check(begin<=end,"Invalid Vorbis residue bounds");const auto classes=b.Read(6)+1;Check(b.Read(8)<books,"Invalid Vorbis residue classbook");std::vector<unsigned> cascades;
        for(unsigned k=0;k<classes;++k){auto mask=b.Read(3);if(b.Read(1))mask|=b.Read(5)<<3;cascades.push_back(mask);}
        for(auto mask:cascades)for(unsigned k=0;k<8;++k)if(mask&(1u<<k))Check(b.Read(8)<books,"Invalid Vorbis residue book");
    }
    const auto mappings=b.Read(6)+1;
    for(unsigned m=0;m<mappings;++m){Check(b.Read(16)==0,"Unsupported Vorbis mapping");const auto submaps=b.Read(1)?b.Read(4)+1:1;
        if(b.Read(1)){const auto steps=b.Read(8)+1;Check(out.channels==2,"Vorbis coupling requires stereo");for(unsigned s=0;s<steps;++s){const auto magnitude=b.Read(1),angle=b.Read(1);Check(magnitude!=angle,"Invalid Vorbis coupling pair");}}
        Check(b.Read(2)==0,"Nonzero Vorbis mapping reserved bits");if(submaps>1)for(unsigned ch=0;ch<out.channels;++ch)Check(b.Read(4)<submaps,"Invalid Vorbis channel mux");
        for(unsigned s=0;s<submaps;++s){Check(b.Read(8)==0,"Unsupported Vorbis submap time");Check(b.Read(8)<floors,"Invalid Vorbis submap floor");Check(b.Read(8)<residues,"Invalid Vorbis submap residue");}
    }
    for(unsigned n=b.Read(6)+1;n;--n){out.modes.push_back(b.Read(1)!=0);Check(b.Read(16)==0 && b.Read(16)==0,"Unsupported Vorbis mode transform/window");Check(b.Read(8)<mappings,"Invalid Vorbis mode mapping");}
    Check(b.Read(1)==1,"Missing Vorbis setup framing bit");b.End();out.previous=out.blocks[out.modes[0]?1:0];return out;
}
} // namespace harvester
