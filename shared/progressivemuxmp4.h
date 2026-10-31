// Verified growing multiplexed fMP4, two virtual track views. GPL-3.0-or-later.
#pragma once
#include "progressiveresourceio.h"
#include "DSUtil/HarvesterMP4Muxed.h"
#include <array>
namespace foxicmp { namespace progressive {
class MuxedMp4Bridge {
    using Layout=harvester::muxmp4::Layout;
    struct Published {Snapshot model;std::string resource;std::array<std::string,2> ids;uint64_t prefix=0;bool complete=false;Layout layout;harvester::Snapshot legacy;};
    std::shared_ptr<ResourceSource> source;std::filesystem::path registry;Layout::Validate validate;
    std::shared_ptr<const Published> current;std::mutex publication,errorMutex;
    std::atomic<bool> cancelled{false},failed{false};std::atomic<uint64_t> decoderBytes[2]{};
    std::string failure;
    static void supported(bool value,const char* why){Require(value,"muxedMp4",why,"unsupported_media");}
    static std::shared_ptr<Published> Plan(const std::string& json) {
        auto out=std::make_shared<Published>();out->model=Parse(json);const auto& m=out->model;
        supported(!m.live && !m.hasWindow && m.knownDuration && m.numerator==1 && m.denominator==10000000 && m.offset==0 && m.tracks.size()==2 && m.resources.size()==1,"Muxed fMP4 requires known-duration VOD, zero offset, two required scalar views and one resource");
        Require(m.state!="failed" && m.state!="cancelled","muxedMp4","Writer failed or cancelled","writer-failed");
        for(const auto& item:m.tracks) {
            const auto& t=item.second;supported((t.kind=="video" || t.kind=="audio") && t.required && t.container=="fmp4-muxed" && !t.resource.empty() && t.initialization.empty() && t.segments.empty(),"Muxed fMP4 requires shared scalar tracks without external init");
            const unsigned k=t.kind=="video"?0:1;supported(out->ids[k].empty(),"Duplicate muxed track kind");out->ids[k]=t.id;supported(t.codec.empty() || t.codec==(k?"aac-lc":"h264"),"Unsupported muxed codec hint");
            Require(t.state!="failed" && t.state!="cancelled","muxedMp4","Track writer failed","resource-failed");
            if(out->resource.empty())out->resource=t.resource;supported(out->resource==t.resource,"Muxed views must share one resource ID");
        }
        supported(!out->ids[0].empty() && !out->ids[1].empty(),"Muxed fMP4 needs both video and audio");const auto& v=m.tracks.at(out->ids[0]);const auto& a=m.tracks.at(out->ids[1]);
        supported(v.initializationReady==a.initializationReady && v.state==a.state,"Muxed views require equal readiness/state");const auto& r=m.resources.at(out->resource);
        supported(!r.retired,"Retired muxed resource");Require(r.state!="failed" && r.state!="cancelled","muxedMp4","Resource writer failed","resource-failed");Require(r.integrity,"muxedMp4","Muxed fMP4 requires SHA-256","integrity-required");
        for(const auto& range:r.ranges){supported(range.offset==out->prefix,"Muxed fMP4 requires a contiguous confirmed prefix");out->prefix=range.offset+range.length;}
        out->complete=m.state=="complete" && v.state=="complete" && r.state=="complete" && r.known && out->prefix==r.length;return out;
    }
    void check() const {Require(!cancelled.load(),"muxedMp4","Reader cancelled","cancelled");Require(!failed.load(),"muxedMp4","Muxed reader failed; close and reopen","session_closed");}
    void fail(const std::string& code){std::lock_guard<std::mutex> lock(errorMutex);if(failure.empty())failure=code;failed.store(true);}
    bool exact(const std::string& id,uint64_t at,void* out,size_t count) {
        auto* bytes=static_cast<char*>(out);for(size_t n=0;n<count;){check();const auto part=(std::min)(size_t(65536),count-n);const auto r=source->read(id,at+n,bytes+n,part);Require(r.data() && r.bytes==part,"muxedMp4",r.reason.c_str(),r.data()?"source-truncated":r.code.c_str());n+=part;}check();return true;
    }
    void materialize(Published& next,const Published* old) {
        if(old)next.layout=old->layout;
        if(next.model.tracks.at(next.ids[0]).initializationReady) {
            next.layout.Update([&](uint64_t at,void* p,size_t n){return exact(next.resource,at,p,n);},next.prefix,next.complete,validate);
        }
        auto& s=next.legacy;s.sessionId=next.model.id;s.generation=next.model.generation;s.revision=next.model.revision;s.duration=next.model.duration;s.timestampOffset=0;s.state=state(next.model.state);s.tracks.resize(2);
        for(unsigned k=0;k<2;++k) {
            auto& t=s.tracks[k];const auto& view=next.layout.tracks[k];t.path=k?"audio.fmp4":"video.fmp4";t.container=k?"m4a":"mp4";t.required=true;t.fragmented=true;t.initializationReady=next.layout.Initialized();t.available=view.bytes;t.total=next.complete?view.bytes:0;t.state=state(next.model.tracks.at(next.ids[k]).state);
            const auto& samples=view.index.Samples();if(!samples.empty()) {
                supported(samples.front().start>=(k?-10000000:0) && samples.front().start<=10000000,"Muxed tracks must start near zero");
                for(const auto& sample:samples)supported(sample.end<=next.model.duration,"Muxed sample exceeds declared VOD duration");
            }
        }
        check();
    }
    static harvester::State state(const std::string& value){return value=="complete"?harvester::State::Complete:value=="paused"?harvester::State::Paused:value=="merging"?harvester::State::Merging:harvester::State::Downloading;}
    bool read(const Published& p,unsigned k,uint64_t at,void* data,size_t count) {
        check();return p.layout.ReadAt([&](uint64_t offset,void* out,size_t n){return exact(p.resource,offset,out,n);},k,at,data,count);
    }
    void cleanup() noexcept {if(!source)return;try{const auto ids=source->readerIds();source.reset();for(const auto& id:ids)ReaderRequest(registry,"{\"version\":1,\"action\":\"readerForget\",\"readerId\":\""+id+"\"}");}catch(...){source.reset();}}
public:
    static bool Declared(const std::string& json) {
        rapidjson::Document d;d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(json.data(),json.size());if(!d.IsObject() || !d.HasMember("tracks") || !d["tracks"].IsArray())return false;
        for(const auto& t:d["tracks"].GetArray())if(t.IsObject() && t.HasMember("containerHint") && t["containerHint"].IsString() && std::string(t["containerHint"].GetString(),t["containerHint"].GetStringLength())=="fmp4-muxed")return true;return false;
    }
    static void CheckProfile(const std::string& json){Plan(json);}
    MuxedMp4Bridge(const std::filesystem::path& root,const std::filesystem::path& receipts,const std::string& json,Layout::Validate validation):registry(receipts),validate(std::move(validation)) {
        auto next=Plan(json);supported(next->prefix<=harvester::muxmp4::MaxMdat,"Initial muxed prefix exceeds 16 MiB");source=std::make_shared<ResourceSource>(root,receipts,json);
        try {materialize(*next,nullptr);Require(next->layout.Initialized() && !next->layout.tracks[0].index.Samples().empty() && !next->layout.tracks[1].index.Samples().empty(),"muxedMp4","Wait for head moov and the first complete A/V fragment","waiting_initialization");current=std::move(next);}
        catch(const Error&){cleanup();throw;}catch(const std::exception& e){cleanup();throw Error("unsupported_media","muxedMp4",e.what());}
    }
    ~MuxedMp4Bridge(){cancel();cleanup();}
    void cancel(){cancelled.store(true);if(source)source->cancel();}
    harvester::Snapshot snapshot() const{return std::atomic_load(&current)->legacy;}
    bool readIndex(unsigned k,uint64_t at,void* data,size_t count){try{const auto p=std::atomic_load(&current);return read(*p,k,at,data,count);}catch(const Error& e){if(e.code!="cancelled")fail(e.code);return false;}catch(...){fail("muxed-index-error");return false;}}
    int64_t readPayload(unsigned k,uint64_t at,void* data,size_t count) {
        try{check();Require(k<2 && count<=65536 && at<=MaxByte && count<=MaxByte-at && (data || !count),"muxedMp4","Invalid bounded read","invalid-read");const auto p=std::atomic_load(&current);const auto size=p->layout.tracks[k].bytes;
            if(!count)return 0;if(at>=size)return p->complete && at==size?0:-1;const auto n=size_t((std::min)(uint64_t(count),size-at));std::vector<uint8_t> bytes(n);
            Require(read(*p,k,at,bytes.data(),n),"muxedMp4","Virtual read exceeds confirmed bytes","source-truncated");check();std::memcpy(data,bytes.data(),n);decoderBytes[k].fetch_add(n);return int64_t(n);
        }catch(const Error& e){if(e.code!="cancelled")fail(e.code);return -1;}catch(...){fail("muxed-read-error");return -1;}
    }
    void publish(const std::string& json) {
        std::lock_guard<std::mutex> lock(publication);check();const auto old=std::atomic_load(&current);auto next=Plan(json);Advance(old->model,next->model);
        supported(next->prefix-old->prefix<=harvester::muxmp4::MaxMdat,"Publish at most 16 MiB of new muxed bytes");
        try{source->publish(json);materialize(*next,old.get());std::shared_ptr<const Published> accepted=std::move(next);std::atomic_store(&current,std::move(accepted));}
        catch(const Error& e){fail(e.code);throw;}catch(const std::exception& e){fail("unsupported_media");throw Error("unsupported_media","muxedMp4",e.what());}
    }
    void verify(){check();try{source->verifyPinned();}catch(const Error& e){fail(e.code);throw;}}
    std::vector<BoundResource> bindings() const {const auto p=std::atomic_load(&current);std::vector<BoundResource> out;for(const auto& id:p->ids){auto b=source->binding(p->resource);b.id=id;out.push_back(std::move(b));}return out;}
    std::string details() const {
        const auto p=std::atomic_load(&current);rapidjson::Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("schemaVersion",2,a);d.AddMember("generation",Value(std::to_string(p->model.generation).c_str(),a),a);d.AddMember("revision",Value(std::to_string(p->model.revision).c_str(),a),a);d.AddMember("writerInstanceId",Value(p->model.writer.c_str(),a),a);
        d.AddMember("mediaMode","growing-muxed-fmp4-h264-aac-lc",a);d.AddMember("muxed",true,a);d.AddMember("segments",false,a);d.AddMember("videoContainer","fmp4",a);d.AddMember("audioContainer","fmp4",a);d.AddMember("decoderContainer","fmp4",a);d.AddMember("videoCodec","h264",a);d.AddMember("audioCodec","aac-lc",a);
        Value bytes(rapidjson::kObjectType);bytes.AddMember("video",Value(std::to_string(decoderBytes[0].load()).c_str(),a),a);bytes.AddMember("audio",Value(std::to_string(decoderBytes[1].load()).c_str(),a),a);d.AddMember("decoderIoBytes",bytes,a);d.AddMember("physicalResources",1,a);d.AddMember("fragments",uint32_t(p->layout.Fragments()),a);
        if(!p->model.metadata.empty()){rapidjson::Document m;m.Parse(p->model.metadata.c_str());Value v(m,a);d.AddMember("metadata",v,a);}
        Value tracks(rapidjson::kArrayType);for(unsigned k=0;k<2;++k){const auto& t=p->model.tracks.at(p->ids[k]);Value v(rapidjson::kObjectType);v.AddMember("index",k,a);v.AddMember("id",Value(t.id.c_str(),a),a);v.AddMember("kind",Value(t.kind.c_str(),a),a);v.AddMember("label",Value(t.label.c_str(),a),a);v.AddMember("language",Value(t.language.c_str(),a),a);v.AddMember("default",t.defaultTrack,a);v.AddMember("required",true,a);v.AddMember("ready",true,a);v.AddMember("state","ready",a);v.AddMember("errorCode","",a);v.AddMember("error","",a);v.AddMember("codec",Value(k?"aac-lc":"h264",a),a);v.AddMember("container","fmp4",a);v.AddMember("decoderIoBytes",Value(std::to_string(decoderBytes[k].load()).c_str(),a),a);tracks.PushBack(v,a);}d.AddMember("mediaTracks",tracks,a);d.AddMember("subtitleTracks",Value(rapidjson::kArrayType),a);return lease_detail::Encode(d);
    }
};
}} // namespace foxicmp::progressive
