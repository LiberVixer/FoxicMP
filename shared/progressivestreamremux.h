// Internal managed continuous-remux parent. Not a public player capability.
// GPL-3.0-or-later.
#pragma once
#include "progressiveresourceio.h"
#include "progressiveprocess.h"
#include "progressivestreamaudio.h"
#include <functional>

namespace foxicmp { namespace progressive {
struct StreamRemuxLimits {
    std::chrono::milliseconds lifetime{3600000},operation{10000},heartbeat{30000};
};
// Explicit internal selection. Existing callers/public v1 retain the verified
// no-reorder profile; a manifest or worker reply cannot widen it implicitly.
enum class StreamRemuxProfile { H264Video, H264ReorderedVideo, H264AacWindow, HevcReorderedVideo, HevcAacWindow, Av1Video, Av1AacWindow, Vp9Video, Vp9AacWindow, Vp9SuperframeVideo, Vp9SuperframeAacWindow, WebmVp9Video, WebmVp9SuperframeVideo };
inline bool StreamRemuxHasAudio(StreamRemuxProfile profile){return profile==StreamRemuxProfile::H264AacWindow || profile==StreamRemuxProfile::HevcAacWindow || profile==StreamRemuxProfile::Av1AacWindow || profile==StreamRemuxProfile::Vp9AacWindow || profile==StreamRemuxProfile::Vp9SuperframeAacWindow;}
inline bool StreamRemuxIsHevc(StreamRemuxProfile profile){return profile==StreamRemuxProfile::HevcReorderedVideo || profile==StreamRemuxProfile::HevcAacWindow;}
inline bool StreamRemuxIsAv1(StreamRemuxProfile profile){return profile==StreamRemuxProfile::Av1Video || profile==StreamRemuxProfile::Av1AacWindow;}
inline bool StreamRemuxIsWebm(StreamRemuxProfile profile){return profile==StreamRemuxProfile::WebmVp9Video || profile==StreamRemuxProfile::WebmVp9SuperframeVideo;}
inline bool StreamRemuxIsVp9(StreamRemuxProfile profile){return profile==StreamRemuxProfile::Vp9Video || profile==StreamRemuxProfile::Vp9AacWindow || profile==StreamRemuxProfile::Vp9SuperframeVideo || profile==StreamRemuxProfile::Vp9SuperframeAacWindow || StreamRemuxIsWebm(profile);}
inline const char* StreamRemuxMode(StreamRemuxProfile profile){return profile==StreamRemuxProfile::WebmVp9SuperframeVideo?
    "continuous-webm-vp9-superframe-video-v1":profile==StreamRemuxProfile::WebmVp9Video?
    "continuous-webm-vp9-video-v1":profile==StreamRemuxProfile::Vp9SuperframeAacWindow?
    "continuous-vp9-superframe-aac-window-v1":profile==StreamRemuxProfile::Vp9SuperframeVideo?
    "continuous-vp9-superframe-video-v1":profile==StreamRemuxProfile::Vp9AacWindow?
    "continuous-vp9-aac-window-v1":profile==StreamRemuxProfile::Vp9Video?
    "continuous-vp9-video-v1":profile==StreamRemuxProfile::Av1AacWindow?
    "continuous-av1-aac-window-v1":profile==StreamRemuxProfile::Av1Video?
    "continuous-av1-video-v1":profile==StreamRemuxProfile::HevcAacWindow?
    "continuous-hevc-aac-window-v1":profile==StreamRemuxProfile::HevcReorderedVideo?
    "continuous-hevc-reordered-video-v1":profile==StreamRemuxProfile::H264AacWindow?
    "continuous-h264-aac-window-v1":profile==StreamRemuxProfile::H264ReorderedVideo?
    "continuous-h264-reordered-video-v1":"continuous-h264-video-v1";}
struct StreamRemuxStatus {
    std::string code="starting",reason,result,appliedRevision;
    uint64_t workerId=0,bytesRead=0,bytesWritten=0,maximumReadEnd=0,neededOffset=0;
    unsigned neededLength=0;
    bool done=false,filesClosed=false,sourceComplete=false;
    std::vector<std::string> readerIds;
    std::string audioWindow;
};
// The sink is an INTERNAL bounded, nonblocking append interface, not an arbitrary
// application callback. It must throw on failure and must never retain references
// into the supplied chunk. Persistent cache + decoder reader ownership are separate.
class StreamRemuxSession {
public:
    using Sink=std::function<void(uint64_t,const std::vector<char>&)>;
    using AudioWindowSink=std::function<void(const StreamAudioWindow&)>;
private:
    using Clock=std::chrono::steady_clock;
    static constexpr uint64_t InputLimit=128*1024*1024,OutputLimit=64*1024*1024;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::atomic<bool> cancelled{false};
    std::unique_ptr<ResourceSource> source;
    bool closingSource=false;
    Snapshot snapshot;
    std::string resourceId;
    std::filesystem::path executable;
    StreamRemuxLimits limits;
    StreamRemuxProfile profile;
    Sink sink;
    AudioWindowSink audioSink;
    StreamRemuxStatus state;
    Clock::time_point deadline,heartbeatDeadline;
    std::thread worker;
    const char* workerMode()const{return StreamRemuxMode(profile);}
    // All accesses to snapshot/source/status are under mutex, including release.
    void check() const {
        Require(!closingSource,"streamRemux","Source is closing","session-closed");
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
        uint64_t totalRead=0,totalWritten=0;Sha256 digest;StreamAudioWindow audio;
        const bool hasAudio=StreamRemuxHasAudio(profile);
        {
            std::lock_guard<std::mutex> lock(mutex);check();state.workerId=child.id();state.code="running";
            const auto& r=snapshot.resources.at(resourceId);
            const bool webm=StreamRemuxIsWebm(profile);
            child.send(std::string("{\"mode\":\"")+workerMode()+"\",\"formatHint\":\""+(webm?"webm":"mp4")+"\",\"length\":\""+std::to_string(r.length)+"\",\"complete\":"+(r.state=="complete"?"true":"false")+(webm?",\"videoSamples\":\""+std::to_string(snapshot.duration/400000)+"\"":"")+"}");
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
                if(hasAudio)Keys(result,"streamRemux.result",{"code","mode","sourceComplete","sourceEof","continuousOutput","playbackSupported","transcoded","packets","bytesRead","bytesWritten","sha256","waits","videoPackets","audioPackets","aacInitialSkipSamples","audioPresentationSamples","aacEndTrimRequired","pcmWindowRequired","aacPcmWindow"});
                else Keys(result,"streamRemux.result",{"code","mode","sourceComplete","sourceEof","continuousOutput","playbackSupported","transcoded","packets","bytesRead","bytesWritten","sha256","waits"});
                auto boolean=[&](const char* key,bool expected){valid(result[key].IsBool() && result[key].GetBool()==expected);};
                boolean("sourceComplete",true);boolean("sourceEof",true);boolean("continuousOutput",true);boolean("playbackSupported",false);boolean("transcoded",false);
                valid(Text(result["mode"],"mode")==workerMode() && result["packets"].IsUint() && result["packets"].GetUint()>0 && result["packets"].GetUint()<=50000);
                valid(totalWritten>0 && Unsigned(result["bytesWritten"],"bytesWritten",OutputLimit)==totalWritten && Unsigned(result["bytesRead"],"bytesRead",InputLimit)==totalRead && Text(result["sha256"],"sha256",64)==digest.hex());
                if(hasAudio){
                    valid(audio.pcm.Valid() && StreamAudioWindow::Parse(result["aacPcmWindow"]).json==audio.json);
                    boolean("pcmWindowRequired",true);boolean("aacEndTrimRequired",audio.pcm.EndPadding()!=0);
                    valid(result["videoPackets"].IsUint() && result["videoPackets"].GetUint()>0 && result["audioPackets"].IsUint() && result["audioPackets"].GetUint()==audio.pcm.PacketCount());
                    valid(result["packets"].GetUint()==uint64_t(result["videoPackets"].GetUint())+result["audioPackets"].GetUint());
                    valid(result["aacInitialSkipSamples"].IsUint() && result["aacInitialSkipSamples"].GetUint()==audio.pcm.RawBegin() && Unsigned(result["audioPresentationSamples"],"audioPresentationSamples")==audio.pcm.Samples());
                }
                {
                    std::lock_guard<std::mutex> lock(mutex);check();observe();
                    valid(state.sourceComplete);source->verifyPinned();
                }
                valid(child.finish()==0);return lease_detail::Encode(result);
            }
            valid(d.HasMember("io") && d["io"].IsString());const auto io=Text(d["io"],"streamRemux.io",16);
            if(io=="audio-window"){
                Keys(d,"streamRemux.audioWindow",{"io","window","sha256"});valid(hasAudio && !audio.pcm.Valid() && totalWritten==0);
                auto plan=StreamAudioWindow::Parse(d["window"]);valid(Text(d["sha256"],"sha256",64)==plan.sha256);
                {std::lock_guard<std::mutex> lock(mutex);check();source->verifyPinned();audioSink(plan);audio=plan;state.audioWindow=plan.json;}
                child.send("{\"code\":\"audio-window-accepted\",\"sha256\":\""+plan.sha256+"\"}");continue;
            }
            if(io=="wait") {
                Keys(d,"streamRemux.wait",{"io","reason","offset","length"});const auto reason=Text(d["reason"],"reason",32);
                valid(reason=="waiting-range" || reason=="source-complete");const auto offset=Unsigned(d["offset"],"offset",InputLimit);
                valid(d["length"].IsUint());const auto count=d["length"].GetUint();valid(reason=="source-complete"?count==0:count>0 && count<=65536);
                waitForSource(child,reason,offset,count);
                std::lock_guard<std::mutex> lock(mutex);check();child.send(std::string("{\"code\":\"resume\",\"complete\":")+(snapshot.resources.at(resourceId).state=="complete"?"true":"false")+"}");continue;
            }
            if(io=="write") {
                valid(!hasAudio || audio.pcm.Valid());
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
        std::unique_ptr<ResourceSource> closing;
        {
            std::lock_guard<std::mutex> lock(mutex);
            // execute's Process destructor has killed/reaped the child first.
            // Freeze the last snapshot before transferring sole ownership;
            // status/cancel must not wait behind durable receipt persistence.
            observe();closingSource=true;closing=std::move(source);
            state.code=code;state.reason="source-receipt-pending";changed.notify_all();
        }
        closing->closeMedia();closing.reset();
        {
            std::lock_guard<std::mutex> lock(mutex);
            state.filesClosed=true;state.result=std::move(result);
            state.done=true;state.reason.clear();changed.notify_all();
        }
    }
public:
    StreamRemuxSession(const std::filesystem::path& directory,const std::filesystem::path& registry,
                       const std::string& json,const std::string& resource,const std::filesystem::path& child,
                       Sink output,StreamRemuxLimits policy={},StreamRemuxProfile selected=StreamRemuxProfile::H264Video,AudioWindowSink windowSink={})
        :snapshot(Parse(json)),resourceId(resource),executable(child),limits(policy),profile(selected),sink(std::move(output)),audioSink(std::move(windowSink)) {
        Require(profile==StreamRemuxProfile::H264Video || profile==StreamRemuxProfile::H264ReorderedVideo || profile==StreamRemuxProfile::H264AacWindow || profile==StreamRemuxProfile::HevcReorderedVideo || profile==StreamRemuxProfile::HevcAacWindow || StreamRemuxIsAv1(profile) || StreamRemuxIsVp9(profile),"streamRemux","Invalid internal profile","invalid_request");
        Require(bool(sink) && limits.lifetime.count()>0 && limits.lifetime<=std::chrono::hours(1) && limits.operation.count()>0 && limits.operation<=limits.lifetime && limits.heartbeat.count()>0 && limits.heartbeat<=std::chrono::seconds(30),"streamRemux","Invalid internal limits","invalid_request");
        Require(!snapshot.live && snapshot.resources.count(resourceId),"streamRemux","Expected local non-live resource","unsupported-stream-source");
        const bool hasAudio=StreamRemuxHasAudio(profile);
        const bool hevc=StreamRemuxIsHevc(profile),av1=StreamRemuxIsAv1(profile),vp9=StreamRemuxIsVp9(profile);
        Require(!hasAudio || bool(audioSink),"streamRemux","AAC window consumer required","invalid_request");
        Require(snapshot.tracks.size()==(hasAudio?2u:1u),"streamRemux","Unexpected continuous track count","unsupported-stream-source");
        std::set<std::string> kinds;
        for(const auto& item:snapshot.tracks){const auto& track=item.second;
            Require((track.kind=="video" || (hasAudio && track.kind=="audio")) && kinds.insert(track.kind).second && track.container==(StreamRemuxIsWebm(profile)?"webm":"mp4") && ((hevc || av1 || vp9)?track.codec==(track.kind=="video"?(vp9?"vp9":av1?"av1":"hevc"):"aac-lc"):(track.codec.empty() || track.codec==(track.kind=="video"?"h264":"aac-lc"))) && track.resource==resourceId && track.initialization.empty() && track.segments.empty() && (!(hasAudio || hevc || av1 || vp9) || track.required),"streamRemux","Unsupported continuous-remux track layout","unsupported-stream-source");
        }
        if(hasAudio || hevc || av1 || vp9)Require(snapshot.resources.size()==1 && snapshot.knownDuration && snapshot.numerator==1 && snapshot.denominator==10000000 && snapshot.offset==0 && !snapshot.hasWindow,"streamRemux","Known-duration local source required","unsupported-stream-source");
        if(hevc || av1 || vp9)Require(snapshot.duration>0 && snapshot.duration<=36000000000LL,"streamRemux","Bounded VOD duration required","unsupported-stream-source");
        if(StreamRemuxIsWebm(profile))Require(snapshot.duration%400000==0 && snapshot.duration/400000<=50000,"streamRemux","Exact bounded 25fps timeline required","unsupported-stream-source");
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
    bool heartbeatWhileRunning(){std::lock_guard<std::mutex> lock(mutex);if(state.done || closingSource)return false;check();heartbeatDeadline=Clock::now()+limits.heartbeat;changed.notify_all();return true;}
    void cancel(){cancelled.store(true);std::lock_guard<std::mutex> lock(mutex);if(source)source->cancel();changed.notify_all();}
    StreamRemuxStatus status()const{std::lock_guard<std::mutex> lock(mutex);return state;}
};
}}
