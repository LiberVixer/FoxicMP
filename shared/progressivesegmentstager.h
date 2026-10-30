// Bounded single-segment staging/indexing. GPL-3.0-or-later.
// No downloader, decoder, remux or progressive playback activation.
#pragma once
#include "progressiveresourceio.h"
#include "DSUtil/HarvesterMP4.h"

namespace foxicmp { namespace progressive {
struct SegmentStage {
    std::string code,reason,initializationResource,resource;
    std::vector<char> bytes;
    std::vector<harvester::Sample> samples;
    bool ready() const {return code=="data";}
};
class SegmentStager {
    Snapshot snapshot;
    std::shared_ptr<ResourceSource> source;
    mutable std::mutex mutex;
    std::atomic<bool> cancelled{false};
    static constexpr uint64_t MaxInitBytes=1024*1024,MaxSegmentBytes=16*1024*1024,MaxSamples=65536;
    static SegmentStage result(const char* code,const char* reason) {
        SegmentStage out;out.code=code;out.reason=reason;return out;
    }
    static SegmentStage readiness(const Resource& resource,uint64_t limit) {
        if(resource.state=="failed" || resource.state=="cancelled")return result("resource-failed","Segment resource writer failed");
        if(resource.retired)return result("resource-retired","Segment resource left the active window");
        if(resource.known && resource.length>limit)return result("segment-too-large","Segment resource exceeds its bounded staging limit");
        if(!resource.known || !resource.covers(0,resource.length))return result("waiting-range","Segment resource is not fully confirmed");
        if(!resource.length)return result("unsupported_media","Empty segment resources cannot be staged");
        return result("data","Confirmed segment resource");
    }
    SegmentStage readResource(const Resource& resource,uint64_t limit) {
        auto out=readiness(resource,limit);if(!out.ready())return out;
        out.bytes.resize(size_t(resource.length));
        for(uint64_t at=0;at<resource.length;) {
            const auto count=size_t((std::min)(uint64_t(65536),resource.length-at));
            const auto read=source->read(resource.id,at,out.bytes.data()+size_t(at),count);
            if(!read.data() || read.bytes!=count) {
                out.code=read.code;out.reason=read.reason;out.bytes.clear();return out;
            }
            at+=count;
        }
        return out;
    }
    SegmentStage stageLocked(const std::string& trackId,size_t index,bool demux) {
        if(cancelled.load())return result("cancelled","Segment staging was cancelled");
        if(snapshot.state=="failed" || snapshot.state=="cancelled")return result("writer-failed","Segment writer failed or cancelled");
        const auto found=snapshot.tracks.find(trackId);Require(found!=snapshot.tracks.end(),"trackId","Unknown segment track","unknown_track");
        const auto& track=found->second;Require(track.resource.empty(),"track.source","Track does not use segments","unsupported_media");
        if(index>=track.segments.size() && !Terminal(track.state) && !Terminal(snapshot.state))return result("waiting-segment","Segment has not been published yet");
        Require(index<track.segments.size(),"segmentIndex","Segment index is out of range","unknown_segment");
        const auto& segment=track.segments[index];
        char unused=0;const auto control=source->read(segment.resource,0,&unused,0);
        if(!control.data())return result(control.code.c_str(),control.reason.c_str());
        if(track.state=="failed" || track.state=="cancelled")return result("resource-failed","Segment track writer failed");
        if(!track.initializationReady)return result("waiting-initialization","Track initialization is not ready");
        std::string initialization=track.initialization;
        for(size_t i=0;i<=index;++i)if(!track.segments[i].initialization.empty())initialization=track.segments[i].initialization;
        Require(initialization!=segment.resource,"initializationResourceId","Separate initialization and segment resources are required","unsupported_media");
        const auto& media=snapshot.resources.at(segment.resource);
        auto ready=readiness(media,MaxSegmentBytes);if(!ready.ready())return ready;
        if(!initialization.empty()) {
            ready=readiness(snapshot.resources.at(initialization),MaxInitBytes);if(!ready.ready())return ready;
        }
        auto out=result("data","Staged initialization and media segment");
        if(!initialization.empty()) {
            out=readResource(snapshot.resources.at(initialization),MaxInitBytes);if(!out.ready())return out;
        }
        auto payload=readResource(media,MaxSegmentBytes);if(!payload.ready())return payload;
        out.bytes.insert(out.bytes.end(),payload.bytes.begin(),payload.bytes.end());
        out.initializationResource=initialization;out.resource=segment.resource;
        if(demux) {
            Require(track.container=="fmp4" && (track.kind=="video" || track.kind=="audio"),"track","Only single-track fMP4 packet indexing is enabled","unsupported_media");
            Require(snapshot.numerator==1 && snapshot.denominator==10000000,"timeline.timebase","Segment index requires the 100 ns timebase","unsupported_media");
            Require(track.codec.empty() || track.codec==(track.kind=="video"?"h264":"aac-lc"),"track.codecHint","Segment index profile is H.264/AAC","unsupported_media");
            try {
                // Bound allocation before FragmentIndex handles the trun tables.
                uint64_t packets=0;harvester::BoxData data{reinterpret_cast<const uint8_t*>(out.bytes.data()),out.bytes.size()};
                for(const auto& box:data.Children())if(box.first==harvester::FourCC("moof")) {
                    const auto traf=box.second.One("traf"),tfhd=traf.One("tfhd");const auto flags=tfhd.U32(0);
                    Require(!(flags>>24) && (flags&0x020000) && !(flags&1),"tfhd","Segment indexing requires default-base-is-moof without absolute offsets","unsupported_media");
                    for(const auto& child:traf.Children())if(child.first==harvester::FourCC("trun")) {
                        const auto count=child.second.U32(4);
                        Require(count<=MaxSamples-packets,"trun","Segment packet count exceeds its bound","segment-too-large");packets+=count;
                    }
                }
                harvester::FragmentIndex indexer;
                indexer.Update([&](uint64_t at,void* buffer,size_t count){
                    if(at>out.bytes.size() || count>out.bytes.size()-size_t(at))return false;
                    std::memcpy(buffer,out.bytes.data()+size_t(at),count);return true;
                },out.bytes.size(),track.kind=="audio"?1:0,true);
                Require(indexer.Initialized() && !indexer.Samples().empty(),"segment","Segment has no indexed packets","unsupported_media");
                out.samples=indexer.Samples();
            }catch(const Error&){throw;}
             catch(const std::exception&){return result("unsupported_media","Invalid or unsupported fragmented MP4 segment");}
        }
        const auto final=source->read(segment.resource,0,&unused,0);
        if(!final.data())return result(final.code.c_str(),final.reason.c_str());
        return out;
    }
public:
    SegmentStager(const Snapshot& value,const std::shared_ptr<ResourceSource>& io):snapshot(value),source(io) {}
    SegmentStager(const std::filesystem::path& root,const std::filesystem::path& registry,const std::string& json)
        :snapshot(Parse(json)),source(std::make_shared<ResourceSource>(root,registry,json)) {}
    void publish(const std::string& json) {
        auto next=Parse(json);std::lock_guard<std::mutex> lock(mutex);Advance(snapshot,next);
        source->publish(json);snapshot=std::move(next);
    }
    SegmentStage stage(const std::string& trackId,size_t index,bool demux=false) {
        std::lock_guard<std::mutex> lock(mutex);return stageLocked(trackId,index,demux);
    }
    void cancel(){cancelled.store(true);source->cancel();}
    std::vector<std::string> readerIds() const {return source->readerIds();}
    std::shared_ptr<std::atomic<bool>> releaseReceipt() const {return source->releaseReceipt();}
    std::shared_ptr<ResourceSource> io() const {return source;}
};
}} // namespace foxicmp::progressive
