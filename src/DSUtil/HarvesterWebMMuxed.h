// Two single-track WebM views of one growing, verified physical resource.
// GPL-3.0-or-later. Original blocks/payload stay in the source file.
#pragma once
#include "HarvesterWebM.h"
#include <array>
#include <memory>
#include <cmath>

namespace harvester { namespace muxwebm {
constexpr uint64_t MaxAppend=16777216,MaxInit=1048576,MaxCluster=16777216;
using Bytes=std::vector<uint8_t>;
using webm::Check;
inline Bytes Size(uint64_t n) {
    for(unsigned w=1;w<=8;++w)if(n<(uint64_t(1)<<(7*w))-1){Bytes b(w);for(unsigned i=0;i<w;++i)b[w-1-i]=uint8_t(n>>(8*i));b[0]|=uint8_t(0x80u>>(w-1));return b;}
    throw std::runtime_error("WebM projected size overflow");
}
inline Bytes Header(uint32_t id,uint64_t n) {
    Bytes out;unsigned w=1;while(w<4 && id>=(uint32_t(1)<<(8*w)))++w;
    for(unsigned i=w;i;--i)out.push_back(uint8_t(id>>((i-1)*8)));const auto size=Size(n);out.insert(out.end(),size.begin(),size.end());return out;
}
inline Bytes Element(uint32_t id,webm::Data d){auto out=Header(id,d.size);if(d.size)out.insert(out.end(),d.bytes,d.bytes+d.size);return out;}
inline void Join(Bytes& a,const Bytes& b){a.insert(a.end(),b.begin(),b.end());}
struct Span {uint64_t offset=0,length=0,physical=0;std::shared_ptr<const Bytes> memory;};
struct View {uint32_t id=0;uint64_t bytes=0;std::vector<Span> spans;WebMIndex index;};
class Layout {
public:
    using Read=WebMRead;
    using Validate=std::function<void(unsigned,const WebMInfo&)>;
    std::array<View,2> tracks;
private:
    Bytes ebml,info;std::array<Bytes,2> entries;
    uint64_t position=0,scanned=0,segmentEnd=UINT64_MAX;
    size_t clusters=0,elements=0;bool started=false,hasInfo=false,hasTracks=false,initialized=false;
    static void Memory(View& view,Bytes bytes) {
        Check(bytes.size()<=uint64_t(INT64_MAX)-view.bytes,"Virtual WebM overflow");const auto n=bytes.size();view.spans.push_back({view.bytes,n,0,std::make_shared<const Bytes>(std::move(bytes))});view.bytes+=n;
    }
    static void Physical(View& view,uint64_t at,uint64_t n) {
        Check(n<=uint64_t(INT64_MAX)-view.bytes,"Virtual WebM overflow");view.spans.push_back({view.bytes,n,at,{}});view.bytes+=n;
    }
    void Info(webm::Data data) {
        Check(!hasInfo && !hasTracks,"Repeated/misplaced WebM Info");
        std::set<uint32_t> unique;
        for(const auto& field:data.Children()) {
            Check(field.id==0xec || unique.insert(field.id).second,"Repeated WebM Info field");
            Check(field.id==0x2ad7b1 || field.id==0x4489 || field.id==0x4d80 || field.id==0x5741 || field.id==0x73a4 || field.id==0x4461 || field.id==0xec || field.id==0xbf,"Linked/unsupported WebM Info field");
            if(field.id==0x4489){const auto d=field.body.Float();Check(std::isfinite(d) && d>0 && d<=double(INT64_MAX)/10000.0,"Invalid WebM Duration");}
        }
        Check(webm::OptionalNumber(data,0x2ad7b1,1000000)==1000000,"Muxed WebM requires 1 ms timestamp scale");info=Element(0x1549a966,data);hasInfo=true;
    }
    void Tracks(webm::Data data) {
        Check(hasInfo && !hasTracks,"Repeated/misplaced WebM Tracks");unsigned count=0;std::array<uint64_t,2> uid{};
        for(const auto& child:data.Children()) {
            if(child.id==0xec || child.id==0xbf)continue;Check(child.id==0xae && ++count<=2,"Expected exactly two WebM TrackEntry elements");
            const auto type=child.body.One(0x83).Number();Check(type==1 || type==2,"Unsupported WebM track kind");const unsigned k=type==1?0:1;
            Check(entries[k].empty(),"Duplicate WebM track kind");const auto id=child.body.One(0xd7).Number();Check(id && id<=UINT32_MAX,"Invalid WebM track number");tracks[k].id=uint32_t(id);
            uid[k]=child.body.One(0x73c5).Number();Check(uid[k],"Invalid WebM TrackUID");
            std::set<uint32_t> unique;
            for(const auto& field:child.body.Children()) {
                const auto f=field.id;
                Check(f==0xec || unique.insert(f).second,"Repeated WebM TrackEntry field");
                Check(f==0xd7 || f==0x73c5 || f==0x83 || f==0xb9 || f==0x88 || f==0x55aa || f==0x9c || f==0x55ee || f==0x23e383 || f==0x536e || f==0x22b59c || f==0x22b59d || f==0x86 || f==0x63a2 || f==0x258688 || f==0x56aa || f==0x56bb || f==0xe0 || f==0xe1 || f==0x23314f || f==0xec || f==0xbf,"External/linked/encrypted/unsupported WebM track field");
                if(f==0x55ee)Check(field.body.Number()==0,"Block additions are outside this profile");
            }
            if(!k){const auto v=child.body.One(0xe0);Check(webm::OptionalNumber(v,0x53c0,0)==0 && webm::OptionalNumber(v,0x9a,0)!=1 && webm::OptionalNumber(v,0x54b2,0)==0,"Alpha/interlaced/non-pixel video is unsupported");}
            entries[k]=Element(0xae,child.body);
        }
        Check(count==2 && !entries[0].empty() && !entries[1].empty() && tracks[0].id!=tracks[1].id && uid[0]!=uid[1],"Expected distinct video/audio TrackNumber and TrackUID");hasTracks=true;
    }
    void Initialize(const Validate& validate) {
        Check(hasInfo && hasTracks && !initialized,"Incomplete/repeated WebM initialization");
        for(unsigned k=0;k<2;++k) {
            Bytes bytes=ebml;Join(bytes,Bytes{0x18,0x53,0x80,0x67,0x01,0xff,0xff,0xff,0xff,0xff,0xff,0xff});Join(bytes,info);Join(bytes,Element(0x1654ae6b,{entries[k].data(),entries[k].size()}));
            const auto config=WebMInitialization([&](uint64_t at,void* out,size_t n){if(!RangeReady(at,n,bytes.size()))return false;if(n)std::memcpy(out,bytes.data()+size_t(at),n);return true;},bytes.size(),int(k),true);
            Check((k?config.codec=="opus":config.codec=="vp8" || config.codec=="vp9" || config.codec=="av1"),"Muxed WebM enables VP8/VP9/AV1 with Opus only");validate(k,config);Memory(tracks[k],std::move(bytes));
        }
        initialized=true;
    }
    unsigned Kind(webm::Data block) const {
        Check(block.size>=4,"Truncated shared WebM block");const auto w=webm::Width(block.bytes[0],8);Check(block.size>w+3,"Empty shared WebM block");
        uint64_t id=block.bytes[0]&((0x80u>>(w-1))-1);for(unsigned i=1;i<w;++i)id=(id<<8)|block.bytes[i];
        Check(id==tracks[0].id || id==tracks[1].id,"WebM block references an undeclared track");return id==tracks[0].id?0:1;
    }
    void Cluster(const Read& read,const webm::Element& e,uint64_t prefix) {
        Check(++clusters<=8192,"Too many shared WebM clusters");const auto raw=webm::Body(read,e,prefix,MaxCluster);const webm::Data data{raw.data(),raw.size(),e.data};
        Bytes clock;std::array<std::vector<std::pair<uint64_t,uint64_t>>,2> selected;std::array<uint64_t,2> sizes{};size_t blocks=0;
        for(const auto& child:data.Children(65536)) {
            if(child.id==0xe7){Check(clock.empty(),"Repeated Cluster timestamp");child.body.Number();clock=Element(child.id,child.body);}
            else if(child.id==0xa3 || child.id==0xa0) {
                Check(!clock.empty(),"Block before Cluster timestamp");Kind(child.id==0xa3?child.body:child.body.One(0xa1));
                // Keep the original complete SimpleBlock/BlockGroup, including
                // variable-width track IDs, references and discard padding.
                ++blocks;
            }else Check(child.id==0xa7 || child.id==0xab || child.id==0xec || child.id==0xbf,"Unsupported/encrypted Cluster field");
        }
        Check(!clock.empty() && blocks,"Cluster lacks timestamp/media blocks");
        for(size_t at=0;at<raw.size();) {
            webm::Element block;Check(webm::Header(raw.data()+at,raw.size()-at,block) && !block.unknown && block.end<=raw.size()-at,"Invalid Cluster child envelope");
            if(block.id==0xa3 || block.id==0xa0) {
                const webm::Data body{raw.data()+at+size_t(block.data),size_t(block.size),e.data+at+block.data};const auto k=Kind(block.id==0xa3?body:body.One(0xa1));
                selected[k].emplace_back(e.data+at,block.end);sizes[k]+=block.end;
            }
            at+=size_t(block.end);
        }
        for(unsigned k=0;k<2;++k) {
            if(selected[k].empty())continue;auto& view=tracks[k];auto header=Header(0x1f43b675,clock.size()+sizes[k]);Join(header,clock);Memory(view,std::move(header));
            for(const auto& part:selected[k])Physical(view,part.first,part.second);
            view.index.Update([&](uint64_t at,void* out,size_t n){return ReadAt(read,k,at,out,n);},view.bytes,int(k));
        }
    }
public:
    bool Initialized() const{return initialized;}
    uint64_t Position() const{return position;}
    size_t Fragments() const{return clusters;}
    bool ReadAt(const Read& read,unsigned k,uint64_t at,void* out,size_t count) const {
        Check(k<2 && (out || !count),"Invalid WebM virtual read");const auto& view=tracks[k];if(!RangeReady(at,count,view.bytes))return false;if(!count)return true;
        auto* bytes=static_cast<uint8_t*>(out);size_t copied=0;auto i=std::upper_bound(view.spans.begin(),view.spans.end(),at,[](uint64_t p,const Span& s){return p<s.offset;});if(i!=view.spans.begin())--i;
        for(;copied<count && i!=view.spans.end();++i){if(at>=i->offset+i->length)continue;Check(at>=i->offset,"WebM virtual layout gap");const auto n=size_t((std::min)(uint64_t(count-copied),i->length-(at-i->offset)));if(i->memory)std::memcpy(bytes+copied,i->memory->data()+size_t(at-i->offset),n);else if(!read(i->physical+at-i->offset,bytes+copied,n))return false;at+=n;copied+=n;}return copied==count;
    }
    void Update(const Read& read,uint64_t prefix,bool complete,const Validate& validate) {
        Check(prefix>=scanned && prefix-scanned<=MaxAppend && prefix<=uint64_t(INT64_MAX),"Shared WebM prefix regressed or append exceeds 16 MiB");scanned=prefix;
        if(!started) {
            webm::Element h;if(!webm::Header(read,0,prefix,h)){Check(!complete,"Truncated final EBML header");return;}Check(h.id==0x1a45dfa3 && !h.unknown && h.size<=4096,"Expected bounded EBML header");if(h.end>prefix){Check(!complete,"Truncated final EBML body");return;}
            const auto body=webm::Body(read,h,prefix,4096);auto head=Element(h.id,{body.data(),body.size(),h.data});webm::Element s;if(!webm::Header(read,h.end,prefix,s)){Check(!complete,"Truncated final Segment header");return;}Check(s.id==0x18538067,"Expected a single WebM Segment");
            ebml=std::move(head);segmentEnd=s.end;position=s.data;started=true;
        }
        while(position<prefix) {
            webm::Element e;if(!webm::Header(read,position,prefix,e))break;Check(!e.unknown && e.end<=segmentEnd,"Unknown-size/oversized Segment child");
            Check(e.size<=(e.id==0x1f43b675?MaxCluster:MaxInit),"Shared WebM element exceeds bound");if(e.end>prefix)break;Check(++elements<=32768,"Too many WebM top-level elements");
            if(e.id==0x1f43b675){Check(initialized,"Cluster before Info/Tracks");Cluster(read,e,prefix);}
            else if(e.id==0x1549a966 || e.id==0x1654ae6b) {
                Check(!initialized && e.end<=MaxInit,"Repeated/oversized WebM initialization");const auto bytes=webm::Body(read,e,prefix,MaxInit);const webm::Data data{bytes.data(),bytes.size(),e.data};if(e.id==0x1549a966)Info(data);else Tracks(data);if(hasInfo && hasTracks)Initialize(validate);
            }else Check(e.id==0x114d9b74 || e.id==0x1c53bb6b || e.id==0x1254c367 || e.id==0xec || e.id==0xbf,"Unsupported shared WebM top-level element");
            position=e.end;
        }
        if(complete)Check(initialized && clusters && position==prefix && (segmentEnd==UINT64_MAX || segmentEnd==prefix),"Truncated/incomplete final shared WebM");
    }
};
}} // namespace harvester::muxwebm
