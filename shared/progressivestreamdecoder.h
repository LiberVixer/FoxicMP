// Internal decoder views of the verified continuous-remux cache.
// No filesystem URL, public capability or downloader integration. GPL-3.0-or-later.
#pragma once
#include "progressivestreamcache.h"
#include <thread>

namespace foxicmp { namespace progressive {
class StreamDecoderSource {
    std::shared_ptr<StreamOutputStore> store;
    const std::string selectedMode;
    std::shared_ptr<const StreamAudioWindow> pcmWindow;
    std::atomic<bool> cancelled{false};
    mutable std::mutex decoderMutex;
    mutable std::vector<std::weak_ptr<StreamOutputStore::Reader>> decoderReaders;
public:
    explicit StreamDecoderSource(std::shared_ptr<StreamOutputStore> output,std::string mode="continuous-h264-video-v1",bool audioConsumer=false):store(std::move(output)),selectedMode(std::move(mode)) {
        Require(bool(store),"remuxDecoder","Missing output cache","invalid_request");
        const bool audio=selectedMode=="continuous-h264-aac-window-v1" || selectedMode=="continuous-hevc-aac-window-v1";
        Require(selectedMode=="continuous-h264-video-v1" || selectedMode=="continuous-h264-reordered-video-v1" || selectedMode=="continuous-hevc-reordered-video-v1" || (audio && audioConsumer),"remuxDecoder","Invalid decoder profile","invalid_request");
        Require(hevcVideo()==store->hevcVideo(),"remuxDecoder","Cache codec/profile mismatch","invalid_request");
        if(audio){
            const auto s=store->status();Require(s.initialized && s.fragments>0 && s.failure.empty() && !s.audioWindow.empty(),"remuxDecoder","Wait for validated A/V cache","waiting_initialization");
            rapidjson::Document d;d.Parse(s.audioWindow.c_str());Require(!d.HasParseError(),"remuxDecoder","Invalid cached PCM window","backend-protocol-error");
            pcmWindow=std::make_shared<const StreamAudioWindow>(StreamAudioWindow::Parse(d));
        }
    }
    const std::string& mode()const{return selectedMode;}
    bool hevcVideo()const{return selectedMode=="continuous-hevc-reordered-video-v1" || selectedMode=="continuous-hevc-aac-window-v1";}
    const StreamAudioWindow* audioWindow()const{return pcmWindow.get();}
    harvester::muxmp4::Layout avLayout()const{
        Require(bool(pcmWindow),"remuxDecoder","Not an A/V source","invalid_request");return store->avLayout();
    }
    void copyAvLayout(harvester::muxmp4::Layout& target)const{
        Require(bool(pcmWindow),"remuxDecoder","Not an A/V source","invalid_request");store->copyAvLayout(target);
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
    // Only decoder cursors opt in. Raw/independent readers keep their own lease
    // until their owner explicitly closes it, even after producer cancellation.
    std::shared_ptr<StreamOutputStore::Reader> decoderReader()const {
        std::lock_guard<std::mutex> lock(decoderMutex);
        // Native decoder opens run off the GUI thread. Producer receipt release
        // can briefly own the registry guard while libmpv opens required audio.
        // Retry only that transient admission failure, never integrity/identity
        // errors. Raw reader/API admission retains its immediate busy result.
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
        for(;;){
            Require(!stopped(),"remuxDecoder","Decoder source cancelled","cancelled");
            try{auto value=reader();decoderReaders.push_back(value);return value;}
            catch(const Error& e){
                Require(!stopped(),"remuxDecoder","Decoder source cancelled","cancelled");
                if(e.code!="reader_registry_busy" || std::chrono::steady_clock::now()>=deadline)throw;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }
    // Controller worker only: cancel() itself remains metadata-only. Closing
    // Reader serializes with in-flight I/O and persists filesClosed after close.
    // A paused libmpv callback may outlive these files; subsequent reads fail.
    void closeCancelledDecoderReaders() {
        if(!stopped())return;
        std::lock_guard<std::mutex> lock(decoderMutex);
        for(auto it=decoderReaders.begin();it!=decoderReaders.end();) {
            if(auto value=it->lock())try {value->close();} catch(...) {++it;continue;}
            it=decoderReaders.erase(it);
        }
    }
};

// Sample-index view for the Classic splitter. It uses the same bounded parser
// as HarvesterSession; no decoder ever opens the diagnostic privatePath().
// This internal view is not yet connected to the installed DirectShow graph.
class StreamFragmentDecoder {
    std::shared_ptr<StreamDecoderSource> source;
    std::shared_ptr<StreamOutputStore::Reader> reader;
    harvester::FragmentIndex index;
    std::unique_ptr<harvester::muxmp4::Layout> av;
    mutable std::mutex viewMutex;
    uint64_t visible=0;
public:
    explicit StreamFragmentDecoder(std::shared_ptr<StreamDecoderSource> value,bool decoder=false):source(std::move(value)){
        Require(bool(source),"remuxDecoder","Missing decoder source","invalid_request");reader=decoder?source->decoderReader():source->reader();
    }
    std::string id()const{return reader->id();}
    void refresh(){
        std::lock_guard<std::mutex> lock(viewMutex);
        Require(!source->stopped(),"remuxDecoder","Decoder source cancelled","cancelled");const auto s=source->status();
        Require(s.failure.empty(),"remuxDecoder","Output producer failed",s.failure.c_str());
        if(visible==s.visible)return;
        if(source->audioWindow()){
            if(!av)av=std::make_unique<harvester::muxmp4::Layout>();
            source->copyAvLayout(*av);visible=s.visible;return;
        }
        auto next=index;
        next.Update([&](uint64_t at,void* data,size_t n){
            auto* out=static_cast<char*>(data);size_t copied=0;
            while(copied<n){const auto r=reader->read(at+copied,out+copied,(std::min)(n-copied,size_t(65536)));
                Require(r.data() && r.bytes>0,"remuxDecoder","Cannot read validated fragment",r.code.c_str());copied+=r.bytes;}
            return true;
        },s.visible,0,s.complete,0,source->hevcVideo());
        index=std::move(next);visible=s.visible;
    }
    const harvester::FragmentIndex& fragments()const{return index;}
    uint64_t viewSize(unsigned kind)const{
        std::lock_guard<std::mutex> lock(viewMutex);Require(kind<(source->audioWindow()?2u:1u),"remuxDecoder","Invalid track","invalid_request");
        return av?av->tracks[kind].bytes:visible;
    }
    bool viewComplete(unsigned kind)const{
        std::lock_guard<std::mutex> lock(viewMutex);const auto s=source->status();
        Require(!source->stopped() && s.failure.empty(),"remuxDecoder","Output producer failed",s.failure.empty()?"cancelled":s.failure.c_str());
        Require(kind<(source->audioWindow()?2u:1u),"remuxDecoder","Invalid track","invalid_request");
        // The immutable sample plan proves the audio endpoint independently of
        // a longer video. It does not complete the cache or release video leases.
        return (s.complete && visible==s.visible) || (kind==1 && av && av->tracks[1].index.Samples().size()==source->audioWindow()->pcm.PacketCount());
    }
    ResourceRead readView(unsigned kind,uint64_t at,void* out,size_t count){
        std::lock_guard<std::mutex> lock(viewMutex);
        if(source->stopped())return {"cancelled","Decoder source cancelled",0};
        if(!source->audioWindow())return kind==0?reader->read(at,out,count):ResourceRead{"invalid-read","Invalid track",0};
        if(!av || kind>1 || count>65536 || !harvester::RangeReady(at,count,av->tracks[kind].bytes))return {"waiting-range","View unavailable",0};
        try{
            std::string code;
            const bool ok=av->ReadAt([&](uint64_t offset,void* bytes,size_t n){const auto r=reader->read(offset,bytes,n);if(!r.data() || r.bytes!=n){code=r.code;return false;}return true;},kind,at,out,count);
            return ok?ResourceRead{"data","Validated A/V projection",count}:ResourceRead{code.empty()?"cache-invalid-fragment":code,"Projected read failed",0};
        }catch(const Error& e){return {e.code,e.what(),0};}catch(...){return {"cache-invalid-fragment","Projected read failed",0};}
    }
    ResourceRead read(uint64_t at,void* out,size_t count){
        if(source->stopped())return {"cancelled","Decoder source cancelled",0};return reader->read(at,out,count);
    }
    void close()noexcept{try{reader->close();}catch(...){}}
    ~StreamFragmentDecoder(){close();}
};

// One cursor per actual decoder open. Interrupt never takes the read mutex.
// Waiting is not EOF. A validated store endpoint (or immutable, fully indexed
// AAC track endpoint) is required before returning 0.
class StreamDecoderCursor {
    std::shared_ptr<StreamDecoderSource> source;
    std::shared_ptr<StreamOutputStore::Reader> reader;
    std::unique_ptr<StreamFragmentDecoder> projection;
    unsigned track=0;
    mutable std::mutex mutex,errorMutex;
    uint64_t position=0;
    std::string failure;
    bool closed=false;
    std::shared_ptr<std::atomic<bool>> interrupted=std::make_shared<std::atomic<bool>>(false);
    std::atomic<bool> waiting{false};
    std::atomic<uint64_t> total{0},eofs{0};
    void fail(const std::string& code){std::lock_guard<std::mutex> lock(errorMutex);if(failure.empty())failure=code;}
public:
    explicit StreamDecoderCursor(std::shared_ptr<StreamDecoderSource> value,int kind=-1):source(std::move(value)) {
        Require(bool(source) && (kind==-1 || (kind>=0 && kind<2 && source->audioWindow())),"remuxDecoder","Invalid decoder source/track","invalid_request");
        if(kind>=0){track=unsigned(kind);projection=std::make_unique<StreamFragmentDecoder>(source,true);projection->refresh();}
        else reader=source->decoderReader();
    }
    ~StreamDecoderCursor(){close();}
    StreamDecoderCursor(const StreamDecoderCursor&)=delete;
    StreamDecoderCursor& operator=(const StreamDecoderCursor&)=delete;
    void interrupt() noexcept {interrupted->store(true);}
    std::weak_ptr<std::atomic<bool>> interruptToken()const noexcept{return interrupted;}
    bool isWaiting()const noexcept{return waiting.load();}
    uint64_t bytes()const noexcept{return total.load();}
    uint64_t eofCount()const noexcept{return eofs.load();}
    std::string id()const{return projection?projection->id():reader->id();}
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
                ResourceRead result;
                if(projection){
                    projection->refresh();const auto limit=projection->viewSize(track);
                    if(position<limit)result=projection->readView(track,position,out,size_t((std::min)({count,limit-position,uint64_t(65536)})));
                    else if(projection->viewComplete(track))result={"eof","Confirmed track endpoint",0};
                    else {waiting=true;std::this_thread::sleep_for(std::chrono::milliseconds(50));continue;}
                }else result=reader->read(position,out,size_t((std::min)(count,uint64_t(65536))),std::chrono::milliseconds(50));
#ifdef FOXICMP_PRIVATE_STREAM_IO_TEST
                stream_cache_detail::privateIoBoundary(4); // Before decoder-visible result.
#endif
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
            if(closed || interrupted->load() || source->stopped() || !error().empty() || !state.failure.empty() || offset<0)return -1;
            if(projection)projection->refresh();
            if(uint64_t(offset)>(projection?projection->viewSize(track):state.visible))return -1;
            position=uint64_t(offset);return offset;
        }catch(...){return -1;}
    }
    int64_t size()const noexcept {
        try{
            if(interrupted->load() || source->stopped())return -1;
            if(projection){projection->refresh();return projection->viewComplete(track)?int64_t(projection->viewSize(track)):-1;}
            const auto state=source->status();return state.complete && state.failure.empty()?int64_t(state.visible):-1;
        }catch(...){return -1;}
    }
    void close() noexcept {
        interrupt();std::lock_guard<std::mutex> lock(mutex);if(closed)return;
        // The lease is released by Reader only after the native file is closed.
        try{if(projection)projection->close();else reader->close();}catch(...){}closed=true;waiting=false;
    }
};
}}
