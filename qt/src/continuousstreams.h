// Internal libmpv adapter; retained until mpv_terminate_destroy has returned.
// Not registered by the ordinary Player/API yet. GPL-3.0-or-later.
#pragma once
#include "../../shared/progressivestreamdecoder.h"
#include <mpv/stream_cb.h>

class ContinuousStreams {
    using Source=foxicmp::progressive::StreamDecoderSource;
    using Cursor=foxicmp::progressive::StreamDecoderCursor;
    std::mutex mutex;
    std::shared_ptr<Source> source;
    std::string token;
    using Cookie=std::shared_ptr<Cursor>;
    std::vector<std::weak_ptr<Cursor>> readers;
    void detachLocked(){for(auto& weak:readers)if(auto reader=weak.lock())reader->interrupt();readers.clear();source.reset();token.clear();}
    static int open(void* opaque,char* uri,mpv_stream_cb_info* info) noexcept {
        auto* self=static_cast<ContinuousStreams*>(opaque);
        try{
            std::lock_guard<std::mutex> lock(self->mutex);
            if(!self->source || self->token!=uri || self->source->stopped())return MPV_ERROR_LOADING_FAILED;
            auto& readers=self->readers;readers.erase(std::remove_if(readers.begin(),readers.end(),[](const auto& weak){return weak.expired();}),readers.end());
            if(readers.size()>=foxicmp::progressive::MaxReaders)return MPV_ERROR_LOADING_FAILED;
            auto reader=std::make_shared<Cursor>(self->source);auto cookie=std::make_unique<Cookie>(reader);readers.push_back(reader);*info={};
            info->cookie=cookie.release();
            info->read_fn=[](void* r,char* out,uint64_t n)->int64_t{return (*static_cast<Cookie*>(r))->read(out,n);};
            info->seek_fn=[](void* r,int64_t at)->int64_t{const auto v=(*static_cast<Cookie*>(r))->seek(at);return v<0?int64_t(MPV_ERROR_UNSUPPORTED):v;};
            info->size_fn=[](void* r)->int64_t{const auto v=(*static_cast<Cookie*>(r))->size();return v<0?int64_t(MPV_ERROR_UNSUPPORTED):v;};
            info->cancel_fn=[](void* r){(*static_cast<Cookie*>(r))->interrupt();};
            info->close_fn=[](void* r){delete static_cast<Cookie*>(r);};return 0;
        }catch(...){return MPV_ERROR_LOADING_FAILED;}
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
    // Detaching one decoder must not cancel independent readers of the store.
    void clear(){std::lock_guard<std::mutex> lock(mutex);detachLocked();}
    ~ContinuousStreams(){clear();}
};
