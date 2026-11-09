// Internal playback commands for the narrow continuous remux profile.
// No public capability/SDK claim. GPL-3.0-or-later.
#pragma once
#include "progressivestreamcontroller.h"
#include <cmath>
namespace foxicmp { namespace progressive {
inline bool ContinuousPlaybackAction(const std::string& action){return action=="continuousRemuxPlay" || action=="continuousPlaybackStatus" || action=="continuousPlaybackControl";}
struct ContinuousPlaybackCommand {
    std::string action,id,requestId,fingerprint,jobId,sessionId,writer,title,command;
    uint64_t generation=0;double seconds=0,startSeconds=0;bool replace=false,activate=false,byRequest=false;
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
        Keys(d,"continuousPlayback",{"version","action","playbackId","command"},{"seconds"});c.id=Id(d["playbackId"],"playbackId",true);c.command=Text(d["command"],"command",8);
        Require(c.command=="play" || c.command=="pause" || c.command=="stop" || c.command=="seek","continuousPlayback","Unsupported control","invalid_request");
        if(c.command=="seek"){
            Require(d.HasMember("seconds") && d["seconds"].IsNumber(),"seconds","Expected seek time","invalid_request");c.seconds=d["seconds"].GetDouble();
            Require(std::isfinite(c.seconds) && c.seconds>=0 && c.seconds<3600,"seconds","Invalid seek time","invalid_request");
        }else Require(!d.HasMember("seconds"),"seconds","Unexpected seek time","invalid_request");
    }return c;
}
struct ContinuousPlayback {
    const std::string id=lease_detail::RandomId();ContinuousPlaybackCommand command;
    std::string mode="continuous-h264-video-v1";
    std::shared_ptr<StreamDecoderSource> source;
    std::function<StreamCacheStatus()> output;
    std::function<std::string()> producerError;
    int64_t duration=0;bool closed=false,committing=false;std::string error,uri;
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
    void checkSeek(double seconds)const{
        Require(output && output().complete,"continuousPlayback","Seek requires completed output","continuous-seek-unavailable");
        Require(seconds<double(duration)/10000000.,"seconds","Seek exceeds source duration","invalid_request");
    }
};
}}
