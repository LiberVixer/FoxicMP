// Internal managed continuous-remux parent. Not a public player capability.
// GPL-3.0-or-later.
#pragma once
#include "progressiveresourceio.h"
#include "progressiveprocess.h"
#include <functional>

namespace foxicmp { namespace progressive {
struct StreamRemuxLimits {
    std::chrono::milliseconds lifetime{3600000},operation{10000},heartbeat{30000};
};
// Explicit internal selection. Existing callers/public v1 retain the verified
// no-reorder profile; a manifest or worker reply cannot widen it implicitly.
enum class StreamRemuxProfile { H264Video, H264ReorderedVideo };
struct StreamRemuxStatus {
    std::string code="starting",reason,result,appliedRevision;
    uint64_t workerId=0,bytesRead=0,bytesWritten=0,maximumReadEnd=0,neededOffset=0;
    unsigned neededLength=0;
    bool done=false,filesClosed=false,sourceComplete=false;
    std::vector<std::string> readerIds;
};
// The sink is an INTERNAL bounded, nonblocking append interface, not an arbitrary
// application callback. It must throw on failure and must never retain references
// into the supplied chunk. Persistent cache + decoder reader ownership are separate.
class StreamRemuxSession {
public:
    using Sink=std::function<void(uint64_t,const std::vector<char>&)>;
private:
    using Clock=std::chrono::steady_clock;
    static constexpr uint64_t InputLimit=128*1024*1024,OutputLimit=64*1024*1024;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::atomic<bool> cancelled{false};
    std::unique_ptr<ResourceSource> source;
    Snapshot snapshot;
    std::string resourceId;
    std::filesystem::path executable;
    StreamRemuxLimits limits;
    StreamRemuxProfile profile;
    Sink sink;
    StreamRemuxStatus state;
    Clock::time_point deadline,heartbeatDeadline;
    std::thread worker;
    const char* workerMode()const{return profile==StreamRemuxProfile::H264ReorderedVideo?
        "continuous-h264-reordered-video-v1":"continuous-h264-video-v1";}
    // All accesses to snapshot/source/status are under mutex, including release.
    void check() const {
        Require(!cancelled.load(),"streamRemux","Remux was cancelled","cancelled");
        const auto now=Clock::now();
        Require(now<deadline,"streamRemux","Remux lifetime expired","backend-time-limit");
        const auto& r=snapshot.resources.at(resourceId);
        Require(snapshot.state!="failed" && snapshot.state!="cancelled","streamRemux","Writer failed","writer-failed");
        Require(r.state!="failed" && r.state!="cancelled","streamRemux","Resource failed","resource-failed");
        Require(r.state=="complete" || now<heartbeatDeadline,"streamRemux","Writer heartbeat expired","writer-heartbeat-timeout");
    }
    void observe() {
        state.maximumReadEnd=source->maximumReadEnd();state.readerIds=source->readerIds();
        state.appliedRevision=std::to_string(snapshot.revision);
        state.sourceComplete=snapshot.resources.at(resourceId).state=="complete";
    }
    static bool waiting(const std::string& code) {
        return code=="waiting-range" || code=="waiting-resource" || code=="reader_registry_busy";
    }
    static void valid(bool ok){Require(ok,"streamRemux","Invalid worker message","backend-protocol-error");}
    ResourceRead read(uint64_t offset,void* bytes,unsigned length) {
        check();auto reply=source->read(resourceId,offset,bytes,length);observe();return reply;
    }
    void waitForSource(process_detail::Process& child,const std::string& reason,uint64_t offset,unsigned length) {
        std::unique_lock<std::mutex> lock(mutex);
        state.code="waiting";state.reason=reason;state.neededOffset=offset;state.neededLength=length;
        std::vector<char> bytes(length);
        for(;;){
            check();Require(child.alive(),"streamRemux","Worker exited while waiting","backend-worker-exited");
            const auto reply=read(reason=="source-complete"?0:offset,bytes.data(),length);
            if(!reply.data() && reply.code!="eof" && !waiting(reply.code))throw Error(reply.code.c_str(),"streamRemux",reply.reason.c_str());
            if(reason=="source-complete") {
                source->verifyPinned();
                if(snapshot.resources.at(resourceId).state=="complete")break;
            }else if(reply.data() || reply.code=="eof")break;
            // No empty resume loop: retain the child's blocked AVIO context.
            // The bounded wake also notices heartbeat loss and external rewrites.
            changed.wait_for(lock,std::chrono::milliseconds(25));
        }
        state.code="running";state.reason.clear();state.neededLength=0;
    }
    std::string execute() {
        process_detail::Process child(executable,cancelled,{limits.lifetime,limits.operation});
        uint64_t totalRead=0,totalWritten=0;Sha256 digest;
        {
            std::lock_guard<std::mutex> lock(mutex);check();state.workerId=child.id();state.code="running";
            const auto& r=snapshot.resources.at(resourceId);
            child.send(std::string("{\"mode\":\"")+workerMode()+"\",\"formatHint\":\"mp4\",\"length\":\""+std::to_string(r.length)+"\",\"complete\":"+(r.state=="complete"?"true":"false")+"}");
        }
        for(;;){
            {std::lock_guard<std::mutex> lock(mutex);check();}
            const auto line=child.receive();rapidjson::Document d;
            d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(line.data(),line.size());
            valid(!d.HasParseError() && d.IsObject());harvester::UniqueKeys(d);
            if(d.HasMember("result")) {
                Keys(d,"streamRemux",{"result"});auto& result=d["result"];
                valid(result.IsObject() && result.HasMember("code") && result["code"].IsString());
                const auto code=Text(result["code"],"streamRemux.code",64);
                if(code!="data")throw Error(code.c_str(),"streamRemux","Worker rejected stream");
                Keys(result,"streamRemux.result",{"code","mode","sourceComplete","sourceEof","continuousOutput","playbackSupported","transcoded","packets","bytesRead","bytesWritten","sha256","waits"});
                auto boolean=[&](const char* key,bool expected){valid(result[key].IsBool() && result[key].GetBool()==expected);};
                boolean("sourceComplete",true);boolean("sourceEof",true);boolean("continuousOutput",true);boolean("playbackSupported",false);boolean("transcoded",false);
                valid(Text(result["mode"],"mode")==workerMode() && result["packets"].IsUint() && result["packets"].GetUint()>0 && result["packets"].GetUint()<=50000);
                valid(totalWritten>0 && Unsigned(result["bytesWritten"],"bytesWritten",OutputLimit)==totalWritten && Unsigned(result["bytesRead"],"bytesRead",InputLimit)==totalRead && Text(result["sha256"],"sha256",64)==digest.hex());
                {
                    std::lock_guard<std::mutex> lock(mutex);check();observe();
                    valid(state.sourceComplete);source->verifyPinned();
                }
                valid(child.finish()==0);return lease_detail::Encode(result);
            }
            valid(d.HasMember("io") && d["io"].IsString());const auto io=Text(d["io"],"streamRemux.io",16);
            if(io=="wait") {
                Keys(d,"streamRemux.wait",{"io","reason","offset","length"});const auto reason=Text(d["reason"],"reason",32);
                valid(reason=="waiting-range" || reason=="source-complete");const auto offset=Unsigned(d["offset"],"offset",InputLimit);
                valid(d["length"].IsUint());const auto count=d["length"].GetUint();valid(reason=="source-complete"?count==0:count>0 && count<=65536);
                waitForSource(child,reason,offset,count);
                std::lock_guard<std::mutex> lock(mutex);check();child.send(std::string("{\"code\":\"resume\",\"complete\":")+(snapshot.resources.at(resourceId).state=="complete"?"true":"false")+"}");continue;
            }
            if(io=="write") {
                Keys(d,"streamRemux.write",{"io","offset","hex"});valid(Unsigned(d["offset"],"offset",OutputLimit)==totalWritten && d["hex"].IsString());
                const auto n=d["hex"].GetStringLength();valid(n>0 && n<=131072 && !(n&1) && n/2<=OutputLimit-totalWritten);
                std::vector<char> bytes(n/2);const auto* hex=d["hex"].GetString();
                auto digit=[](char c){return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:-1;};
                for(size_t i=0;i<bytes.size();++i){auto a=digit(hex[2*i]),b=digit(hex[2*i+1]);valid(a>=0 && b>=0);bytes[i]=char((a<<4)|b);}
                {
                    std::lock_guard<std::mutex> lock(mutex);check();source->verifyPinned();
                    sink(totalWritten,bytes);digest.update(bytes.data(),bytes.size());totalWritten+=bytes.size();state.bytesWritten=totalWritten;
                }
                child.send("{\"code\":\"data\"}");continue;
            }
            Keys(d,"streamRemux.read",{"io","offset","length"});valid(io=="read" && d["length"].IsUint());
            const auto at=Unsigned(d["offset"],"offset",InputLimit);const auto count=d["length"].GetUint();valid(count>0 && count<=65536);
            valid(totalRead<=InputLimit-count);std::vector<char> bytes(count);ResourceRead reply;
            {
                std::lock_guard<std::mutex> lock(mutex);reply=read(at,bytes.data(),count);
                if(reply.data()) {valid(reply.bytes>0 && reply.bytes<=count);totalRead+=reply.bytes;state.bytesRead=totalRead;}
            }
            if(!reply.data() && reply.code!="eof" && !waiting(reply.code))throw Error(reply.code.c_str(),"streamRemux",reply.reason.c_str());
            if(waiting(reply.code)){child.send("{\"code\":\"waiting-range\"}");continue;}
            if(reply.code=="eof"){child.send("{\"code\":\"eof\"}");continue;}
            const char* digits="0123456789abcdef";std::string hex;hex.reserve(reply.bytes*2);
            for(size_t i=0;i<reply.bytes;++i){auto b=static_cast<unsigned char>(bytes[i]);hex+=digits[b>>4];hex+=digits[b&15];}
            child.send("{\"code\":\"data\",\"hex\":\""+hex+"\"}");
        }
    }
    void run() noexcept {
        std::string code="data",result;
        try{result=execute();}catch(const Error& e){code=e.code;}catch(const std::exception& e){code=e.what();}catch(...){code="backend-parent-error";}
        std::lock_guard<std::mutex> lock(mutex);
        // execute's Process destructor has killed/reaped the child first. Media
        // handles close before filesClosed receipts; completion is visible last.
        observe();source.reset();state.filesClosed=true;state.result=std::move(result);
        state.code=code;state.done=true;state.reason.clear();changed.notify_all();
    }
public:
    StreamRemuxSession(const std::filesystem::path& directory,const std::filesystem::path& registry,
                       const std::string& json,const std::string& resource,const std::filesystem::path& child,
                       Sink output,StreamRemuxLimits policy={},StreamRemuxProfile selected=StreamRemuxProfile::H264Video)
        :snapshot(Parse(json)),resourceId(resource),executable(child),limits(policy),profile(selected),sink(std::move(output)) {
        Require(profile==StreamRemuxProfile::H264Video || profile==StreamRemuxProfile::H264ReorderedVideo,"streamRemux","Invalid internal profile","invalid_request");
        Require(bool(sink) && limits.lifetime.count()>0 && limits.lifetime<=std::chrono::hours(1) && limits.operation.count()>0 && limits.operation<=limits.lifetime && limits.heartbeat.count()>0 && limits.heartbeat<=std::chrono::seconds(30),"streamRemux","Invalid internal limits","invalid_request");
        Require(!snapshot.live && snapshot.resources.count(resourceId),"streamRemux","Expected local non-live resource","unsupported-stream-source");
        Require(snapshot.tracks.size()==1,"streamRemux","Expected one video-only track","unsupported-stream-source");
        const auto& track=snapshot.tracks.begin()->second;
        Require(track.kind=="video" && track.container=="mp4" && (track.codec.empty() || track.codec=="h264") && track.resource==resourceId && track.initialization.empty() && track.segments.empty(),"streamRemux","Unsupported continuous-remux track layout","unsupported-stream-source");
        const auto& r=snapshot.resources.at(resourceId);
        Require(r.known && r.length>0 && r.length<=InputLimit && r.integrity && !r.retired,"streamRemux","Known bounded SHA-protected source required","unsupported-stream-source");
        source=std::make_unique<ResourceSource>(directory,registry,json);
        deadline=Clock::now()+limits.lifetime;heartbeatDeadline=Clock::now()+limits.heartbeat;observe();
        worker=std::thread([this]{run();});
    }
    ~StreamRemuxSession(){cancel();if(worker.joinable())worker.join();}
    StreamRemuxSession(const StreamRemuxSession&)=delete;StreamRemuxSession& operator=(const StreamRemuxSession&)=delete;
    void publish(const std::string& json) {
        const auto next=Parse(json);std::lock_guard<std::mutex> lock(mutex);
        Require(!state.done,"streamRemux","Session already closed","session-closed");check();Advance(snapshot,next);
        // A known length may not change under the original demux context.
        Require(next.resources.at(resourceId).length==snapshot.resources.at(resourceId).length,"streamRemux","Source length changed","generation_changed");
        source->publish(json);snapshot=next;heartbeatDeadline=Clock::now()+limits.heartbeat;observe();changed.notify_all();
    }
    void heartbeat(){std::lock_guard<std::mutex> lock(mutex);Require(!state.done,"streamRemux","Session already closed","session-closed");check();heartbeatDeadline=Clock::now()+limits.heartbeat;changed.notify_all();}
    // A controller may have accepted a heartbeat just before producer EOF.
    // Test-and-renew under the same lock; terminal results must stay terminal.
    bool heartbeatWhileRunning(){std::lock_guard<std::mutex> lock(mutex);if(state.done)return false;check();heartbeatDeadline=Clock::now()+limits.heartbeat;changed.notify_all();return true;}
    void cancel(){cancelled.store(true);std::lock_guard<std::mutex> lock(mutex);if(source)source->cancel();changed.notify_all();}
    StreamRemuxStatus status()const{std::lock_guard<std::mutex> lock(mutex);return state;}
};
}}
