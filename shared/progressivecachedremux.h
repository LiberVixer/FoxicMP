// Internal owner of managed remux + private output cache. No public capability.
// GPL-3.0-or-later.
#pragma once
#include "progressivestreamremux.h"
#include "progressivestreamcache.h"
#include "progressivestreamdecoder.h"
namespace foxicmp { namespace progressive {
struct CachedRemuxStatus {StreamRemuxStatus source;StreamCacheStatus output;bool done=false;};
class CachedRemuxSession {
    std::shared_ptr<StreamCacheOwner> owner;
    std::shared_ptr<StreamOutputStore> output;
    std::unique_ptr<StreamRemuxSession> producer;
    std::thread finalizer;
    const std::string mode;
public:
    CachedRemuxSession(const std::filesystem::path& directory,const std::filesystem::path& registry,
        const std::filesystem::path& cache,const std::string& json,const std::string& resource,
        const std::filesystem::path& child,std::shared_ptr<StreamCacheOwner> cacheOwner,
        uint64_t outputLimit=64*1024*1024,StreamRemuxLimits limits={},StreamRemuxProfile profile=StreamRemuxProfile::H264Video):owner(std::move(cacheOwner)),mode(StreamRemuxMode(profile)) {
        Require(bool(owner),"remuxCache","Cache service owner required","invalid_request");
        const auto model=Parse(json);
        output=owner->create(cache,registry,model.id,model.generation,outputLimit,profile==StreamRemuxProfile::HevcReorderedVideo);
        try{
            producer=std::make_unique<StreamRemuxSession>(directory,registry,json,resource,child,[store=output](uint64_t at,const std::vector<char>& bytes){store->append(at,bytes);},limits,profile,[store=output](const StreamAudioWindow& plan){store->acceptAudioWindow(plan);});
            finalizer=std::thread([this]{
                StreamRemuxStatus state;
                try{
                    do{if(!output->status().failure.empty())producer->cancel();state=producer->status();if(!state.done)std::this_thread::sleep_for(std::chrono::milliseconds(10));}while(!state.done);
                    std::string hash;
                    if(state.code=="data"){rapidjson::Document result;result.Parse(state.result.c_str());Require(!result.HasParseError() && result.IsObject() && result.HasMember("sha256"),"remuxCache","Missing producer digest","cache-finalization-failed");hash=Text(result["sha256"],"sha256",64);}
                    Require(state.filesClosed,"remuxCache","Original source still open","cache-finalization-failed");
                    output->producerClosed(state.readerIds,state.code=="data"?std::string():state.code,hash);
                }catch(...){output->producerClosed(state.readerIds,"cache-finalization-failed");}
            });
        }catch(...){if(producer){producer->cancel();producer.reset();}output->producerClosed({},"cancelled");owner->retire(output);throw;}
    }
    ~CachedRemuxSession(){producer->cancel();if(finalizer.joinable())finalizer.join();producer.reset();owner->retire(output);}
    CachedRemuxSession(const CachedRemuxSession&)=delete;CachedRemuxSession& operator=(const CachedRemuxSession&)=delete;
    void publish(const std::string& json){producer->publish(json);}
    void heartbeat(){producer->heartbeat();}
    bool heartbeatWhileRunning(){return producer->heartbeatWhileRunning();}
    void cancel(){producer->cancel();}
    void retryClosedReaders(){output->retryClosedReaders();}
    std::shared_ptr<StreamOutputStore::Reader> reader(){return output->reader();}
    std::vector<harvester::Sample> avSamples(unsigned kind)const{return output->avSamples(kind);}
    // Audio callers must bind the explicit PCM-window adapter before decoding.
    // Existing callers retain video-only admission, including the public API.
    std::shared_ptr<StreamDecoderSource> decoderSource(bool audioConsumer=false){return std::make_shared<StreamDecoderSource>(output,mode,audioConsumer);}
    CachedRemuxStatus status()const{CachedRemuxStatus s;s.source=producer->status();s.output=output->status();s.done=s.source.done && s.output.producerClosed;return s;}
    bool retire(){producer->cancel();return owner->retire(output);}
    std::filesystem::path privatePath()const{return output->privatePath();}
};
}}
