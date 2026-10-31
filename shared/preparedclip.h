// Immutable, bounded, process-local MP4 clip. No paths, downloader handles or URLs.
#pragma once
#include "progressivereaderlease.h"
#include <cstring>
#include <atomic>
namespace foxicmp { namespace progressive {
inline const char* PreparedPreviewCapabilities(){return R"({"version":1,"mode":"prepared-remux-clip","playbackSupported":true,"boundedClip":true,"continuousPlayback":false,"sourceEofDistinct":true,"requiresPreparedProbe":true,"maxBytes":16777216,"filesystemFallback":false,"networkFallback":false,"mediaBytesOverIpc":false})";}
struct PreparedClip {
    std::shared_ptr<const std::vector<char>> bytes;
    const std::string id=lease_detail::RandomId();
    std::atomic<bool> cancelled{false};
    std::atomic<uint64_t> bytesRead{0};
    std::atomic<unsigned> readers{0};
    explicit PreparedClip(std::shared_ptr<const std::vector<char>> input):bytes(std::move(input)) {
        Require(bytes && !bytes->empty() && bytes->size()<=16777216,"preparedClip","Invalid bounded clip","invalid_prepared_clip");
    }
    std::string uri()const{return "foxicmp-remux://"+id;}
    int64_t read(uint64_t offset,void* buffer,uint64_t count){
        if(cancelled.load() || offset>bytes->size() || (!buffer && count))return -1;
        const auto n=(std::min)({count,uint64_t(bytes->size())-offset,uint64_t(1048576)});
        if(n)std::memcpy(buffer,bytes->data()+offset,size_t(n));
        bytesRead.fetch_add(n);return int64_t(n);
    }
};
class PreparedClips {
    inline static std::mutex mutex;
    inline static std::map<std::string,std::weak_ptr<PreparedClip>> entries;
public:
    static std::shared_ptr<PreparedClip> create(std::shared_ptr<const std::vector<char>> bytes){
        std::lock_guard<std::mutex> lock(mutex);
        for(auto it=entries.begin();it!=entries.end();)if(it->second.expired())it=entries.erase(it);else ++it;
        Require(entries.size()<16,"preparedClip","Prepared clip readers are still closing","prepared_clip_limit");
        auto source=std::make_shared<PreparedClip>(std::move(bytes));entries[source->id]=source;return source;
    }
    static std::shared_ptr<PreparedClip> find(const std::string& uri){
        const std::string prefix="foxicmp-remux://";if(uri.compare(0,prefix.size(),prefix)!=0)return nullptr;
        auto id=uri.substr(prefix.size());if(!lease_detail::HexId(id))return nullptr;
        std::lock_guard<std::mutex> lock(mutex);auto it=entries.find(id);if(it==entries.end())return nullptr;
        auto source=it->second.lock();return source && !source->cancelled.load()?source:nullptr;
    }
};
}}
namespace foxicmp { namespace progressive {
struct PreviewCommand{std::string action,id,title,command,requestId,fingerprint;bool replace=false,activate=false,byRequest=false;double seconds=0;};
inline PreviewCommand ParsePreviewCommand(const std::string& text){
 rapidjson::Document d;d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(text.data(),text.size());Require(!d.HasParseError() && d.IsObject(),"preview","Invalid preview command","invalid_request");harvester::UniqueKeys(d);
 Require(d.HasMember("version") && d["version"].IsUint() && d["version"].GetUint()==1 && d.HasMember("action"),"preview","Expected API version 1","invalid_request");PreviewCommand out;out.action=Text(d["action"],"action",32);
 if(out.action=="previewRemux"){Keys(d,"preview",{"version","action","probeId","requestId"},{"replace","activate","title"});out.requestId=Id(d["requestId"],"requestId",true);rapidjson::StringBuffer buffer;rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);Canonical(d,writer);out.fingerprint=buffer.GetString();out.id=Id(d["probeId"],"probeId",true);if(d.HasMember("replace"))out.replace=Flag(d["replace"],"replace");if(d.HasMember("activate"))out.activate=Flag(d["activate"],"activate");out.title=d.HasMember("title")?Text(d["title"],"title",1024):"FoxicMP — fragment";for(auto& c:out.title)if(static_cast<unsigned char>(c)<32)c=' ';}
 else if(out.action=="previewStatus"){Keys(d,"preview",{"version","action"},{"previewId","requestId"});Require(d.HasMember("previewId")!=d.HasMember("requestId"),"preview","Specify exactly one preview identity","invalid_request");out.byRequest=d.HasMember("requestId");out.id=Id(d[out.byRequest?"requestId":"previewId"],"preview identity",true);}
 else {Require(out.action=="previewStatus" || out.action=="previewCancel" || out.action=="previewControl","action","Unknown preview action","invalid_request");Keys(d,"preview",out.action=="previewControl"?std::initializer_list<const char*>{"version","action","previewId","command"}:std::initializer_list<const char*>{"version","action","previewId"},out.action=="previewControl"?std::initializer_list<const char*>{"seconds"}:std::initializer_list<const char*>{});out.id=Id(d["previewId"],"previewId",true);
  if(out.action=="previewControl"){out.command=Text(d["command"],"command",8);Require(out.command=="pause" || out.command=="play" || out.command=="seek","command","Unknown preview control","invalid_request");if(out.command=="seek"){Require(d.HasMember("seconds") && d["seconds"].IsNumber(),"seconds","Expected bounded seek time","invalid_request");out.seconds=d["seconds"].GetDouble();Require(out.seconds>=0 && out.seconds<=3600,"seconds","Invalid seek time","invalid_request");}else Require(!d.HasMember("seconds"),"seconds","Unexpected seek time","invalid_request");}
 }return out;
}
}}
