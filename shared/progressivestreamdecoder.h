// Internal decoder views of the verified continuous-remux cache.
// No filesystem URL, public capability or downloader integration. GPL-3.0-or-later.
#pragma once
#include "progressivestreamcache.h"

namespace foxicmp { namespace progressive {
class StreamDecoderSource {
    std::shared_ptr<StreamOutputStore> store;
    std::atomic<bool> cancelled{false};
public:
    explicit StreamDecoderSource(std::shared_ptr<StreamOutputStore> output):store(std::move(output)) {
        Require(bool(store),"remuxDecoder","Missing output cache","invalid_request");
    }
    void cancel() noexcept {cancelled.store(true);}
    bool stopped()const noexcept{return cancelled.load();}
    StreamCacheStatus status()const{return store->status();}
    std::string sessionId()const{return store->sessionId();}
    uint64_t generation()const{return store->generation();}
    BoundResource readerResource()const{return store->readerResource();}
    std::shared_ptr<StreamOutputStore::Reader> reader()const {
        Require(!stopped(),"remuxDecoder","Decoder source cancelled","cancelled");return store->reader();
    }
};

// One cursor per actual decoder open. Interrupt never takes the read mutex.
// Waiting is not EOF; only an explicitly completed, validated store returns 0.
class StreamDecoderCursor {
    std::shared_ptr<StreamDecoderSource> source;
    std::shared_ptr<StreamOutputStore::Reader> reader;
    mutable std::mutex mutex,errorMutex;
    uint64_t position=0;
    std::string failure;
    bool closed=false;
    std::shared_ptr<std::atomic<bool>> interrupted=std::make_shared<std::atomic<bool>>(false);
    std::atomic<bool> waiting{false};
    std::atomic<uint64_t> total{0},eofs{0};
    void fail(const std::string& code){std::lock_guard<std::mutex> lock(errorMutex);if(failure.empty())failure=code;}
public:
    explicit StreamDecoderCursor(std::shared_ptr<StreamDecoderSource> value):source(std::move(value)) {
        Require(bool(source),"remuxDecoder","Missing decoder source","invalid_request");reader=source->reader();
    }
    ~StreamDecoderCursor(){close();}
    StreamDecoderCursor(const StreamDecoderCursor&)=delete;
    StreamDecoderCursor& operator=(const StreamDecoderCursor&)=delete;
    void interrupt() noexcept {interrupted->store(true);}
    std::weak_ptr<std::atomic<bool>> interruptToken()const noexcept{return interrupted;}
    bool isWaiting()const noexcept{return waiting.load();}
    uint64_t bytes()const noexcept{return total.load();}
    uint64_t eofCount()const noexcept{return eofs.load();}
    std::string id()const{return reader->id();}
    // Status queries never wait behind a decoder blocked on unpublished bytes.
    std::string error()const{std::lock_guard<std::mutex> lock(errorMutex);return failure;}
    int64_t read(char* out,uint64_t count) noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        try {
            if(closed || interrupted->load() || source->stopped()){waiting=false;return -1;}
            if(!error().empty())return -1;
            // libmpv's zero-length probe must not become a recorded media EOF.
            if(!count)return 0;
            if(!out){fail("invalid-read");return -1;}
            for(;;){
                if(interrupted->load() || source->stopped()){waiting=false;return -1;}
                const auto result=reader->read(position,out,size_t((std::min)(count,uint64_t(65536))),std::chrono::milliseconds(50));
                if(interrupted->load() || source->stopped()){waiting=false;return -1;}
                if(result.code=="waiting-range"){waiting=true;continue;}
                waiting=false;
                if(result.data()){position+=result.bytes;total.fetch_add(result.bytes);return int64_t(result.bytes);}
                if(result.code=="eof"){eofs.fetch_add(1);return 0;}
                fail(result.code);return -1;
            }
        }catch(const Error& e){fail(e.code);}catch(...){fail("decoder-read-error");}
        waiting=false;return -1;
    }
    int64_t seek(int64_t offset) noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        try{const auto state=source->status();
            if(closed || interrupted->load() || source->stopped() || !error().empty() || !state.failure.empty() || offset<0 || uint64_t(offset)>state.visible)return -1;
            position=uint64_t(offset);return offset;
        }catch(...){return -1;}
    }
    int64_t size()const noexcept {
        try{const auto state=source->status();return !interrupted->load() && !source->stopped() && state.complete && state.failure.empty()?int64_t(state.visible):-1;}catch(...){return -1;}
    }
    void close() noexcept {
        interrupt();std::lock_guard<std::mutex> lock(mutex);if(closed)return;
        // The lease is released by Reader only after the native file is closed.
        try{reader->close();}catch(...){}closed=true;waiting=false;
    }
};

// Sample-index view for the Classic splitter. It uses the same bounded parser
// as HarvesterSession; no decoder ever opens the diagnostic privatePath().
// This internal view is not yet connected to the installed DirectShow graph.
class StreamFragmentDecoder {
    std::shared_ptr<StreamDecoderSource> source;
    std::shared_ptr<StreamOutputStore::Reader> reader;
    harvester::FragmentIndex index;
    uint64_t visible=0;
public:
    explicit StreamFragmentDecoder(std::shared_ptr<StreamDecoderSource> value):source(std::move(value)){
        Require(bool(source),"remuxDecoder","Missing decoder source","invalid_request");reader=source->reader();
    }
    std::string id()const{return reader->id();}
    void refresh(){
        Require(!source->stopped(),"remuxDecoder","Decoder source cancelled","cancelled");const auto s=source->status();
        Require(s.failure.empty(),"remuxDecoder","Output producer failed",s.failure.c_str());
        if(visible==s.visible)return;
        auto next=index;
        next.Update([&](uint64_t at,void* data,size_t n){
            auto* out=static_cast<char*>(data);size_t copied=0;
            while(copied<n){const auto r=reader->read(at+copied,out+copied,(std::min)(n-copied,size_t(65536)));
                Require(r.data() && r.bytes>0,"remuxDecoder","Cannot read validated fragment",r.code.c_str());copied+=r.bytes;}
            return true;
        },s.visible,0,s.complete);
        index=std::move(next);visible=s.visible;
    }
    const harvester::FragmentIndex& fragments()const{return index;}
    ResourceRead read(uint64_t at,void* out,size_t count){
        if(source->stopped())return {"cancelled","Decoder source cancelled",0};return reader->read(at,out,count);
    }
    ~StreamFragmentDecoder(){reader->close();}
};
}}
