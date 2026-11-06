// Shared internal continuous-remux preparation controller; no playback commit.
// All filesystem/producer work is off the calling thread. GPL-3.0-or-later.
#pragma once
#include "progressivecachedremux.h"
#include "progressiveretirement.h"
#include <deque>

namespace foxicmp { namespace progressive {
inline bool ContinuousRemuxAction(const std::string& action){
    return action=="continuousRemuxStart" || action=="continuousRemuxStatus" || action=="continuousRemuxUpdate"
        || action=="continuousRemuxHeartbeat" || action=="continuousRemuxCancel" || action=="continuousRemuxForget";
}
struct ContinuousRetirementPolicy {
    static constexpr size_t Capacity=8,WorkerLimit=2;
    struct Busy:std::runtime_error{Busy():std::runtime_error("Continuous remux jobs are still closing"){};};
};
using ContinuousRetirement=ObjectRetirement<ContinuousRetirementPolicy>;
struct ContinuousControllerHooks {std::function<void()> prepare,publish;}; // Private deterministic I/O fixtures.
class ContinuousRemuxController {
    using Clock=std::chrono::steady_clock;
    static std::atomic<bool>& jobsStarted(){static std::atomic<bool> value{false};return value;}
    static int64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
    static void profile(const Snapshot& s,const std::string& resource,StreamRemuxProfile selected){
        Require(!s.live && !s.hasWindow && s.knownDuration && s.duration>0 && s.duration<=36000000000LL && s.numerator==1 && s.denominator==10000000 && s.offset==0,
            "continuousRemux","Known zero-origin VOD clock required","unsupported-stream-source");
        const bool audio=selected==StreamRemuxProfile::H264AacWindow;
        Require(s.resources.size()==1 && s.resources.count(resource) && s.tracks.size()==(audio?2u:1u),"continuousRemux","Unexpected scalar MP4 track layout","unsupported-stream-source");
        std::set<std::string> kinds;
        for(const auto& item:s.tracks){const auto& t=item.second;
            Require((t.kind=="video" || (audio && t.kind=="audio")) && kinds.insert(t.kind).second && t.required && t.container=="mp4"
                && (t.codec.empty() || t.codec==(t.kind=="video"?"h264":"aac-lc")) && t.resource==resource && t.initialization.empty() && t.segments.empty(),
                "continuousRemux","Unsupported continuous copy-remux track","unsupported-stream-source");
        }
        const auto& r=s.resources.at(resource);
        Require(r.known && r.length>0 && r.length<=128*1024*1024 && r.integrity && !r.retired,"continuousRemux","Unsupported continuous copy-remux resource","unsupported-stream-source");
    }
    static void healthy(const Snapshot& s,const std::string& resource){
        const auto bad=[](const std::string& state){return state=="failed" || state=="cancelled";};
        Require(!bad(s.state),"continuousRemux","Writer failed","writer-failed");
        for(const auto& item:s.tracks)Require(!bad(item.second.state),"continuousRemux","Required track failed","writer-failed");
        Require(!bad(s.resources.at(resource).state),"continuousRemux","Required resource failed","resource-failed");
    }
    struct View {
        std::string phase="preparing",error,cleanup;
        CachedRemuxStatus native;
        bool terminal=false,released=false,forgotten=false;
        uint64_t appliedHeartbeat=0;
        std::shared_ptr<StreamDecoderSource> decoder;
    };
    struct Job {
        const std::string id=lease_detail::RandomId();
        std::string requestId,fingerprint,initial,resource;
        Snapshot accepted;
        bool initiallyComplete=false;
        std::filesystem::path directory,registry,cache,child;
        std::shared_ptr<StreamCacheOwner> owner;
        StreamRemuxLimits limits;
        StreamRemuxProfile selected=StreamRemuxProfile::H264Video;
        ContinuousControllerHooks hooks;
        std::mutex mutex;std::condition_variable changed;std::deque<std::string> updates;
        std::atomic<bool> cancel{false},forget{false},stop{false};
        std::atomic<int64_t> heartbeatAt{now()};std::atomic<uint64_t> heartbeatSequence{0};
        const Clock::time_point admitted=Clock::now();
        std::shared_ptr<const View> current=std::make_shared<View>();
        std::thread thread;
        std::shared_ptr<const View> view()const{return std::atomic_load(&current);}
        void expose(const View& v){std::shared_ptr<const View> copy=std::make_shared<View>(v);std::atomic_store(&current,std::move(copy));}
        void wake(){changed.notify_all();}
        void requestCancel(){cancel.store(true);wake();}
        ~Job()noexcept{stop.store(true);requestCancel();if(thread.joinable())thread.join();}
        void run()noexcept{
            View v;std::unique_ptr<CachedRemuxSession> session;std::shared_ptr<StreamDecoderSource> decoder;std::set<std::string> acknowledged;
            const auto began=admitted;
            auto expired=[&]{return now()-heartbeatAt.load()>=limits.heartbeat.count();};
            try{
                if(hooks.prepare)hooks.prepare();
                Require(!cancel.load() && !stop.load(),"continuousRemux","Preparation cancelled","cancelled");
                Require(Clock::now()-began<limits.lifetime,"continuousRemux","Controller lifetime expired during preparation","backend-time-limit");
                Require(initiallyComplete || now()-heartbeatAt.load()<limits.heartbeat.count(),"continuousRemux","Writer heartbeat expired during preparation","writer-heartbeat-timeout");
                session=std::make_unique<CachedRemuxSession>(directory,registry,cache,initial,resource,child,owner,64*1024*1024,limits,selected);
                if(selected!=StreamRemuxProfile::H264AacWindow)decoder=session->decoderSource();
            }catch(const Error& e){v.error=e.code;v.terminal=true;}catch(...){v.error="continuous-preparation-failed";v.terminal=true;}
            for(;;)try{
                if(stop.load() || cancel.load() || forget.load()){
                    if(decoder)decoder->cancel();if(session)session->cancel();v.decoder.reset();
                }
                if(!v.terminal && !cancel.load() && !stop.load() && !forget.load()){
                    Require(Clock::now()-began<limits.lifetime,"continuousRemux","Controller lifetime expired","backend-time-limit");
                    // Copy one command under the admission mutex, then perform I/O
                    // without it. accepted/applied revisions intentionally differ.
                    std::string update;bool writerComplete=false;
                    {std::lock_guard<std::mutex> lock(mutex);writerComplete=accepted.resources.at(resource).state=="complete";if(!updates.empty()){update=std::move(updates.front());updates.pop_front();}}
                    Require(writerComplete || !expired(),"continuousRemux","Writer heartbeat expired","writer-heartbeat-timeout");
                    if(!update.empty()){if(hooks.publish)hooks.publish();if(!cancel.load() && !stop.load()){session->publish(update);healthy(Parse(update),resource);}}
                    const auto sequence=heartbeatSequence.load();
                    if(sequence!=v.appliedHeartbeat && !cancel.load() && !stop.load()){
                        // Complete may race a queued heartbeat; never turn that
                        // already confirmed completion into a heartbeat failure.
                        session->heartbeatWhileRunning();v.appliedHeartbeat=sequence;
                    }
                }
                if(session){
                    v.native=session->status();
                    // AAC admission needs the immutable, SHA-validated window.
                    // Status/publications stay live while initialization is pending.
                    if(!decoder && selected==StreamRemuxProfile::H264AacWindow && !cancel.load() && !stop.load() && !forget.load()
                        && v.error.empty() && v.native.output.initialized && v.native.output.fragments && v.native.output.failure.empty())decoder=session->decoderSource(true);
                    if(v.native.done){v.terminal=true;if(v.native.source.code!="data" && v.error.empty())v.error=v.native.source.code;}
                    if((!v.terminal || v.native.source.code=="data") && !cancel.load() && !stop.load() && !forget.load() && v.native.output.initialized && v.native.output.fragments && v.native.output.failure.empty())v.decoder=decoder;
                    if(!v.error.empty()){if(decoder)decoder->cancel();v.decoder.reset();session->cancel();}
                }
                const bool retiring=stop.load() || cancel.load() || forget.load() || !v.error.empty();
                if(retiring){
                    v.decoder.reset();
                    if(decoder && !v.released) {
                        // Publish failure/cancellation before potentially slow
                        // file/receipt I/O. Do not wait for the UI/mpv close_fn
                        // to retire cancelled decoder cursors' native handles.
                        v.phase=!v.error.empty() && v.error!="cancelled"?"error":"cancelling";
                        expose(v);decoder->closeCancelledDecoderReaders();
                    }
                    if(session && !v.native.done){v.phase="cancelling";}
                    else {
                        if(session)session->retire();
                        v.released=owner->settle();
                        if(session)v.native=session->status();
                        else if(v.released)v.native.source.filesClosed=true;
                        v.cleanup=v.released?"released":v.native.output.cleanupCode.empty()?"cache-recovery-required":v.native.output.cleanupCode;
                    }
                }
                if(v.terminal || (!session && retiring)){
                    v.terminal=true;v.phase=!v.error.empty() && v.error!="cancelled"?"error":retiring?"cancelled":"complete";
                }else if(!retiring)v.phase=v.decoder?"ready":v.native.source.code=="waiting"?"waiting":"preparing";
                if(forget.load() && v.released){
                    v.phase="forgetting";
                    auto ids=v.native.source.readerIds;ids.insert(ids.end(),v.native.output.readerIds.begin(),v.native.output.readerIds.end());
                    for(const auto& id:ids)if(!acknowledged.count(id)){
                        rapidjson::Document reply;reply.Parse(ReaderRequest(registry,"{\"version\":1,\"action\":\"readerForget\",\"readerId\":\""+id+"\"}").c_str());
                        // A client may already have acknowledged readerRelease
                        // with readerForget. Absence is NOT a release proof:
                        // permit idempotent cleanup only after independent
                        // source close, zero output readers and cache removal.
                        const bool alreadyAcknowledged=reply.IsObject() && reply.HasMember("code") && reply["code"].IsString()
                            && std::string(reply["code"].GetString())=="unknown_reader" && v.native.source.filesClosed
                            && v.native.output.removed && v.native.output.activeReaders==0;
                        Require(reply.IsObject() && reply.HasMember("ok") && (reply["ok"].GetBool() || alreadyAcknowledged),"continuousRemux","Wait for reader receipt acknowledgement","receipt-cleanup-failed");acknowledged.insert(id);
                    }
                    session.reset();decoder.reset();v.phase="forgotten";v.forgotten=true;v.decoder.reset();expose(v);return;
                }
                expose(v);
                if(stop.load() && v.released){session.reset();return;}
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait_for(lock,std::chrono::milliseconds(25));
            }catch(const Error& e){v.error=e.code;v.terminal=true;v.decoder.reset();if(decoder)decoder->cancel();if(session)session->cancel();expose(v);}
            catch(...){v.error="continuous-controller-error";v.terminal=true;v.decoder.reset();if(decoder)decoder->cancel();if(session)session->cancel();expose(v);}
        }
        std::string status(){
            const auto v=view();Snapshot admitted;size_t queued=0;
            {std::lock_guard<std::mutex> lock(mutex);admitted=accepted;queued=updates.size();}
            rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("version",1,a);out.AddMember("ok",true,a);out.AddMember("mode",Value(StreamRemuxMode(selected),a),a);
            out.AddMember("audio",selected==StreamRemuxProfile::H264AacWindow,a);
            out.AddMember("audioWindowReady",v->decoder && v->decoder->audioWindow(),a);
            if(v->decoder && v->decoder->audioWindow()){
                const auto& pcm=v->decoder->audioWindow()->pcm;
                out.AddMember("audioSampleRate",pcm.Rate(),a);out.AddMember("audioChannels",pcm.Channels(),a);
                out.AddMember("audioPresentationSamples",Value(std::to_string(pcm.Samples()).c_str(),a),a);
            }
            auto text=[&](const char* key,const std::string& value){out.AddMember(Value(key,a),Value(value.c_str(),a),a);};
            text("jobId",id);text("requestId",requestId);text("sessionId",admitted.id);text("generation",std::to_string(admitted.generation));text("writerInstanceId",admitted.writer);
            text("state",forget.load() && !v->forgotten?"forgetting":cancel.load() && !v->released?"cancelling":v->phase);text("errorCode",v->error);text("cleanupCode",v->cleanup);
            text("acceptedRevision",std::to_string(admitted.revision));text("appliedRevision",v->native.source.appliedRevision);
            text("acceptedHeartbeat",std::to_string(heartbeatSequence.load()));text("appliedHeartbeat",std::to_string(v->appliedHeartbeat));
            text("producerCode",v->native.source.code);text("neededOffset",std::to_string(v->native.source.neededOffset));text("visibleBytes",std::to_string(v->native.output.visible));text("writtenBytes",std::to_string(v->native.output.written));
            text("maximumReadEnd",std::to_string(v->native.source.maximumReadEnd));out.AddMember("fragments",v->native.output.fragments,a);out.AddMember("samples",v->native.output.samples,a);
            out.AddMember("queuedUpdates",uint64_t(queued),a);out.AddMember("neededLength",v->native.source.neededLength,a);out.AddMember("workerId",v->native.source.workerId,a);
            out.AddMember("playbackReady",bool(v->decoder) && !cancel.load() && !forget.load() && !stop.load(),a);out.AddMember("sourceComplete",v->native.source.sourceComplete,a);out.AddMember("outputComplete",v->native.output.complete,a);
            out.AddMember("sourceFilesClosed",v->native.source.filesClosed,a);out.AddMember("released",v->released,a);out.AddMember("forgotten",v->forgotten,a);
            Value ids(rapidjson::kArrayType);for(const auto& id:v->native.source.readerIds)ids.PushBack(Value(id.c_str(),a),a);for(const auto& id:v->native.output.readerIds)ids.PushBack(Value(id.c_str(),a),a);out.AddMember("readerIds",ids,a);
            return lease_detail::Encode(out);
        }
    };
    std::mutex dispatch;
    std::shared_ptr<Job> job;
    std::filesystem::path registry,cache,child;
    StreamRemuxLimits limits;
    ContinuousControllerHooks hooks;
    std::shared_ptr<StreamCacheOwner> serviceOwner=std::make_shared<StreamCacheOwner>(std::make_shared<StreamCacheBudget>());
    static std::string failure(const char* code,const char* why){rapidjson::Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("version",1,a);d.AddMember("ok",false,a);d.AddMember("code",Value(code,a),a);d.AddMember("error",Value(why,a),a);return lease_detail::Encode(d);}
public:
    ContinuousRemuxController(std::filesystem::path receipts,std::filesystem::path output,std::filesystem::path executable,StreamRemuxLimits policy={},ContinuousControllerHooks fixture={})
        :registry(std::move(receipts)),cache(std::move(output)),child(std::move(executable)),limits(policy),hooks(std::move(fixture)){}
    ~ContinuousRemuxController(){if(job){job->stop.store(true);job->requestCancel();}}
    // Final process exit only, after GUI/API/decoder owners are gone. Ordinary
    // destruction remains asynchronous; timeout keeps conservative recovery fences.
    static bool settleOnExit(std::chrono::milliseconds timeout){
        return !jobsStarted().load() || ContinuousRetirement::waitEmpty(timeout);
    }
    void cancelPlaybackJob(const std::string& id){std::lock_guard<std::mutex> lock(dispatch);if(job && job->id==id)job->requestCancel();}
    std::function<StreamCacheStatus()> outputObserver(const std::string& id){
        std::lock_guard<std::mutex> lock(dispatch);Require(job && job->id==id,"continuousPlayback","Unknown remux job","unknown-continuous-job");
        return [retained=job]{return retained->view()->native.output;};
    }
    std::function<std::string()> errorObserver(const std::string& id){
        std::lock_guard<std::mutex> lock(dispatch);Require(job && job->id==id,"continuousPlayback","Unknown remux job","unknown-continuous-job");
        return [retained=job]{return retained->view()->error;};
    }
    std::shared_ptr<StreamDecoderSource> playbackSource(const std::string& id,const std::string& session,uint64_t generation,const std::string& writer,int64_t& duration){
        std::lock_guard<std::mutex> lock(dispatch);
        Require(job && job->id==id,"continuousPlayback","Unknown remux job","unknown-continuous-job");
        Require(job->accepted.id==session && job->accepted.generation==generation && job->accepted.writer==writer,"continuousPlayback","Session/writer changed","generation_changed");
        auto v=job->view();Require(!job->cancel.load() && !job->forget.load() && v->error.empty(),"continuousPlayback","Remux job is closed","continuous-closed");
        Require(bool(v->decoder),"continuousPlayback","Wait for initialization and media","continuous-not-ready");duration=job->accepted.duration;return v->decoder;
    }
    std::shared_ptr<StreamDecoderSource> decoder(const std::string& id){
        std::lock_guard<std::mutex> lock(dispatch);if(!job || job->id!=id || job->cancel.load() || job->forget.load())return {};return job->view()->decoder;
    }
    std::string request(const std::string& json){
        std::lock_guard<std::mutex> lock(dispatch);
        try{
            Require(json.size()<=65536,"continuousRemux","Request exceeds 64 KiB","invalid_request");rapidjson::Document d;d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(json.data(),json.size());
            Require(!d.HasParseError() && d.IsObject(),"continuousRemux","Invalid command JSON","invalid_request");InlineKeys(d);
            Require(d.HasMember("version") && d["version"].IsUint() && d["version"].GetUint()==1 && d.HasMember("action"),"continuousRemux","Expected API version 1","invalid_request");const auto action=Text(d["action"],"action",32);
            if(action=="continuousRemuxStart"){
                Keys(d,"continuousRemux",{"version","action","requestId","sessionDirectory","session","resourceId"},{"mode"});
                // Omission preserves the original profile. Explicit selection
                // is immutable and forms part of the replay fingerprint.
                const auto mode=d.HasMember("mode")?Text(d["mode"],"mode",64):"continuous-h264-video-v1";
                Require(mode=="continuous-h264-video-v1" || mode=="continuous-h264-reordered-video-v1" || mode=="continuous-h264-aac-window-v1","mode","Unsupported continuous mode","invalid_request");
                const auto selected=mode=="continuous-h264-aac-window-v1"?StreamRemuxProfile::H264AacWindow:mode=="continuous-h264-reordered-video-v1"?StreamRemuxProfile::H264ReorderedVideo:StreamRemuxProfile::H264Video;
                const auto rid=Id(d["requestId"],"requestId",true);rapidjson::StringBuffer encoded;rapidjson::Writer<rapidjson::StringBuffer> writer(encoded);Canonical(d,writer);const std::string fingerprint(encoded.GetString(),encoded.GetSize());
                if(job && job->requestId==rid){Require(job->fingerprint==fingerprint,"requestId","Request identity conflict","continuous-conflict");return job->status();}
                Require(!job || job->view()->forgotten,"continuousRemux","Forget the previous job first","continuous-busy");
                const auto initial=lease_detail::Encode(d["session"]);const auto parsed=Parse(initial);const auto resource=Id(d["resourceId"],"resourceId");profile(parsed,resource,selected);
                healthy(parsed,resource);
                const auto directory=std::filesystem::u8path(Text(d["sessionDirectory"],"sessionDirectory",8192));Require(directory.is_absolute(),"sessionDirectory","Absolute local directory required","invalid_request");
                auto next=ContinuousRetirement::create<Job>([]{return new Job;});next->requestId=rid;next->fingerprint=fingerprint;next->initial=initial;next->accepted=parsed;next->initiallyComplete=parsed.resources.at(resource).state=="complete";next->resource=resource;next->directory=directory;next->registry=registry;next->cache=cache;next->child=child;next->limits=limits;next->hooks=hooks;next->owner=serviceOwner;
                next->selected=selected;
                next->thread=std::thread([raw=next.get()]{raw->run();});job=std::move(next);jobsStarted().store(true);return job->status();
            }
            if(action=="continuousRemuxStatus"){
                Keys(d,"continuousRemux",{"version","action"},{"jobId","requestId"});Require(d.HasMember("jobId")!=d.HasMember("requestId"),"continuousRemux","Specify one status identity","invalid_request");
            }else if(action=="continuousRemuxUpdate")Keys(d,"continuousRemux",{"version","action","jobId","session"});
            else{Require(action=="continuousRemuxHeartbeat" || action=="continuousRemuxCancel" || action=="continuousRemuxForget","continuousRemux","Unknown command","invalid_request");Keys(d,"continuousRemux",{"version","action","jobId","sessionId","generation","writerInstanceId"});}
            Require(bool(job),"continuousRemux","Unknown job","unknown-continuous-job");
            if(d.HasMember("jobId"))Require(Id(d["jobId"],"jobId",true)==job->id,"continuousRemux","Wrong job identity","unknown-continuous-job");
            else Require(Id(d["requestId"],"requestId",true)==job->requestId,"continuousRemux","Wrong request identity","unknown-continuous-job");
            if(action!="continuousRemuxStatus" && action!="continuousRemuxUpdate"){
                Require(Id(d["sessionId"],"sessionId",true)==job->accepted.id && Unsigned(d["generation"],"generation")==job->accepted.generation && Id(d["writerInstanceId"],"writerInstanceId",true)==job->accepted.writer,"continuousRemux","Session/writer changed","generation_changed");
            }
            if(action=="continuousRemuxCancel")job->requestCancel();
            if(action=="continuousRemuxForget"){job->forget.store(true);job->requestCancel();}
            if(action=="continuousRemuxUpdate"){
                const auto raw=lease_detail::Encode(d["session"]);const auto next=Parse(raw);profile(next,job->resource,job->selected);std::lock_guard<std::mutex> lock(job->mutex);
                Require(!job->cancel.load() && !job->forget.load() && !job->view()->terminal,"continuousRemux","Job no longer accepts updates","continuous-closed");
                Require(job->updates.size()<8,"continuousRemux","Update queue is full","continuous-backpressure");Advance(job->accepted,next);
                job->updates.push_back(raw);job->accepted=next;job->heartbeatAt.store(now());job->wake();
            }
            if(action=="continuousRemuxHeartbeat"){
                Require(!job->cancel.load() && !job->forget.load() && !job->view()->terminal,"continuousRemux","Job no longer accepts heartbeat","continuous-closed");job->heartbeatAt.store(now());job->heartbeatSequence.fetch_add(1);job->wake();
            }
            return job->status();
        }catch(const ContinuousRetirementPolicy::Busy& e){return failure("continuous-busy",e.what());}
        catch(const Error& e){return failure(e.code.c_str(),e.what());}
        catch(...){return failure("continuous-controller-error","Cannot handle continuous remux command");}
    }
};
}}
