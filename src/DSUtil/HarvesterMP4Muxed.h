// Two logical single-track fMP4 views over one immutable growing MP4 prefix.
// GPL-3.0-or-later. No file I/O, codecs, transcoding or intermediate media file.
#pragma once
#include "HarvesterMP4.h"
#include <array>

namespace harvester { namespace muxmp4 {
constexpr uint64_t MaxInit=1048576,MaxMoof=1048576,MaxMdat=16777216;
constexpr size_t MaxFragments=8192;
using Bytes=std::vector<uint8_t>;
inline void Check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
inline void U32(Bytes& out,uint32_t value){for(int i=3;i>=0;--i)out.push_back(uint8_t(value>>(i*8)));}
inline void Put32(Bytes& out,size_t at,uint32_t value){Check(at<=out.size() && out.size()-at>=4,"MP4 projection field overflow");for(unsigned i=0;i<4;++i)out[at+i]=uint8_t(value>>((3-i)*8));}
inline Bytes Box(uint32_t type,BoxData body){Check(body.size<=UINT32_MAX-8,"Projected box overflow");Bytes out;out.reserve(body.size+8);U32(out,uint32_t(body.size+8));U32(out,type);if(body.size)out.insert(out.end(),body.data,body.data+body.size);return out;}
inline Bytes Box(const char* type,const Bytes& body){return Box(FourCC(type),{body.data(),body.size()});}
inline void Join(Bytes& to,const Bytes& from){to.insert(to.end(),from.begin(),from.end());}
struct Span {
    uint64_t offset=0,length=0,physical=0;
    std::shared_ptr<const Bytes> memory;
};
struct View {
    uint32_t id=0;uint64_t bytes=0;
    std::vector<Span> spans;FragmentIndex index;
};
class Layout {
public:
    using Read=FragmentIndex::Read;
    using Validate=std::function<void(unsigned,BoxData)>;
    std::array<View,2> tracks;
private:
    struct Atom {uint64_t at=0,size=0;uint32_t type=0;size_t header=0;};
    uint64_t position=0,scanned=0;uint32_t sequence=0;
    size_t fragments=0,atoms=0;bool initialized=false,trailer=false,hevc=false,av1=false,vp9=false,opus=false;
    Bytes ftyp;
    static bool Header(const Read& read,uint64_t prefix,uint64_t at,Atom& a) {
        if(!RangeReady(at,8,prefix))return false;
        uint8_t bytes[16]{};Check(read(at,bytes,8),"Muxed MP4 header I/O failed");BoxData b{bytes,sizeof(bytes)};
        a={at,b.U32(0),b.U32(4),8};
        if(a.size==1){if(!RangeReady(at,16,prefix))return false;Check(read(at+8,bytes+8,8),"Extended MP4 header I/O failed");a.size=b.U64(8);a.header=16;}
        Check(a.size>=a.header && a.size<=uint64_t(INT64_MAX)-at,"Invalid muxed MP4 atom size");return true;
    }
    static Bytes Metadata(const Read& read,const Atom& a,uint64_t bound) {
        Check(a.size<=bound,"Muxed MP4 metadata exceeds bound");Bytes bytes(size_t(a.size-a.header));Check(read(a.at+a.header,bytes.data(),bytes.size()),"Muxed MP4 metadata I/O failed");return bytes;
    }
    static void Memory(View& view,Bytes bytes) {
        Check(bytes.size()<=uint64_t(INT64_MAX)-view.bytes,"MP4 virtual size overflow");const auto n=bytes.size();
        view.spans.push_back({view.bytes,n,0,std::make_shared<const Bytes>(std::move(bytes))});view.bytes+=n;
    }
    void Initialize(BoxData movie,const Validate& validate) {
        const auto mvhd=movie.One("mvhd"),mvex=movie.One("mvex");std::array<BoxData,2> selected{};
        size_t count=0;
        for(const auto& child:movie.Children()) {
            Check(child.first!=FourCC("pssh"),"Encrypted muxed MP4 is unsupported");
            if(child.first!=FourCC("trak"))continue;
            Check(++count<=2,"Muxed MP4 requires exactly two tracks");
            const auto track=child.second,handler=track.One("mdia").One("hdlr"),tkhd=track.One("tkhd");
            Check(!track.One("tref",true).data,"Cross-track references are outside muxed profile");
            const auto minf=track.One("mdia").One("minf"),dref=minf.One("dinf").One("dref"),stbl=minf.One("stbl");
            Check(dref.U32(0)==0 && dref.U32(4)==1,"Expected one self-contained data reference");
            const auto refs=dref.Slice(8,dref.size-8).Children();
            Check(refs.size()==1 && refs[0].first==FourCC("url ") && refs[0].second.size==4 && refs[0].second.U32(0)==1,"External MP4 data references are forbidden");
            for(const auto& table:stbl.Children()) {
                if(table.first==FourCC("stts") || table.first==FourCC("stsc") || table.first==FourCC("stco") || table.first==FourCC("co64") || table.first==FourCC("ctts") || table.first==FourCC("stss"))
                    Check(table.second.size==8 && table.second.U32(0)==0 && table.second.U32(4)==0,"Nonempty nonfragmented sample table");
                else if(table.first==FourCC("sdtp"))Check(table.second.size==4 && table.second.U32(0)==0,"Nonempty/invalid nonfragmented sample-dependency table");
                else if(table.first==FourCC("stsz"))Check(table.second.size==12 && table.second.U32(0)==0 && table.second.U32(8)==0,"Nonempty sample-size table");
                else Check(table.first==FourCC("stsd") || table.first==FourCC("sgpd") || table.first==FourCC("sbgp"),"Unsupported/encrypted sample table");
            }
            const auto stsd=stbl.One("stsd");Check(stsd.U32(0)==0 && stsd.U32(4)==1,"Expected one sample description");
            const auto entries=stsd.Slice(8,stsd.size-8).Children();Check(entries.size()==1 && entries[0].second.U32(4)==1,"External/invalid sample data reference");
            Check((tkhd.U32(0)>>24)<=1,"Unsupported tkhd version");
            const auto type=handler.U32(8);Check(type==FourCC("vide") || type==FourCC("soun"),"Unsupported muxed MP4 track kind");
            const unsigned k=type==FourCC("vide")?0:1;Check(!selected[k].data,"Duplicate muxed MP4 track kind");selected[k]=track;
            if(k==0){hevc=entries[0].first==FourCC("hvc1") || entries[0].first==FourCC("hev1");av1=entries[0].first==FourCC("av01");vp9=entries[0].first==FourCC("vp09");}
            else opus=entries[0].first==FourCC("Opus");
            tracks[k].id=tkhd.U32(tkhd.data[0]?20:12);Check(tracks[k].id!=0,"Zero MP4 track ID");
        }
        Check(count==2 && selected[0].data && selected[1].data && tracks[0].id!=tracks[1].id,"Expected distinct video/audio tracks");
        std::array<BoxData,2> defaults{};size_t trexCount=0;
        for(const auto& child:mvex.Children()) {
            Check(child.first==FourCC("trex") || child.first==FourCC("mehd"),"Unsupported mvex child");
            if(child.first!=FourCC("trex"))continue;
            ++trexCount;const auto id=child.second.U32(4);Check(id==tracks[0].id || id==tracks[1].id,"Unknown trex track");
            const unsigned k=id==tracks[0].id?0:1;Check(!defaults[k].data,"Duplicate trex");defaults[k]=child.second;
        }
        Check(trexCount==2 && defaults[0].data && defaults[1].data,"Expected two trex defaults");
        for(unsigned k=0;k<2;++k) {
            Bytes body=Box(FourCC("mvhd"),mvhd);Join(body,Box(FourCC("trak"),selected[k]));
            Join(body,Box("mvex",Box(FourCC("trex"),defaults[k])));
            validate(k,{body.data(),body.size()});Bytes bytes=ftyp;Join(bytes,Box("moov",body));Memory(tracks[k],std::move(bytes));
        }
        initialized=true;
    }
    static Bytes Project(BoxData moof,uint32_t id,uint64_t originalSize) {
        const auto mfhd=moof.One("mfhd");BoxData selected;size_t count=0;
        for(const auto& child:moof.Children()) {
            Check(child.first==FourCC("mfhd") || child.first==FourCC("traf"),"Unsupported moof child");
            if(child.first==FourCC("traf")){++count;if(child.second.One("tfhd").U32(4)==id){Check(!selected.data,"Duplicate fragment track");selected=child.second;}}
        }
        Check(count==2 && selected.data,"Fragment requires both logical tracks");
        const auto tfhd=selected.One("tfhd");selected.One("tfdt");
        Check((tfhd.U32(0)>>24)==0 && (tfhd.U32(0)&0x020000) && !(tfhd.U32(0)&1),"Muxed fragment requires default-base-is-moof without explicit base");
        Bytes traf;size_t runs=0;
        // Rebuild once to determine the canonical projected moof length.
        for(const auto& child:selected.Children()) {
            Check(child.first==FourCC("tfhd") || child.first==FourCC("tfdt") || child.first==FourCC("trun"),"Unsupported/encrypted traf child");
            if(child.first==FourCC("trun")){Check(runs++ || (child.second.U32(0)&1),"First muxed trun requires a data offset");}
            Join(traf,Box(child.first,child.second));
        }
        Check(runs!=0,"Empty muxed fragment track");Bytes body=Box(FourCC("mfhd"),mfhd);Join(body,Box("traf",traf));
        const auto size=body.size()+8;traf.clear();
        for(const auto& child:selected.Children()) {
            Bytes bytes(child.second.data,child.second.data+child.second.size);
            if(child.first==FourCC("trun") && (child.second.U32(0)&1)) {
                const int64_t offset=int32_t(child.second.U32(8));
                Check(offset>=int64_t(originalSize) && uint64_t(offset)-originalSize+size<=INT32_MAX,"Invalid projected data offset");
                Put32(bytes,8,uint32_t(uint64_t(offset)-originalSize+size));
            }
            Join(traf,Box(child.first,{bytes.data(),bytes.size()}));
        }
        body=Box(FourCC("mfhd"),mfhd);Join(body,Box("traf",traf));return Box("moof",body);
    }
    void Fragment(const Read& read,const Atom& moof,const Atom& mdat) {
        Check(++fragments<=MaxFragments && mdat.size<=MaxMdat,"Muxed fragment count/size exceeds bound");
        const auto bytes=Metadata(read,moof,MaxMoof);const BoxData body{bytes.data(),bytes.size()};const auto mfhd=body.One("mfhd");
        Check(mfhd.size==8 && mfhd.U32(0)==0,"Invalid muxed mfhd");const auto next=mfhd.U32(4);
        Check(next && (!sequence || (sequence<UINT32_MAX && next==sequence+1)),"Fragment sequence gap/rollover");
        std::array<uint32_t,2> ids{};size_t count=0;
        for(const auto& child:body.Children())if(child.first==FourCC("traf")){Check(count<2,"Extra fragment track");ids[count++]=child.second.One("tfhd").U32(4);}
        Check(count==2 && ids[0]!=ids[1] && (ids[0]==tracks[0].id || ids[1]==tracks[0].id) && (ids[0]==tracks[1].id || ids[1]==tracks[1].id),"Fragment track IDs differ from initialization");
        std::vector<std::pair<uint64_t,uint64_t>> ranges;
        for(unsigned k=0;k<2;++k) {
            auto& view=tracks[k];const auto old=view.index.Samples().size();Memory(view,Project(body,view.id,moof.size));const auto start=view.bytes;
            Check(mdat.size<=uint64_t(INT64_MAX)-view.bytes,"MP4 virtual size overflow");view.spans.push_back({view.bytes,mdat.size,mdat.at,{}});view.bytes+=mdat.size;
            view.index.Update([&](uint64_t at,void* out,size_t n){return ReadAt(read,k,at,out,n);},view.bytes,int(k),false,0,hevc,av1,vp9,opus);
            const auto& samples=view.index.Samples();Check(samples.size()>old,"Fragment lacks one logical track");
            for(size_t i=old;i<samples.size();++i) {
                const auto& sample=samples[i];Check(sample.end>sample.start && sample.offset>=start+mdat.header && RangeReady(sample.offset,sample.size,view.bytes),"Invalid muxed sample envelope");
                if(i){const auto& previous=samples[i-1];const auto expected=AddTime(previous.decode,previous.end-previous.start);const auto delta=sample.decode>=expected?uint64_t(sample.decode)-uint64_t(expected):uint64_t(expected)-uint64_t(sample.decode);Check(delta<=1,"Muxed decode cadence has a gap/overlap");}
                const auto physical=mdat.at+sample.offset-start;ranges.emplace_back(physical,physical+sample.size);
            }
        }
        std::sort(ranges.begin(),ranges.end());auto cursor=mdat.at+mdat.header;
        for(const auto& range:ranges){Check(range.first==cursor && range.second>cursor,"Overlapping/unreferenced muxed payload");cursor=range.second;}
        Check(cursor==mdat.at+mdat.size,"Unreferenced muxed payload tail");sequence=next;
    }
public:
    bool Initialized() const{return initialized;}
    uint64_t Position() const{return position;}
    size_t Fragments() const{return fragments;}
    bool ReadAt(const Read& read,unsigned k,uint64_t at,void* out,size_t count) const {
        Check(k<2 && (out || !count),"Invalid virtual MP4 read");const auto& view=tracks[k];if(!RangeReady(at,count,view.bytes))return false;
        if(!count)return true;auto* bytes=static_cast<uint8_t*>(out);size_t copied=0;
        auto i=std::upper_bound(view.spans.begin(),view.spans.end(),at,[](uint64_t p,const Span& span){return p<span.offset;});if(i!=view.spans.begin())--i;
        for(;copied<count && i!=view.spans.end();++i) {
            if(at>=i->offset+i->length)continue;Check(at>=i->offset,"Virtual MP4 layout gap");const auto n=size_t((std::min)(uint64_t(count-copied),i->length-(at-i->offset)));
            if(i->memory)std::memcpy(bytes+copied,i->memory->data()+size_t(at-i->offset),n);
            else if(!read(i->physical+at-i->offset,bytes+copied,n))return false;
            copied+=n;at+=n;
        }
        return copied==count;
    }
    void Update(const Read& read,uint64_t prefix,bool complete,const Validate& validate) {
        Check(prefix>=scanned && prefix-scanned<=MaxMdat && prefix<=uint64_t(INT64_MAX),"Muxed append exceeds 16 MiB or prefix regressed");scanned=prefix;
        while(position<prefix) {
            Atom a;if(!Header(read,prefix,position,a))break;
            if(a.type==FourCC("mdat"))Check(false,"Orphan muxed mdat");
            if(a.type==FourCC("moof")) {
                Check(initialized && !trailer,"Fragment before init/after trailer");Check(a.size<=MaxMoof,"Muxed moof exceeds bound");if(!RangeReady(a.at,a.size,prefix))break;
                Atom m;if(!Header(read,prefix,a.at+a.size,m))break;Check(m.type==FourCC("mdat") && m.size<=MaxMdat,"Expected bounded mdat after moof");if(!RangeReady(m.at,m.size,prefix))break;
                Check(atoms<=32768-2,"Too many muxed top-level atoms");atoms+=2;Fragment(read,a,m);position=m.at+m.size;continue;
            }
            Check(a.size<=MaxInit,"Muxed initialization/trailer exceeds bound");if(!RangeReady(a.at,a.size,prefix))break;
            Check(++atoms<=32768,"Too many muxed top-level atoms");
            if(a.type==FourCC("ftyp")){Check(position==0 && ftyp.empty() && a.size>=16,"Invalid ftyp placement");const auto body=Metadata(read,a,MaxInit);ftyp=Box(a.type,{body.data(),body.size()});}
            else if(a.type==FourCC("moov")){Check(!initialized && !ftyp.empty() && a.at+a.size<=MaxInit,"Invalid/oversized head moov");const auto bytes=Metadata(read,a,MaxInit);Initialize({bytes.data(),bytes.size()},validate);}
            else if(a.type==FourCC("mfra")){Check(initialized && fragments,"Trailer before media");trailer=true;}
            else Check(a.type==FourCC("free") && !trailer,"Unsupported muxed top-level atom");
            position+=a.size;
        }
        if(complete)Check(position==prefix && initialized && fragments,"Truncated/empty completed muxed MP4");
    }
};
}} // namespace harvester::muxmp4
