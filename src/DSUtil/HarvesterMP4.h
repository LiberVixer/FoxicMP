// Bounded single-track fragmented MP4 index. GPL-3.0-or-later.
#pragma once
#include "HarvesterState.h"
#include <functional>

namespace harvester {
inline uint32_t FourCC(const char* s) {return uint32_t(uint8_t(s[0]))<<24 | uint32_t(uint8_t(s[1]))<<16 | uint32_t(uint8_t(s[2]))<<8 | uint8_t(s[3]);}
struct BoxData {
    const uint8_t* data=nullptr; size_t size=0;
    uint32_t U32(size_t p) const {if(p>size || size-p<4)throw std::runtime_error("Truncated MP4 field");return uint32_t(data[p])<<24 | uint32_t(data[p+1])<<16 | uint32_t(data[p+2])<<8 | data[p+3];}
    uint64_t U64(size_t p) const {return uint64_t(U32(p))<<32 | U32(p+4);}
    BoxData Slice(size_t p,size_t n) const {if(p>size || n>size-p)throw std::runtime_error("Invalid MP4 box range");return {data+p,n};}
    std::vector<std::pair<uint32_t,BoxData>> Children() const {
        std::vector<std::pair<uint32_t,BoxData>> boxes;
        for(size_t p=0;p<size;) {
            uint64_t n=U32(p);size_t h=8;const auto kind=U32(p+4);
            if(n==1){n=U64(p+8);h=16;}
            if(n<h || n>size-p || boxes.size()>=128)throw std::runtime_error("Invalid/excessive MP4 boxes");
            boxes.emplace_back(kind,Slice(p+h,size_t(n-h)));p+=size_t(n);
        }
        return boxes;
    }
    BoxData One(const char* name,bool optional=false) const {
        BoxData found; unsigned n=0;
        for(const auto& b:Children())if(b.first==FourCC(name)){found=b.second;++n;}
        if(n>1 || (!n && !optional))throw std::runtime_error("Missing/duplicate MP4 box");return found;
    }
};
// Every scanned atom and sample is bounded by the writer-confirmed prefix.
// Index updates are atomic per whole moof/mdat, so incomplete fragments stay invisible.
class FragmentIndex {
    uint64_t position=0;uint32_t id=0,scale=0,defaultDuration=0,defaultSize=0,defaultFlags=0;
    int64_t edit=0,lastDecode=INT64_MIN, origin=0;
    bool initialized=false,first=true;
    int kind=0;
    uint64_t scannedPrefix=0;
    std::vector<Sample> samples;
public:
    using Read=std::function<bool(uint64_t,void*,size_t)>;
    uint64_t Position() const {return position;}
    const std::vector<Sample>& Samples() const {return samples;}
    bool Initialized() const {return initialized;}
    void Update(const Read& read,uint64_t prefix,int trackKind,bool complete=false,int64_t timestampOffset=0) {
        if(prefix<scannedPrefix)throw std::runtime_error("Fragment prefix regressed");
        scannedPrefix=prefix;kind=trackKind;origin=timestampOffset;
        auto header=[&](uint64_t at,uint64_t& n,uint32_t& type,size_t& h) {
            uint8_t bytes[16]={};if(!RangeReady(at,8,prefix))return false;
            if(!read(at,bytes,8))throw std::runtime_error("MP4 header I/O failed");
            BoxData b{bytes,16};n=b.U32(0);type=b.U32(4);h=8;
            if(n==1){if(!RangeReady(at,16,prefix))return false;if(!read(at+8,bytes+8,8))throw std::runtime_error("MP4 header I/O failed");n=b.U64(8);h=16;}
            if(n<h || n>uint64_t(INT64_MAX)-at)throw std::runtime_error("Invalid fragment box size");return true;
        };
        auto body=[&](uint64_t at,uint64_t n,size_t h) {
            if(n>64*1024*1024)throw std::runtime_error("Oversized MP4 metadata");
            std::vector<uint8_t> bytes(size_t(n-h));if(!read(at+h,bytes.data(),bytes.size()))throw std::runtime_error("MP4 metadata I/O failed");return bytes;
        };
        while(position<prefix) {
            uint64_t n=0;uint32_t type=0;size_t h=0;
            if(!header(position,n,type,h) || !RangeReady(position,n,prefix))break;
            if(type==FourCC("moov")) {
                if(initialized || position==0)throw std::runtime_error("Repeated/misplaced fragmented moov");
                auto bytes=body(position,n,h);Init({bytes.data(),bytes.size()});
            } else if(type==FourCC("moof")) {
                if(!initialized)throw std::runtime_error("Fragment before initialization");
                uint64_t payloadSize=0;uint32_t payloadType=0;size_t payloadHeader=0;
                const auto next=position+n;
                if(!header(next,payloadSize,payloadType,payloadHeader) || !RangeReady(next,payloadSize,prefix))break;
                if(payloadType!=FourCC("mdat"))throw std::runtime_error("Fragment must be followed by mdat");
                auto bytes=body(position,n,h);
                Append({bytes.data(),bytes.size()},position,next+payloadHeader,next+payloadSize);
                position=next+payloadSize;continue;
            } else if(type==FourCC("mdat"))throw std::runtime_error("Unindexed fragmented payload");
            else if(type==FourCC("ftyp") && position!=0)throw std::runtime_error("Repeated MP4 initialization");
            position+=n;
        }
        if(complete && position!=prefix)throw std::runtime_error("Truncated final MP4 fragment");
    }
private:
    void Init(BoxData movie) {
        auto track=movie.One("trak"),media=track.One("mdia"),mdhd=media.One("mdhd");
        auto tkhd=track.One("tkhd"),mvhd=movie.One("mvhd"),handler=media.One("hdlr");
        if((mdhd.U32(0)>>24)>1 || (tkhd.U32(0)>>24)>1 || (mvhd.U32(0)>>24)>1)throw std::runtime_error("Unsupported MP4 clock version");
        scale=mdhd.U32(mdhd.data[0]?20:12);id=tkhd.U32(tkhd.data[0]?20:12);
        const auto movieScale=mvhd.U32(mvhd.data[0]?20:12);
        if(!scale || !movieScale || !id || handler.U32(8)!=FourCC(kind?"soun":"vide"))throw std::runtime_error("Invalid fragment track clock/type");
        auto stsd=media.One("minf").One("stbl").One("stsd");
        if(stsd.U32(4)!=1)throw std::runtime_error("Fragment sample-description changes unsupported");
        stsd.Slice(8,stsd.size-8).One(kind?"mp4a":"avc1");
        auto trex=movie.One("mvex").One("trex");
        if(trex.size!=24 || trex.U32(4)!=id || trex.U32(8)!=1)throw std::runtime_error("Invalid fragmented defaults");
        defaultDuration=trex.U32(12);defaultSize=trex.U32(16);defaultFlags=trex.U32(20);
        auto edts=track.One("edts",true);
        if(edts.data) {
            auto elst=edts.One("elst");const auto version=elst.U32(0)>>24;const auto count=elst.U32(4);
            const auto width=version?20u:12u;
            if(version>1 || count<1 || count>2 || elst.size!=8+width*count)throw std::runtime_error("Unsupported fragmented edit list");
            int64_t delay=0;bool mediaEdit=false;
            for(uint32_t i=0;i<count;++i) {
                const size_t p=8+i*width;
                const uint64_t duration=version?elst.U64(p):elst.U32(p);
                const int64_t mediaTime=version?int64_t(elst.U64(p+8)):int32_t(elst.U32(p+4));
                if(elst.U32(p+(version?16:8))!=0x00010000)throw std::runtime_error("Non-unit MP4 edit rate");
                if(mediaTime==-1 && !mediaEdit) {
                    if(duration>uint64_t(INT64_MAX))throw std::runtime_error("Edit overflow");
                    // Integer rescale, checked through the existing signed clock conversion.
                    delay=AddTime(delay,Time100ns(int64_t(duration),movieScale));
                } else if(mediaTime>=0 && !mediaEdit) {edit=AddTime(delay,-Time100ns(mediaTime,scale));mediaEdit=true;}
                else throw std::runtime_error("Unsupported fragmented edit sequence");
            }
            if(!mediaEdit)throw std::runtime_error("Empty MP4 media edit");
        }
        edit=AddTime(edit,-origin);
        initialized=true;
    }
    void Append(BoxData moof,uint64_t moofOffset,uint64_t payloadStart,uint64_t payloadEnd) {
        moof.One("mfhd");auto traf=moof.One("traf");
        if(traf.One("senc",true).data || traf.One("saiz",true).data || traf.One("saio",true).data)throw std::runtime_error("Encrypted fragments unsupported");
        auto tfhd=traf.One("tfhd"),tfdt=traf.One("tfdt");
        const auto flags=tfhd.U32(0)&0xffffff;
        if(tfhd.U32(4)!=id || flags&0x010000 || flags&~0x03003b)throw std::runtime_error("Invalid fragment track flags");
        uint64_t base=moofOffset;size_t p=8;uint32_t duration=defaultDuration,size=defaultSize,sampleFlags=defaultFlags;
        if(flags&1){base=tfhd.U64(p);p+=8;}
        if(flags&2){if(tfhd.U32(p)!=1)throw std::runtime_error("Changed fragment description");p+=4;}
        if(flags&8){duration=tfhd.U32(p);p+=4;}
        if(flags&16){size=tfhd.U32(p);p+=4;}
        if(flags&32){sampleFlags=tfhd.U32(p);p+=4;}
        if(p!=tfhd.size)throw std::runtime_error("Invalid tfhd length");
        const auto version=tfdt.U32(0)>>24;
        if(version>1 || tfdt.size!=(version?12:8))throw std::runtime_error("Invalid tfdt");
        uint64_t decode=version?tfdt.U64(4):tfdt.U32(4);
        if(decode>uint64_t(INT64_MAX))throw std::runtime_error("Decode clock overflow");
        const auto firstDecode=AddTime(Time100ns(int64_t(decode),scale),edit);
        if(lastDecode!=INT64_MIN && firstDecode<lastDecode)throw std::runtime_error("Fragment decode time regressed");
        uint64_t offset=payloadStart;std::vector<Sample> append;
        for(const auto& child:traf.Children())if(child.first==FourCC("trun")) {
            auto trun=child.second;const auto vf=trun.U32(0),f=vf&0xffffff,ver=vf>>24,count=trun.U32(4);
            if(ver>1 || f&~0x000f05 || ((f&4)&&(f&0x400)) || !count || count>2000000-samples.size()-append.size())throw std::runtime_error("Invalid trun entries/flags");
            size_t q=8;
            if(f&1){const auto delta=int32_t(trun.U32(q));q+=4;if(base>uint64_t(INT64_MAX))throw std::runtime_error("Offset overflow");const auto value=AddTime(int64_t(base),delta);if(value<0)throw std::runtime_error("Negative packet offset");offset=uint64_t(value);}
            uint32_t firstFlags=sampleFlags;if(f&4){firstFlags=trun.U32(q);q+=4;}
            const size_t width=4*((f&0x100?1:0)+(f&0x200?1:0)+(f&0x400?1:0)+(f&0x800?1:0));
            if(q>trun.size || (width && count>(trun.size-q)/width) || trun.size-q!=size_t(count)*width)throw std::runtime_error("Truncated trun");
            for(uint32_t i=0;i<count;++i) {
                const auto d=f&0x100?trun.U32(q):duration;q+=f&0x100?4:0;
                const auto n=f&0x200?trun.U32(q):size;q+=f&0x200?4:0;
                const auto sf=f&0x400?trun.U32(q):(i?sampleFlags:firstFlags);q+=f&0x400?4:0;
                int64_t cts=0;if(f&0x800){cts=ver?int32_t(trun.U32(q)):int64_t(trun.U32(q));q+=4;}
                if(!n || n>64*1024*1024 || offset<payloadStart || !RangeReady(offset,n,payloadEnd))throw std::runtime_error("Sample outside confirmed fragment");
                Sample s;s.offset=offset;s.size=n;s.sync=!(sf&0x10000);
                if(decode>uint64_t(INT64_MAX)-d)throw std::runtime_error("Fragment duration overflow");
                s.decode=AddTime(Time100ns(int64_t(decode),scale),edit);
                s.start=AddTime(Time100ns(AddTime(int64_t(decode),cts),scale),edit);
                s.end=AddTime(Time100ns(AddTime(AddTime(int64_t(decode),cts),d),scale),edit);
                append.push_back(s);offset+=n;decode+=d;
            }
        }
        if(append.empty() || (first && !kind && !append.front().sync))throw std::runtime_error("Fragment lacks initial keyframe/samples");
        lastDecode=AddTime(Time100ns(int64_t(decode),scale),edit);first=false;
        samples.insert(samples.end(),append.begin(),append.end());
    }
};
} // namespace harvester
