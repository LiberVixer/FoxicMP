// Append-only virtual fMP4/WebM track over confirmed segment resources. GPL-3.0-or-later.
#pragma once
#include "progressivesegmentstager.h"
namespace foxicmp { namespace progressive {
struct SegmentSpan {std::string resource;uint64_t offset=0,length=0;size_t segment=0;};
struct SegmentLayout {
    std::string initialization;std::vector<SegmentSpan> spans;
    uint64_t available=0;bool initialized=false,complete=false;
};
inline void SegmentProfile(bool condition,const char* message) {Require(condition,"segmentProfile",message,"unsupported_media");}
inline SegmentLayout BuildSegmentLayout(const Snapshot& snapshot,const Track& track) {
    SegmentProfile(track.resource.empty() && track.required && (track.container=="fmp4" || track.container=="webm") &&
        (track.kind=="video" || track.kind=="audio"),"Segment playback requires required separate fMP4 or WebM video/audio tracks");
    const bool webm=track.container=="webm";
    SegmentProfile(track.codec.empty() || (webm?(track.kind=="video"?(track.codec=="vp8" || track.codec=="vp9" || track.codec=="av1"):track.codec=="opus"):(track.kind=="video"?(track.codec=="h264" || track.codec=="hevc" || track.codec=="av1" || track.codec=="vp9"):(track.codec=="aac-lc" || track.codec=="opus"))),"Unsupported segmented container/codec profile");
    SegmentLayout out;out.initialization=track.initialization;
    if(!track.segments.empty() && !track.segments.front().initialization.empty())out.initialization=track.segments.front().initialization;
    SegmentProfile(!out.initialization.empty(),"Segment playback requires an explicit external initialization resource");
    const auto& init=snapshot.resources.at(out.initialization);
    Require(init.state!="failed" && init.state!="cancelled" && track.state!="failed" && track.state!="cancelled","segmentProfile","Required initialization/track writer failed","resource-failed");
    SegmentProfile(!init.retired && (!init.known || (init.length>0 && init.length<=1048576)),"Invalid segment initialization resource");
    const bool initReady=track.initializationReady && init.state=="complete" && init.known && init.covers(0,init.length);
    out.initialized=initReady;
    if(initReady){out.spans.push_back({init.id,0,init.length,0});out.available=init.length;}
    bool prefixReady=initReady;uint64_t previousSequence=0;int64_t previousEnd=0;std::string configuration;
    std::set<std::string> resources{out.initialization};
    for(size_t i=0;i<track.segments.size();++i) {
        const auto& segment=track.segments[i];const auto& resource=snapshot.resources.at(segment.resource);
        SegmentProfile(!segment.discontinuity && segment.offset==0 &&
            (i==0 || segment.configuration==configuration) &&
            (segment.initialization.empty() || segment.initialization==out.initialization),
            "Discontinuity, timestamp rebasing and codec/init changes require another playback profile");
        SegmentProfile(i==0 || (previousSequence<UINT64_MAX && segment.sequence==previousSequence+1),"Segment sequences must be consecutive");
        SegmentProfile(i==0 || (segment.start>=previousEnd && uint64_t(segment.start)-uint64_t(previousEnd)<=uint64_t(webm?10000:10)),"Segment timeline must be contiguous");
        SegmentProfile(i!=0 || (track.kind=="video"?(segment.start>=0 && segment.start<=10000000):(segment.start>=-10000000 && segment.start<=10)),"First segment must start near zero, allowing bounded video reordering/AAC preroll");
        SegmentProfile(resources.insert(resource.id).second && !resource.retired,"Each segment needs a distinct non-retired resource");
        SegmentProfile(!resource.known || (resource.length>0 && resource.length<=16777216),"Segment media resource exceeds its 16 MiB bound");
        Require(resource.state!="failed" && resource.state!="cancelled" && track.state!="failed" && track.state!="cancelled","segmentProfile","Required segment writer failed","resource-failed");
        const bool ready=resource.state=="complete" && resource.known && resource.covers(0,resource.length);
        prefixReady=prefixReady && ready;
        if(prefixReady) {
            SegmentProfile(resource.length<=MaxByte-out.available,"Virtual segment stream overflows");
            out.spans.push_back({resource.id,out.available,resource.length,i});out.available+=resource.length;
        }
        previousSequence=segment.sequence;previousEnd=segment.start+segment.duration;configuration=segment.configuration;
    }
    out.complete=track.state=="complete" && prefixReady && !track.segments.empty();
    return out;
}
inline ResourceRead ReadSegmentStream(ResourceSource& source,const SegmentLayout& layout,uint64_t offset,void* buffer,size_t count) {
    if(offset>layout.available)return {"waiting-range","Virtual segment bytes are not yet available",0};
    if(offset==layout.available)return layout.complete?ResourceRead{"eof","Completed segment track",0}:ResourceRead{"waiting-range","Wait for the next confirmed segment",0};
    const auto requested=size_t((std::min)(uint64_t(count),layout.available-offset));size_t copied=0;
    for(const auto& span:layout.spans) {
        if(offset>=span.offset+span.length)continue;
        if(offset<span.offset)return {"waiting-range","Virtual segment layout has a gap",0};
        const auto at=offset-span.offset;const auto chunk=size_t((std::min)(uint64_t(requested-copied),span.length-at));
        const auto read=source.read(span.resource,at,static_cast<char*>(buffer)+copied,chunk);
        if(!read.data() || read.bytes!=chunk)return {read.data()?"source-truncated":read.code,read.reason,0};
        offset+=chunk;copied+=chunk;if(copied==requested)break;
    }
    return {"data","Confirmed virtual segment bytes",copied};
}
inline void ValidateSegmentMedia(const Snapshot& snapshot,const std::array<std::string,harvester::MaxMediaTracks>& ids,
    const std::array<SegmentLayout,harvester::MaxMediaTracks>& layouts,const std::shared_ptr<ResourceSource>& source,std::set<std::string>& validated) {
    SegmentStager stager(snapshot,source,true,true,true,true,true);
    for(size_t track=0;track<2;++track) {
        const auto& layout=layouts[track];if(!layout.initialized || validated.count(layout.initialization))continue;
        const auto length=snapshot.resources.at(layout.initialization).length;std::vector<char> bytes(size_t(length),0);
        for(size_t at=0;at<bytes.size();) {
            const auto count=(std::min)(size_t(65536),bytes.size()-at);
            const auto read=source->read(layout.initialization,at,bytes.data()+at,count);
            Require(read.data() && read.bytes==count,"segmentProfile",read.reason.c_str(),read.code.c_str());at+=count;
        }
        try {
            if(snapshot.tracks.at(ids[track]).container=="webm") {
                const auto info=harvester::WebMInitialization([&](uint64_t at,void* buffer,size_t count){
                    if(at>bytes.size() || count>bytes.size()-size_t(at))return false;
                    std::memcpy(buffer,bytes.data()+size_t(at),count);return true;
                },length,int(track),true);
                SegmentProfile(info.headerEnd==length && info.segmentEnd==UINT64_MAX && info.scale==1000000,
                    "Segmented WebM initialization requires an unknown-size Segment, 1 ms clock and no Clusters/Cues");
                SegmentProfile(track==0 || info.codec=="opus","Segmented WebM currently requires Opus audio");
            }else {
            unsigned ftyp=0,moov=0;
            for(const auto& box:harvester::BoxData{reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size()}.Children()) {
                if(box.first==harvester::FourCC("ftyp"))++ftyp;
                else if(box.first==harvester::FourCC("moov"))++moov;
                else SegmentProfile(box.first==harvester::FourCC("free"),"Initialization may contain only ftyp, moov and free boxes");
            }
            SegmentProfile(ftyp==1 && moov==1,"Initialization needs exactly one ftyp and moov box");
            }
        }catch(const Error&){throw;}catch(const std::exception&){SegmentProfile(false,"Malformed segment initialization boxes");}
        validated.insert(layout.initialization);
    }
    for(size_t track=0;track<2;++track) {
        const auto& media=snapshot.tracks.at(ids[track]);
        for(size_t span=1;span<layouts[track].spans.size();++span) {
            const auto& part=layouts[track].spans[span];if(validated.count(part.resource))continue;
            auto staged=stager.stage(media.id,part.segment,true);
            Require(staged.ready(),"segmentProfile",staged.reason.c_str(),staged.code.c_str());
            try {
                if(media.container!="webm") {
                const auto initSize=snapshot.resources.at(layouts[track].initialization).length;
                const harvester::BoxData bytes{reinterpret_cast<const uint8_t*>(staged.bytes.data()+size_t(initSize)),staged.bytes.size()-size_t(initSize)};
                for(const auto& box:bytes.Children())
                    SegmentProfile(box.first==harvester::FourCC("moof") || box.first==harvester::FourCC("mdat") ||
                        box.first==harvester::FourCC("styp") || box.first==harvester::FourCC("sidx"),
                        "Media segment may contain only moof, mdat, styp and sidx boxes");
                }
            }catch(const Error&){throw;}catch(const std::exception&){SegmentProfile(false,"Malformed media segment boxes");}
            int64_t start=INT64_MAX,end=INT64_MIN;
            for(const auto& sample:staged.samples){start=(std::min)(start,sample.start);end=(std::max)(end,sample.end);}
            const auto& declared=media.segments[part.segment];const auto declaredEnd=declared.start+declared.duration;
            const auto closeEnough=[](int64_t a,int64_t b){return a>=b?uint64_t(a)-uint64_t(b)<=10:uint64_t(b)-uint64_t(a)<=10;};
            SegmentProfile(closeEnough(start,declared.start) && closeEnough(end,declaredEnd),"Segment timestamps/duration do not match indexed media packets");
            validated.insert(part.resource);
        }
    }
}
}} // namespace foxicmp::progressive
