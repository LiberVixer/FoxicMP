// Internal TS resource staging/virtual stream. GPL-3.0-or-later.
// Not a player capability: engine/IPC integration is a separate step.
#pragma once
#include "progressivelocalcatalog.h"
#include "DSUtil/HarvesterTSCodec.h"

namespace foxicmp { namespace progressive {
class TsResourceStream {
    struct Span {std::string resource;uint64_t offset=0,length=0;size_t segment=0;};
    struct TrackState {
        std::string id,initialization;
        std::vector<Span> spans;
        uint64_t available=0;bool initialized=false,complete=false;
        harvester::ts::Program program;
        std::shared_ptr<const harvester::ts::AccessIndex> index;
    };
    struct Published {Snapshot model;std::array<TrackState,2> tracks;size_t pages=0;};
    std::unique_ptr<LocalSegmentCatalog> catalog;
    std::shared_ptr<ResourceSource> source;
    std::filesystem::path registry;
    std::shared_ptr<const Published> current;
    std::mutex publisher;
    mutable std::mutex errorMutex;
    std::string failureCode,failureReason;
    std::atomic<bool> cancelled{false},failed{false};
    static bool hasCatalog(const std::string& json) {
        rapidjson::Document d;d.Parse(json.c_str());Require(!d.HasParseError() && d.IsObject(),"$","Invalid TS snapshot");return d.HasMember("segmentCatalog");
    }
    static void supported(bool value,const char* message) {Require(value,"tsProfile",message,"unsupported_media");}
    static uint64_t origin(const Snapshot& model) {
        supported(model.offset>=0,"TS origin must be nonnegative and fixed");
        const auto ticks=uint64_t(model.offset),whole=ticks/10000000,part=ticks%10000000;
        supported(whole<=(uint64_t(1)<<33)/90000,"TS origin exceeds its 33-bit clock");
        const auto value=whole*90000+(part*90000+9999999)/10000000;
        supported(value<(uint64_t(1)<<33) && harvester::Time100ns(int64_t(value),90000)==model.offset,"TS origin must equal an exact rescaled 90 kHz timestamp");return value;
    }
    static TrackState layout(const Snapshot& model,const Track& track) {
        supported(track.required && track.resource.empty() && track.container=="mpegts" && (track.kind=="video" || track.kind=="audio"),"TS requires separate required segmented video/audio tracks");
        supported(track.codec.empty() || track.codec==(track.kind=="video"?"h264":"aac-lc"),"Unsupported TS codec hint");
        Require(track.state!="failed" && track.state!="cancelled","tsProfile","Required TS track writer failed","resource-failed");
        TrackState out;out.id=track.id;out.initialization=track.initialization;
        if(!track.segments.empty() && !track.segments.front().initialization.empty())out.initialization=track.segments.front().initialization;
        supported(!out.initialization.empty(),"TS needs an external PAT/PMT resource");
        const auto& init=model.resources.at(out.initialization);
        Require(init.state!="failed" && init.state!="cancelled","tsProfile","Required TS initialization failed","resource-failed");
        supported(!init.retired && (!init.known || (init.length>0 && init.length<=harvester::ts::MaxInitBytes)),"Invalid TS initialization length/state");
        out.initialized=track.initializationReady && init.known && init.state=="complete" && init.covers(0,init.length);
        if(out.initialized){out.spans.push_back({init.id,0,init.length,0});out.available=init.length;}
        bool prefix=out.initialized;uint64_t sequence=0;int64_t end=0;std::string configuration;
        std::set<std::string> used{out.initialization};
        for(size_t i=0;i<track.segments.size();++i) {
            const auto& segment=track.segments[i];const auto& resource=model.resources.at(segment.resource);
            supported(!segment.discontinuity && segment.offset==0 && (segment.initialization.empty() || segment.initialization==out.initialization) &&
                (!i || segment.configuration==configuration),"TS rebasing, discontinuity and initialization/configuration changes need another profile");
            supported(!i || (sequence<UINT64_MAX && segment.sequence==sequence+1 && harvester::ts::Close(segment.start,end)),"TS segment sequence/timeline gap");
            supported(i || (track.kind=="video"?(segment.start>=0 && segment.start<=10000000):(segment.start>=-10000000 && segment.start<=112)),"First TS segment must start near zero");
            supported(used.insert(resource.id).second && !resource.retired && (!resource.known || (resource.length>0 && resource.length<=harvester::ts::MaxSegmentBytes)),"Invalid/aliased TS segment resource");
            Require(resource.state!="failed" && resource.state!="cancelled","tsProfile","Required TS segment writer failed","resource-failed");
            prefix=prefix && resource.known && resource.state=="complete" && resource.covers(0,resource.length);
            if(prefix){supported(resource.length<=MaxByte-out.available,"TS virtual stream overflow");out.spans.push_back({resource.id,out.available,resource.length,i});out.available+=resource.length;}
            sequence=segment.sequence;end=segment.start+segment.duration;configuration=segment.configuration;
        }
        out.complete=track.state=="complete" && prefix && !track.segments.empty();return out;
    }
    static std::shared_ptr<Published> plan(Snapshot model,size_t pages) {
        supported(!model.live && !model.hasWindow && model.knownDuration && model.numerator==1 && model.denominator==10000000 && model.tracks.size()==2,"TS requires known-duration VOD and two tracks in the 100 ns timebase");origin(model);
        Require(model.state!="failed" && model.state!="cancelled","tsProfile","Required TS writer failed","writer-failed");
        auto out=std::make_shared<Published>();out->model=std::move(model);out->pages=pages;
        std::set<std::string> used;std::array<bool,2> kinds{};
        for(const auto& item:out->model.tracks) {
            const auto& track=item.second;supported(track.kind=="video" || track.kind=="audio","Only TS video/audio are enabled");const auto kind=track.kind=="video"?0u:1u;
            supported(!kinds[kind],"Duplicate TS track kind");kinds[kind]=true;out->tracks[kind]=layout(out->model,track);
            supported(used.insert(out->tracks[kind].initialization).second,"TS tracks need distinct initialization resources");
            for(const auto& segment:track.segments)supported(used.insert(segment.resource).second,"TS tracks need distinct media resources");
        }
        supported(kinds[0] && kinds[1] && used.size()==out->model.resources.size(),"TS accepts only resources referenced by its two tracks");
        for(const auto& item:out->model.resources)Require(item.second.integrity,"tsProfile","TS staging requires SHA-256 for every resource","integrity-required");return out;
    }
    void check() const {
        Require(!cancelled.load(),"tsStream","TS reader was cancelled","cancelled");
        if(failed.load()){const auto error=failure();throw Error(error.code.c_str(),"tsStream",error.reason.c_str());}
    }
    ResourceRead failure() const {
        std::lock_guard<std::mutex> lock(errorMutex);return {failureCode.empty()?"session-closed":failureCode,failureReason.empty()?"TS resource stream failed; close and reopen":failureReason,0};
    }
    void fail(const std::string& code,const std::string& reason) {
        failed.store(true);std::lock_guard<std::mutex> lock(errorMutex);if(failureCode.empty()){failureCode=code;failureReason=reason;}
    }
    std::vector<uint8_t> load(const Resource& resource) {
        check();std::vector<uint8_t> bytes(size_t(resource.length));
        for(size_t at=0;at<bytes.size();) {
            check();const auto count=(std::min)(size_t(65536),bytes.size()-at);const auto read=source->read(resource.id,at,bytes.data()+at,count);
            Require(read.data() && read.bytes==count,"tsStream",read.reason.c_str(),read.data()?"source-truncated":read.code.c_str());at+=count;
        }
        check();return bytes;
    }
    void materialize(Published& next,const Published* previous) {
        for(size_t kind=0;kind<2;++kind) {
            auto& track=next.tracks[kind];if(!track.initialized)continue;
            const auto init=load(next.model.resources.at(track.initialization));
            track.program=harvester::ts::Initialization({init.data(),init.size()},int(kind));
            const auto old=previous?&previous->tracks[kind]:nullptr;
            if(old && old->initialized)supported(old->initialization==track.initialization && old->program.pat==track.program.pat && old->program.pmt==track.program.pmt,"TS initialization changed");
            const auto already=old && old->index?old->spans.size()-1:0;
            if(old && old->index && track.spans.size()-1==already){track.index=old->index;continue;}
            auto index=old && old->index?std::make_shared<harvester::ts::AccessIndex>(*old->index):std::make_shared<harvester::ts::AccessIndex>(track.program,init.size(),origin(next.model));
            const auto& declared=next.model.tracks.at(track.id);
            for(size_t i=already+1;i<track.spans.size();++i) {
                const auto& span=track.spans[i];const auto data=load(next.model.resources.at(span.resource));const auto& segment=declared.segments[span.segment];
                index->append({data.data(),data.size()},segment.start,segment.duration);check();
            }
            supported(index->position()==track.available,"TS index and virtual bytes differ");track.index=std::move(index);
        }
    }
    ResourceRead readAt(const Published& state,unsigned kind,uint64_t offset,void* buffer,size_t count) {
        if(cancelled.load())return {"cancelled","TS reader was cancelled",0};if(failed.load())return failure();
        if(kind>=2 || offset>MaxByte || count>65536 || count>MaxByte-offset || (!buffer && count))return {"invalid-read","Invalid bounded TS read",0};
        const auto& track=state.tracks[kind];
        if(offset>track.available)return {track.complete?"out-of-range":"waiting-range","TS virtual bytes are not available",0};
        if(!count)return {"data","Empty TS read",0};
        if(offset==track.available)return {track.complete?"eof":"waiting-range","TS track tail",0};
        const auto wanted=size_t((std::min)(uint64_t(count),track.available-offset));std::vector<char> bytes(wanted);size_t copied=0;
        for(const auto& span:track.spans) {
            if(offset>=span.offset+span.length)continue;
            if(offset<span.offset)return {"waiting-range","TS virtual layout gap",0};
            const auto chunk=size_t((std::min)(uint64_t(wanted-copied),span.offset+span.length-offset));
            const auto read=source->read(span.resource,offset-span.offset,bytes.data()+copied,chunk);
            if(!read.data() || read.bytes!=chunk){fail(read.data()?"source-truncated":read.code,read.reason);return failure();}
            copied+=chunk;offset+=chunk;if(copied==wanted)break;
        }
        if(cancelled.load())return {"cancelled","TS reader was cancelled",0};if(failed.load())return failure();
        if(copied!=wanted){fail("source-truncated","Incomplete TS virtual layout");return failure();}
        std::memcpy(buffer,bytes.data(),wanted);return {"data","Confirmed TS virtual bytes",wanted};
    }
    void cleanup() noexcept {
        if(!source)return;
        try{const auto ids=source->readerIds();source.reset();for(const auto& id:ids)ReaderRequest(registry,"{\"version\":1,\"action\":\"readerForget\",\"readerId\":\""+id+"\"}");}catch(...){source.reset();}
    }
public:
    struct Status {uint64_t revision=0;std::array<uint64_t,2> bytes{};std::array<size_t,2> units{};std::array<bool,2> initialized{},trackComplete{};std::array<int64_t,2> ready{};int64_t available=0;size_t pages=0,resources=0;bool complete=false;std::string code;};
    struct Packet {ResourceRead result;harvester::Sample sample;std::vector<char> bytes;};
    TsResourceStream(const std::filesystem::path& root,const std::filesystem::path& receipts,const std::string& json):registry(receipts) {
        if(hasCatalog(json)){catalog=std::make_unique<LocalSegmentCatalog>(root);catalog->publish(json);}
        auto next=plan(catalog?catalog->catalog().snapshot:Parse(json),catalog?catalog->catalog().pages.size():0);
        source=std::make_shared<ResourceSource>(root,receipts,catalog?catalog->catalog().expandedJson:json,ResourceFailurePolicy::Strict,ResourceMode::BoundedCatalog);
        try{materialize(*next,nullptr);current=std::move(next);}catch(...){cleanup();throw;}
    }
    ~TsResourceStream(){cancel();cleanup();}
    void cancel(){cancelled.store(true);if(catalog)catalog->cancel();if(source)source->cancel();}
    void publish(const std::string& json) {
        std::lock_guard<std::mutex> lock(publisher);check();supported(bool(catalog)==hasCatalog(json),"TS catalog mode cannot change within a session");
        auto old=std::atomic_load(&current);LocalSegmentCatalog::Prepared prepared;
        if(catalog)prepared=catalog->prepare(json);
        auto next=plan(catalog?prepared.value->snapshot:Parse(json),catalog?prepared.value->pages.size():0);Advance(old->model,next->model);
        try {
            source->publish(catalog?prepared.value->expandedJson:json);materialize(*next,old.get());check();
            if(catalog)catalog->commit(std::move(prepared));std::shared_ptr<const Published> accepted=std::move(next);std::atomic_store(&current,std::move(accepted));
        }catch(const harvester::ts::Error& error){fail(error.code,error.what());throw Error(error.code.c_str(),"tsStream",error.what());}
         catch(const Error& error){fail(error.code,error.what());throw;}
         catch(const std::exception& error){fail("ts-stream-error",error.what());throw;}
    }
    ResourceRead read(unsigned track,uint64_t offset,void* buffer,size_t count) {const auto state=std::atomic_load(&current);return readAt(*state,track,offset,buffer,count);}
    Packet sample(unsigned kind,size_t position,bool encoded=false) {
        Packet out;const auto state=std::atomic_load(&current);
        if(cancelled.load()){out.result={"cancelled","TS reader was cancelled",0};return out;}if(failed.load()){out.result=failure();return out;}
        if(kind>=2){out.result={"unknown-track","Unknown TS track",0};return out;}const auto& track=state->tracks[kind];
        if(!track.index){out.result={"waiting-initialization","TS initialization is not ready",0};return out;}
        const auto& units=track.index->samples();
        if(position>=units.size()){out.result={track.complete?(position==units.size()?"eof":"unknown-sample"):"waiting-segment","TS access unit is not available",0};return out;}
        const auto& unit=units[position];const auto& ranges=encoded?unit.encoded:unit.payload;size_t total=0;
        for(const auto& range:ranges){supported(range.size<=harvester::ts::MaxPesBytes-total,"TS sample exceeds its bound");total+=range.size;}
        out.bytes.resize(total);size_t at=0;std::vector<char> window(65536);uint64_t windowOffset=0;size_t windowBytes=0;
        const auto transportEnd=unit.sample.offset+unit.sample.size;
        for(const auto& range:ranges)for(size_t n=0;n<range.size;) {
            const auto offset=uint64_t(range.offset)+n;supported(offset>=unit.sample.offset && offset<=transportEnd && range.size-n<=transportEnd-offset,"TS payload ranges exceed their PES");
            if(!windowBytes || offset<windowOffset || offset-windowOffset>=windowBytes) {
                const auto count=size_t((std::min)(uint64_t(65536),transportEnd-offset));const auto read=readAt(*state,kind,offset,window.data(),count);
                if(!read.data() || read.bytes!=count){out.bytes.clear();out.result=read;out.result.bytes=0;return out;}windowOffset=offset;windowBytes=count;
            }
            const auto count=(std::min)(range.size-n,windowBytes-size_t(offset-windowOffset));std::memcpy(out.bytes.data()+at,window.data()+size_t(offset-windowOffset),count);n+=count;at+=count;
        }
        if(cancelled.load() || failed.load()){out.bytes.clear();out.result=cancelled.load()?ResourceRead{"cancelled","TS reader was cancelled",0}:failure();return out;}
        out.sample=unit.sample;out.result={"data","Confirmed TS elementary access unit",total};return out;
    }
    Status status() const {
        const auto state=std::atomic_load(&current);Status out;out.revision=state->model.revision;out.pages=state->pages;out.resources=state->model.resources.size();out.complete=state->model.state=="complete";
        out.available=state->model.duration;
        for(size_t i=0;i<2;++i){const auto& track=state->tracks[i];out.bytes[i]=track.available;out.initialized[i]=track.initialized;out.trackComplete[i]=track.complete;out.complete=out.complete && track.complete;
            if(track.index){out.units[i]=track.index->samples().size();out.ready[i]=(std::max)(int64_t(0),(std::min)(state->model.duration,track.index->availableEnd(track.available)));}out.available=(std::min)(out.available,out.ready[i]);}
        if(cancelled.load())out.code="cancelled";else if(failed.load())out.code=failure().code;
        if(!out.code.empty()){out.available=0;out.complete=false;}return out;
    }
    size_t seekSample(unsigned kind,int64_t position) const {check();Require(kind<2,"track","Unknown TS track","unknown-track");const auto state=std::atomic_load(&current);return state->tracks[kind].index?state->tracks[kind].index->seekSample(position):0;}
    void verify(){check();try{source->verifyPinned();}catch(const Error& error){fail(error.code,error.what());throw;}}
    std::vector<std::string> readerIds() const {return source->readerIds();}
    std::shared_ptr<std::atomic<bool>> releaseReceipt() const {return source->releaseReceipt();}
};
}} // namespace foxicmp::progressive
