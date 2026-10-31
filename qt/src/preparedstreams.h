#pragma once
#include "../../shared/preparedclip.h"
#include <mpv/stream_cb.h>
#include <mutex>
class PreparedStreams {
    using Clip=foxicmp::progressive::PreparedClip;
    std::mutex mutex;std::shared_ptr<Clip> source;
    struct Reader{std::shared_ptr<Clip> source;uint64_t position=0;explicit Reader(std::shared_ptr<Clip> s):source(std::move(s)){++source->readers;}~Reader(){--source->readers;}};
    static int open(void* opaque,char* uri,mpv_stream_cb_info* info){
        auto* self=static_cast<PreparedStreams*>(opaque);std::shared_ptr<Clip> source;
        {std::lock_guard<std::mutex> lock(self->mutex);source=self->source;}
        if(!source || source->cancelled.load() || source->uri()!=uri)return MPV_ERROR_LOADING_FAILED;
        *info={};info->cookie=new Reader{source};
        info->read_fn=[](void* cookie,char* buffer,uint64_t count)->int64_t{auto* r=static_cast<Reader*>(cookie);auto n=r->source->read(r->position,buffer,count);if(n>0)r->position+=uint64_t(n);return n;};
        info->seek_fn=[](void* cookie,int64_t offset)->int64_t{auto* r=static_cast<Reader*>(cookie);if(offset<0 || r->source->cancelled.load() || uint64_t(offset)>r->source->bytes->size())return -1;r->position=uint64_t(offset);return offset;};
        info->size_fn=[](void* cookie)->int64_t{auto* r=static_cast<Reader*>(cookie);return r->source->cancelled.load()?-1:int64_t(r->source->bytes->size());};
        info->close_fn=[](void* cookie){delete static_cast<Reader*>(cookie);};return 0;
    }
public:
    explicit PreparedStreams(mpv_handle* handle){if(mpv_stream_cb_add_ro(handle,"foxicmp-remux",this,open)<0)throw std::runtime_error("Cannot register prepared clip reader");}
    ~PreparedStreams(){clear();}
    void attach(const std::shared_ptr<Clip>& next){clear();std::lock_guard<std::mutex> lock(mutex);source=next;}
    void replacing(const std::string& uri){std::lock_guard<std::mutex> lock(mutex);if(source && source->uri()!=uri){source->cancelled=true;source.reset();}}
    void clear(){std::lock_guard<std::mutex> lock(mutex);if(source)source->cancelled=true;source.reset();}
};
