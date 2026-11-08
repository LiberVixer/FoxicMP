// Internal libmpv adapter; retained until mpv_terminate_destroy has returned.
// Registered by Player; public API/controller integration is separate. GPL-3.0-or-later.
#pragma once
#include "../../shared/progressivestreamdecoder.h"
#include <mpv/stream_cb.h>
#include <cstdio>

class ContinuousStreams {
    using Source=foxicmp::progressive::StreamDecoderSource;
    using Cursor=foxicmp::progressive::StreamDecoderCursor;
    std::mutex mutex;
    std::shared_ptr<Source> source;
    std::string token;
    size_t opening=0;
    int64_t entry=-1;
    std::shared_ptr<Source> entrySource;
    bool entryInterrupted=false;
    using Cookie=std::shared_ptr<Cursor>;
    std::vector<std::weak_ptr<std::atomic<bool>>> readers;
    void detachLocked(){for(auto& weak:readers)if(auto flag=weak.lock())flag->store(true);readers.clear();source.reset();token.clear();entryInterrupted=true;}
    bool matchesLocked(const char* uri)const{return source && (token==uri || (source->audioWindow() && token+"/audio"==uri));}
    static int open(void* opaque,char* uri,mpv_stream_cb_info* info) noexcept {
        auto* self=static_cast<ContinuousStreams*>(opaque);
        bool reserved=false;
        try{
            std::shared_ptr<Source> held;int kind=-1;
            {
                std::lock_guard<std::mutex> lock(self->mutex);
                if(!self->matchesLocked(uri) || self->source->stopped())return MPV_ERROR_LOADING_FAILED;
                auto& readers=self->readers;readers.erase(std::remove_if(readers.begin(),readers.end(),[](const auto& weak){return weak.expired();}),readers.end());
                if(readers.size()+self->opening>=foxicmp::progressive::MaxReaders)return MPV_ERROR_LOADING_FAILED;
                held=self->source;if(held->audioWindow())kind=self->token==uri?0:1;++self->opening;reserved=true;
            }
            // Registration/pinning may perform I/O. Stop/replacement only need
            // the adapter mutex and must never wait behind this constructor.
            auto reader=std::make_shared<Cursor>(held,kind);auto cookie=std::make_unique<Cookie>(reader);
            {
                std::lock_guard<std::mutex> lock(self->mutex);--self->opening;reserved=false;
                if(self->source!=held || !self->matchesLocked(uri) || held->stopped())return MPV_ERROR_LOADING_FAILED;
                self->readers.push_back(reader->interruptToken());
            }
            *info={};
            info->cookie=cookie.release();
            info->read_fn=[](void* r,char* out,uint64_t n)->int64_t{return (*static_cast<Cookie*>(r))->read(out,n);};
            info->seek_fn=[](void* r,int64_t at)->int64_t{const auto v=(*static_cast<Cookie*>(r))->seek(at);return v<0?int64_t(MPV_ERROR_UNSUPPORTED):v;};
            info->size_fn=[](void* r)->int64_t{const auto v=(*static_cast<Cookie*>(r))->size();return v<0?int64_t(MPV_ERROR_UNSUPPORTED):v;};
            info->cancel_fn=[](void* r){(*static_cast<Cookie*>(r))->interrupt();};
            info->close_fn=[](void* r){delete static_cast<Cookie*>(r);};return 0;
        }catch(const foxicmp::progressive::Error& e){
            // A mandatory AAC projection must not silently lose its native
            // opening error behind libmpv's generic external-file failure.
            std::fprintf(stderr,"Continuous decoder open failed: %s\n",e.code.c_str());
            if(reserved){std::lock_guard<std::mutex> lock(self->mutex);--self->opening;}return MPV_ERROR_LOADING_FAILED;
        }catch(...){if(reserved){std::lock_guard<std::mutex> lock(self->mutex);--self->opening;}return MPV_ERROR_LOADING_FAILED;}
    }
public:
    explicit ContinuousStreams(mpv_handle* handle){
        if(mpv_stream_cb_add_ro(handle,"foxicmp-continuous",this,open)<0)throw std::runtime_error("Cannot register continuous cache reader");
    }
    // A new random token per attachment prevents stale/replaced playback opens.
    std::string attach(std::shared_ptr<Source> next){
        std::lock_guard<std::mutex> lock(mutex);detachLocked();source=std::move(next);
        token="foxicmp-continuous://"+foxicmp::progressive::lease_detail::RandomId()+"/video";return token;
    }
    std::string audioUri(){std::lock_guard<std::mutex> lock(mutex);return source && source->audioWindow()?token+"/audio":std::string();}
    // Detaching one decoder must not cancel independent readers of the store.
    void clear(){std::lock_guard<std::mutex> lock(mutex);detachLocked();}
    void replacing(const std::string& uri){std::lock_guard<std::mutex> lock(mutex);if(token!=uri)detachLocked();}
    std::shared_ptr<Source> current(){std::lock_guard<std::mutex> lock(mutex);return source;}
    std::string uri(){std::lock_guard<std::mutex> lock(mutex);return token;}
    void entryStarted(int64_t id,const std::string& uri){std::lock_guard<std::mutex> lock(mutex);if(source && uri==token){entry=id;entrySource=source;entryInterrupted=false;}}
    struct End {std::shared_ptr<Source> source;bool interrupted=false;};
    End entryEnded(int64_t id){std::lock_guard<std::mutex> lock(mutex);if(id!=entry)return {};entry=-1;return {std::exchange(entrySource,{}),entryInterrupted};}
    ~ContinuousStreams(){clear();}
};
