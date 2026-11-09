// Internal playback commands for the narrow continuous remux profile.
// No public capability/SDK claim. GPL-3.0-or-later.
#pragma once
#include "progressivestreamcontroller.h"
#include <cmath>
namespace foxicmp { namespace progressive {
inline bool ContinuousPlaybackAction(const std::string& action){return action=="continuousRemuxPlay" || action=="continuousPlaybackStatus" || action=="continuousPlaybackControl";}
struct ContinuousPlaybackCommand {
    std::string action,id,requestId,fingerprint,jobId,sessionId,writer,title,command;
    uint64_t generation=0;double seconds=0,startSeconds=0;bool replace=false,activate=false,byRequest=false,allowGrowing=false,waitForRange=false;
};
inline ContinuousPlaybackCommand ParseContinuousPlayback(const std::string& json){
    Require(json.size()<=65536,"continuousPlayback","Request exceeds 64 KiB","invalid_request");
    rapidjson::Document d;d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(json.data(),json.size());
    Require(!d.HasParseError() && d.IsObject(),"continuousPlayback","Invalid JSON","invalid_request");InlineKeys(d);
    Require(d.HasMember("version") && d["version"].IsUint() && d["version"].GetUint()==1 && d.HasMember("action"),"continuousPlayback","Expected version 1","invalid_request");
    ContinuousPlaybackCommand c;c.action=Text(d["action"],"action",32);
    if(c.action=="continuousRemuxPlay"){
        Keys(d,"continuousPlayback",{"version","action","jobId","requestId","sessionId","generation","writerInstanceId"},{"title","replace","activate","startSeconds"});
        c.jobId=Id(d["jobId"],"jobId",true);c.requestId=Id(d["requestId"],"requestId",true);c.sessionId=Id(d["sessionId"],"sessionId",true);
        c.generation=Unsigned(d["generation"],"generation");c.writer=Id(d["writerInstanceId"],"writerInstanceId",true);
        c.title=d.HasMember("title")?Text(d["title"],"title",1024):"FoxicMP";for(auto& ch:c.title)if(static_cast<unsigned char>(ch)<32)ch=' ';
        if(d.HasMember("replace"))c.replace=Flag(d["replace"],"replace");if(d.HasMember("activate"))c.activate=Flag(d["activate"],"activate");
        if(d.HasMember("startSeconds")){
            Require(d["startSeconds"].IsNumber(),"startSeconds","Expected initial position","invalid_request");c.startSeconds=d["startSeconds"].GetDouble();
            Require(std::isfinite(c.startSeconds) && c.startSeconds>=0 && c.startSeconds<3600,"startSeconds","Invalid initial position","invalid_request");
            d["startSeconds"].SetDouble(c.startSeconds==0?0.:c.startSeconds);
        }
        rapidjson::StringBuffer buffer;rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);Canonical(d,writer);c.fingerprint={buffer.GetString(),buffer.GetSize()};
    }else if(c.action=="continuousPlaybackStatus"){
        Keys(d,"continuousPlayback",{"version","action"},{"playbackId","requestId"});
        Require(d.HasMember("playbackId")!=d.HasMember("requestId"),"continuousPlayback","Specify one playback identity","invalid_request");
        c.byRequest=d.HasMember("requestId");c.id=Id(d[c.byRequest?"requestId":"playbackId"],"playback identity",true);
    }else{
        Require(c.action=="continuousPlaybackControl","continuousPlayback","Unknown command","invalid_request");
        Keys(d,"continuousPlayback",{"version","action","playbackId","command"},{"seconds","allowGrowing","waitForRange"});c.id=Id(d["playbackId"],"playbackId",true);c.command=Text(d["command"],"command",10);
        Require(c.command=="play" || c.command=="pause" || c.command=="stop" || c.command=="seek" || c.command=="cancelSeek","continuousPlayback","Unsupported control","invalid_request");
        if(c.command=="seek"){
            Require(d.HasMember("seconds") && d["seconds"].IsNumber(),"seconds","Expected seek time","invalid_request");c.seconds=d["seconds"].GetDouble();
            Require(std::isfinite(c.seconds) && c.seconds>=0 && c.seconds<3600,"seconds","Invalid seek time","invalid_request");
            if(d.HasMember("allowGrowing"))c.allowGrowing=Flag(d["allowGrowing"],"allowGrowing");
            if(d.HasMember("waitForRange")){
                Require(d["waitForRange"].IsBool(),"waitForRange","Expected a boolean","invalid_request");c.waitForRange=d["waitForRange"].GetBool();
            }
            Require(!c.waitForRange || c.allowGrowing,"waitForRange","Waiting requires growing seek opt-in","invalid_request");
        }else Require(!d.HasMember("seconds") && !d.HasMember("allowGrowing") && !d.HasMember("waitForRange"),"seconds","Unexpected seek options","invalid_request");
    }return c;
}
struct ContinuousPlayback {
    const std::string id=lease_detail::RandomId();ContinuousPlaybackCommand command;
    std::string mode="continuous-h264-video-v1";
    std::shared_ptr<StreamDecoderSource> source;
    std::function<StreamCacheStatus()> output;
    std::function<std::string()> producerError;
    int64_t duration=0;bool closed=false,committing=false;std::string error,uri;
    static constexpr unsigned MaxGrowingSeeks=16;
    unsigned growingSeeks=0;
    double pendingSeekSeconds=-1;
    uint64_t seekRevision=0;
    std::string seekState="none",seekError;
private:
    // The epoch also invalidates cancel/requeue of an identical target. A
    // transaction never clears a newer intent when decoder callbacks reenter.
    uint64_t seekEpoch=0,submittingRevision=0,submittingEpoch=0;
    double submittingSeconds=-1;
    bool queuedSubmitting=false,queuedReported=false,queuedGrowing=false,queuedReserved=false;
public:
    enum class QueuedApply { None, Waiting, Superseded, Rejected, Applied };
    bool queuedSubmissionCurrent()const{
        return queuedSubmitting && !closed && seekRevision==submittingRevision && seekEpoch==submittingEpoch && pendingSeekSeconds==submittingSeconds;
    }
    bool queuedSubmissionAllowed()const{
        if(!queuedSubmissionCurrent())return false;
        try{
            checkSeekInternal(submittingSeconds,true,false,true);
            if(producerError && !producerError().empty())return false;
            return queuedSubmissionCurrent();
        }catch(const Error&){return false;}
    }
    void committedSeek(bool growing,bool queued=false){
        if(queued && queuedSubmitting){queuedReported=true;queuedGrowing=growing;return;}
        // Completion may race admission. Never wrap the public remaining budget.
        if(growing && growingSeeks<MaxGrowingSeeks)++growingSeeks;
        clearPendingSeek("superseded");
    }
    void clearPendingSeek(const std::string& state="cancelled",const std::string& reason={}){
        if(pendingSeekSeconds<0)return;
        pendingSeekSeconds=-1;seekState=state;seekError=reason;++seekEpoch;
    }
    void queueSeek(double seconds){
        checkSeek(seconds,true,true);
        Require(seekRevision<UINT64_MAX,"continuousPlayback","Seek revision exhausted","continuous-seek-limit");
        pendingSeekSeconds=seconds;++seekRevision;++seekEpoch;seekState="waiting-range";seekError.clear();
    }
    template<class Owns,class Ready,class Submit> QueuedApply applyQueuedSeek(Owns owns,Ready ready,Submit submit){
        if(pendingSeekSeconds<0 || queuedSubmitting)return QueuedApply::None;
        submittingRevision=seekRevision;submittingEpoch=seekEpoch;submittingSeconds=pendingSeekSeconds;
        queuedSubmitting=true;queuedReported=false;
        struct Reset { bool& flag;bool& reserved;~Reset(){flag=false;reserved=false;} } reset{queuedSubmitting,queuedReserved};
        const auto current=[&]{return queuedSubmissionCurrent() && owns() && queuedSubmissionCurrent();};
        try{
            if(!current())return QueuedApply::Superseded;
            checkSeek(submittingSeconds,true);
            if(producerError){const auto error=producerError();Require(error.empty(),"continuousPlayback","Output producer failed",error.c_str());}
            if(!current())return QueuedApply::Superseded;
            if(!ready(submittingSeconds))return current()?QueuedApply::Waiting:QueuedApply::Superseded;
            if(!current())return QueuedApply::Superseded;
            checkSeek(submittingSeconds,true);
            if(producerError){const auto error=producerError();Require(error.empty(),"continuousPlayback","Output producer failed",error.c_str());}
            if(!current())return QueuedApply::Superseded;
            const bool growing=!output().complete;
            if(!current())return QueuedApply::Superseded;
            // Reserve the last growing-seek slot during a reentrant handoff.
            // The submitting command may use its own reservation; another seek
            // cannot spend it before this operation reports acceptance/rejection.
            queuedReserved=growing;
            if(!submit(submittingSeconds))return QueuedApply::Rejected;
            queuedReserved=false;
            if((queuedReported?queuedGrowing:growing) && growingSeeks<MaxGrowingSeeks)++growingSeeks;
            if(current())clearPendingSeek("applied");
            return QueuedApply::Applied;
        }catch(const Error&){if(!queuedSubmissionCurrent())return QueuedApply::Superseded;throw;}
    }
    void checkInitialPosition()const{
        const auto seconds=command.startSeconds;
        Require(std::isfinite(seconds) && seconds>=0 && seconds<3600,"startSeconds","Invalid initial position","invalid_request");
        if(!seconds)return; // Preserve the existing zero/default startup path.
        // Admission rounds upward; either backend's 100 ns rounding therefore
        // cannot place the decoder beyond the admitted presentation prefix.
        const auto target=int64_t(std::ceil(seconds*10000000.));
        Require(target<duration,"startSeconds","Initial position exceeds source duration","invalid_request");
        Require(bool(output),"continuousPlayback","Output is not ready","continuous-playback-not-ready");const auto s=output();
        Require(s.failure.empty(),"continuousPlayback","Output producer failed",s.failure.c_str());
        Require(!closed && !s.retiring && !s.removed && (!source || !source->stopped()),"continuousPlayback","Playback is closing","continuous-playback-closed");
        Require(s.complete || (s.initialized && s.fragments && target<s.indexedEnd100ns),"startSeconds","Wait for confirmed initial position","waiting-range");
    }
    bool matches(const ContinuousPlaybackCommand& c)const{return c.byRequest?command.requestId==c.id:id==c.id;}
    // ReaderRequest has already validated the receipt off the GUI thread. Bind
    // the release to this exact output, not just a reusable session/generation
    // or a lagging status snapshot. Source leases and closed cursors are inert.
    bool holdsOutputReader(const std::string& reply)const{
        rapidjson::Document d;d.Parse(reply.data(),reply.size());
        if(!source || d.HasParseError() || !d.IsObject() || !d.HasMember("ok") || !d["ok"].IsBool() || !d["ok"].GetBool() || !d.HasMember("reader"))return false;
        const auto& r=d["reader"];
        if(!r.IsObject() || !r.HasMember("released") || !r["released"].IsBool() || r["released"].GetBool()
            || !r.HasMember("filesClosed") || !r["filesClosed"].IsBool() || r["filesClosed"].GetBool()
            || !r.HasMember("sessionId") || !r.HasMember("generation") || !r.HasMember("resources") || !r["resources"].IsArray() || r["resources"].Size()!=1)return false;
        try{
            const auto bound=source->readerResource();const auto& resource=r["resources"][0];
            return Text(r["sessionId"],"sessionId",32)==command.sessionId && Unsigned(r["generation"],"generation")==command.generation
                && resource.IsObject() && resource.HasMember("id") && resource.HasMember("device") && resource.HasMember("fileId")
                && Text(resource["id"],"id",64)==bound.id && Unsigned(resource["device"],"device")==bound.device && Unsigned(resource["fileId"],"fileId")==bound.fileId;
        }catch(const Error&){return false;}
    }
    void checkSeek(double seconds,bool allowGrowing=false,bool allowUnconfirmed=false)const{
        checkSeekInternal(seconds,allowGrowing,allowUnconfirmed,false);
    }
private:
    void checkSeekInternal(double seconds,bool allowGrowing,bool allowUnconfirmed,bool ownSubmission)const{
        Require(bool(output),"continuousPlayback","Output is unavailable","continuous-seek-unavailable");const auto state=output();
        Require(state.complete || allowGrowing,"continuousPlayback","Seek requires completed output","continuous-seek-unavailable");
        Require(std::isfinite(seconds) && seconds>=0 && seconds<3600,"seconds","Invalid seek time","invalid_request");
        Require(seconds<double(duration)/10000000.,"seconds","Seek exceeds source duration","invalid_request");
        Require(!closed && !state.retiring && !state.removed && (!source || !source->stopped()),"continuousPlayback","Playback is closing","continuous-playback-closed");
        Require(state.failure.empty(),"continuousPlayback","Output producer failed",state.failure.c_str());
        if(!state.complete){
            Require(growingSeeks<MaxGrowingSeeks-unsigned(queuedReserved && !ownSubmission),"continuousPlayback","Growing seek budget exhausted; wait for complete output","continuous-seek-limit");
            Require(allowUnconfirmed || int64_t(std::ceil(seconds*10000000.))<state.indexedEnd100ns,"seconds","Wait for confirmed seek range","waiting-range");
        }
    }
};
}}
