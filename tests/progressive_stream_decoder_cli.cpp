// Private native libmpv + Classic fragment-view fixture. Not player IPC.
#include "../shared/progressivecachedremux.h"
#include "../qt/src/continuousstreams.h"
#include <future>
#include <iostream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace p=foxicmp::progressive;
struct Engine {
    mpv_handle* handle=nullptr;std::unique_ptr<ContinuousStreams> streams;
    bool loaded=false,ended=false;int reason=-1,error=0;std::vector<std::string> logs;
    void option(const char* key,const std::string& value){p::Require(mpv_set_option_string(handle,key,value.c_str())>=0,"test",key);}
    explicit Engine(const std::string& images){
        handle=mpv_create();p::Require(handle!=nullptr,"test","Cannot create libmpv");
        try{
            option("config","no");option("load-scripts","no");option("ytdl","no");option("audio","no");option("hwdec","no");
            option("vo",images.empty()?"null":"image");if(!images.empty()){option("vo-image-outdir",images);option("vo-image-format","png");}
            option("untimed","yes");option("keep-open","no");option("idle","yes");option("terminal","no");
            option("demuxer-lavf-o","fflags=+ignidx");option("demuxer-lavf-analyzeduration","0.1");
            option("demuxer-readahead-secs","0");
            mpv_request_log_messages(handle,"warn");p::Require(mpv_initialize(handle)>=0,"test","Cannot initialize libmpv");
            streams=std::make_unique<ContinuousStreams>(handle);
        }catch(...){mpv_terminate_destroy(handle);handle=nullptr;throw;}
    }
    void command(const std::vector<std::string>& args){std::vector<const char*> values;for(auto& s:args)values.push_back(s.c_str());values.push_back(nullptr);p::Require(mpv_command(handle,values.data())>=0,"test","libmpv command failed");}
    void pump(){for(;;){auto* event=mpv_wait_event(handle,0);if(event->event_id==MPV_EVENT_NONE)break;
        if(event->event_id==MPV_EVENT_FILE_LOADED)loaded=true;
        if(event->event_id==MPV_EVENT_END_FILE){auto* end=static_cast<mpv_event_end_file*>(event->data);ended=true;reason=end->reason;error=end->error;}
        if(event->event_id==MPV_EVENT_LOG_MESSAGE && logs.size()<32){auto* log=static_cast<mpv_event_log_message*>(event->data);logs.push_back(std::string(log->prefix)+": "+log->text);}
    }}
    ~Engine(){if(handle)mpv_terminate_destroy(handle);streams.reset();}
};
int main(int argc,char** argv){
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    if(argc!=3)return 2;const auto registry=std::filesystem::u8path(argv[1]),worker=std::filesystem::u8path(argv[2]);
    auto budget=std::make_shared<p::StreamCacheBudget>();auto owner=std::make_shared<p::StreamCacheOwner>(budget);
    std::shared_ptr<p::StreamOutputStore> store;std::unique_ptr<p::CachedRemuxSession> session;
    std::shared_ptr<p::StreamDecoderSource> source;std::shared_ptr<p::StreamDecoderCursor> cursor;
    std::unique_ptr<p::StreamFragmentDecoder> classic;std::unique_ptr<Engine> engine;std::future<int64_t> pending;
    std::vector<char> readBuffer(65536);std::string line;
    while(std::getline(std::cin,line))try{
        rapidjson::Document d;d.Parse(line.c_str());p::Require(!d.HasParseError() && d.IsObject(),"test","Invalid command");const auto action=p::Text(d["action"],"action");
        rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);
        if(action=="store"){
            store=owner->create(std::filesystem::u8path(p::Text(d["root"],"root",8192)),registry,p::Text(d["sessionId"],"sessionId"),1);source=std::make_shared<p::StreamDecoderSource>(store);
        }else if(action=="start"){
            const auto profile=d.HasMember("reorderedVideo") && d["reorderedVideo"].GetBool()?p::StreamRemuxProfile::H264ReorderedVideo:p::StreamRemuxProfile::H264Video;
            session=std::make_unique<p::CachedRemuxSession>(std::filesystem::u8path(p::Text(d["directory"],"directory",8192)),registry,std::filesystem::u8path(p::Text(d["root"],"root",8192)),p::lease_detail::Encode(d["session"]),"clip",worker,owner,64*1024*1024,p::StreamRemuxLimits{},profile);source=session->decoderSource();
        }else if(action=="publish")session->publish(p::lease_detail::Encode(d["session"]));
        else if(action=="append"){
            const auto hex=p::Text(d["hex"],"hex",131072);std::vector<char> bytes;for(size_t k=0;k<hex.size();k+=2)bytes.push_back(char(std::stoul(hex.substr(k,2),nullptr,16)));store->append(store->status().written,bytes);
        }else if(action=="finish")store->producerClosed({},p::Text(d["error"],"error",64,true),d.HasMember("sha256")?p::Text(d["sha256"],"sha256",64):"");
        else if(action=="cursor"){cursor=std::make_shared<p::StreamDecoderCursor>(source);out.AddMember("readerId",p::Value(cursor->id().c_str(),a),a);}
        else if(action=="beginRead"){
            p::Require(!pending.valid(),"test","Read already pending");const auto n=d["length"].GetUint64();auto next=cursor;pending=std::async(std::launch::async,[&,next,n]{return next->read(readBuffer.data(),n);});
        }else if(action=="readResult"){
            if(pending.valid() && pending.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){const auto n=pending.get();out.AddMember("bytes",n,a);out.AddMember("code",p::Value(cursor->error().c_str(),a),a);if(n>0){p::Sha256 sha;sha.update(readBuffer.data(),size_t(n));out.AddMember("sha256",p::Value(sha.hex().c_str(),a),a);}}
            else out.AddMember("pending",true,a);
        }else if(action=="interrupt")cursor->interrupt();
        else if(action=="closeCursor"){cursor->close();cursor.reset();}
        else if(action=="seek")out.AddMember("offset",cursor->seek(d["offset"].GetInt64()),a);
        else if(action=="size")out.AddMember("size",cursor->size(),a);
        else if(action=="cursorStatus"){out.AddMember("waiting",cursor->isWaiting(),a);out.AddMember("bytes",cursor->bytes(),a);out.AddMember("eofs",cursor->eofCount(),a);out.AddMember("error",p::Value(cursor->error().c_str(),a),a);}
        else if(action=="fragmentOpen"){classic=std::make_unique<p::StreamFragmentDecoder>(source);out.AddMember("readerId",p::Value(classic->id().c_str(),a),a);}
        else if(action=="fragmentClose")classic.reset();
        else if(action=="fragments"){
            classic->refresh();out.AddMember("initialized",classic->fragments().Initialized(),a);p::Value samples(rapidjson::kArrayType);
            for(auto& sample:classic->fragments().Samples()){p::Value row(rapidjson::kObjectType);row.AddMember("pts",sample.start,a);row.AddMember("dts",sample.decode,a);row.AddMember("end",sample.end,a);row.AddMember("sync",sample.sync,a);
                p::Sha256 sha;std::vector<char> bytes(65536);for(uint64_t at=0;at<sample.size;){const auto n=size_t((std::min)(sample.size-at,uint64_t(bytes.size())));const auto r=classic->read(sample.offset+at,bytes.data(),n);p::Require(r.data() && r.bytes==n,"test","Cannot read sample");sha.update(bytes.data(),n);at+=n;}row.AddMember("sha256",p::Value(sha.hex().c_str(),a),a);samples.PushBack(row,a);}
            out.AddMember("samples",samples,a);
        }else if(action=="mpv"){
            engine=std::make_unique<Engine>(p::Text(d["images"],"images",8192,true));if(d.HasMember("paused") && d["paused"].GetBool())engine->command({"set","pause","yes"});const auto uri=d.HasMember("file")?p::Text(d["file"],"file",8192):engine->streams->attach(source);engine->command({"loadfile",uri});
        }else if(action=="mpvStatus"){
            engine->pump();out.AddMember("loaded",engine->loaded,a);out.AddMember("ended",engine->ended,a);out.AddMember("reason",engine->reason,a);out.AddMember("error",engine->error,a);double seconds=0;mpv_get_property(engine->handle,"time-pos",MPV_FORMAT_DOUBLE,&seconds);out.AddMember("position",seconds,a);
            char* codec=mpv_get_property_string(engine->handle,"video-codec");out.AddMember("codec",p::Value(codec?codec:"",a),a);mpv_free(codec);
            p::Value logs(rapidjson::kArrayType);for(auto& log:engine->logs)logs.PushBack(p::Value(log.c_str(),a),a);out.AddMember("logs",logs,a);
        }else if(action=="mpvCommand"){std::vector<std::string> args;for(auto& arg:d["args"].GetArray())args.push_back(p::Text(arg,"arg"));engine->command(args);}
        else if(action=="mpvDestroy"){const auto start=std::chrono::steady_clock::now();engine.reset();out.AddMember("seconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),a);}
        else if(action=="cancelSource")source->cancel();
        else if(action=="retire")out.AddMember("removed",session?session->retire():owner->retire(store),a);
        else if(action=="drop"){session.reset();store.reset();}
        else if(action=="collect"){out.AddMember("removed",uint64_t(owner->collect()),a);out.AddMember("bytes",budget->bytes(),a);}
        else if(action=="readerStatus" || action=="readerForget"){std::cout<<p::ReaderRequest(registry,line)<<std::endl;continue;}
        else if(action=="status"){
            const auto s=source->status();out.AddMember("visible",s.visible,a);out.AddMember("written",s.written,a);out.AddMember("complete",s.complete,a);out.AddMember("activeReaders",uint64_t(s.activeReaders),a);out.AddMember("readerIds",p::Value(rapidjson::kArrayType),a);for(auto& id:s.readerIds)out["readerIds"].PushBack(p::Value(id.c_str(),a),a);
            out.AddMember("failure",p::Value(s.failure.c_str(),a),a);out.AddMember("removed",s.removed,a);
            if(session){const auto state=session->status();out.AddMember("done",state.done,a);out.AddMember("workerId",state.source.workerId,a);out.AddMember("sourceCode",p::Value(state.source.code.c_str(),a),a);out.AddMember("maximumReadEnd",state.source.maximumReadEnd,a);out.AddMember("sourceFilesClosed",state.source.filesClosed,a);out.AddMember("sourceResult",p::Value(state.source.result.c_str(),a),a);p::Value ids(rapidjson::kArrayType);for(auto& id:state.source.readerIds)ids.PushBack(p::Value(id.c_str(),a),a);out.AddMember("sourceReaderIds",ids,a);}
        }else p::Require(false,"test","Unknown command");
        std::cout<<p::lease_detail::Encode(out)<<std::endl;
    }catch(const p::Error& e){rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);out.AddMember("code",p::Value(e.code.c_str(),a),a);out.AddMember("message",p::Value(e.what(),a),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;}
    engine.reset();if(cursor)cursor->interrupt();if(pending.valid())pending.wait();cursor.reset();classic.reset();if(source)source->cancel();session.reset();if(store){store->producerClosed({},"cancelled");owner->retire(store);}owner->collect();return 0;
}
