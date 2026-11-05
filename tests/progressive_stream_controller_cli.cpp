// Private command/controller fixture, no installed player/Harvester or public IPC.
#include "../shared/progressivestreamcontroller.h"
#include <iostream>
#include <future>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace p=foxicmp::progressive;
struct Gate {
    std::mutex mutex;std::condition_variable changed;bool hold=false,entered=false;
    void wait(){std::unique_lock<std::mutex> lock(mutex);entered=true;changed.notify_all();changed.wait(lock,[&]{return !hold;});}
    void release(){std::lock_guard<std::mutex> lock(mutex);hold=false;changed.notify_all();}
};
int main(int argc,char** argv){
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    if(argc!=4)return 2;const auto registry=std::filesystem::u8path(argv[1]),cache=std::filesystem::u8path(argv[2]),child=std::filesystem::u8path(argv[3]);
    auto prepare=std::make_shared<Gate>(),publish=std::make_shared<Gate>();std::unique_ptr<p::ContinuousRemuxController> manager;
    std::map<std::string,std::shared_ptr<p::StreamOutputStore::Reader>> readers;std::shared_ptr<p::lease_detail::Handle> guard;std::string line;
    std::map<std::string,std::shared_ptr<p::StreamDecoderCursor>> cursors;
    std::map<std::string,std::future<int64_t>> pendingReads;
    auto initialize=[&](const rapidjson::Value& d){p::StreamRemuxLimits limits;if(d.HasMember("heartbeat"))limits.heartbeat=std::chrono::milliseconds(d["heartbeat"].GetUint());if(d.HasMember("lifetime"))limits.lifetime=std::chrono::milliseconds(d["lifetime"].GetUint());if(limits.operation>limits.lifetime)limits.operation=limits.lifetime;manager=std::make_unique<p::ContinuousRemuxController>(registry,cache,child,limits,p::ContinuousControllerHooks{[prepare]{prepare->wait();},[publish]{publish->wait();}});};
    rapidjson::Document empty;empty.SetObject();initialize(empty);
    while(std::getline(std::cin,line))try{
        rapidjson::Document d;d.Parse(line.c_str());p::Require(!d.HasParseError() && d.IsObject() && d.HasMember("action"),"fixture","Invalid command");const auto action=p::Text(d["action"],"action");
        if(action.find("continuousRemux")==0){std::cout<<manager->request(line)<<std::endl;continue;}
        rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);
        if(action=="configure"){p::Require(p::ContinuousRetirement::occupied()==0,"fixture","Old job still alive");initialize(d);}
        else if(action=="gate" || action=="gateStatus"){
            auto gate=p::Text(d["stage"],"stage")=="prepare"?prepare:publish;std::lock_guard<std::mutex> lock(gate->mutex);
            if(action=="gate"){gate->hold=d["hold"].GetBool();gate->entered=false;gate->changed.notify_all();}out.AddMember("entered",gate->entered,a);
        }else if(action=="dropManager"){
            const auto start=std::chrono::steady_clock::now();manager.reset();out.AddMember("seconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),a);
        }else if(action=="retirement"){out.AddMember("occupied",uint64_t(p::ContinuousRetirement::occupied()),a);}
        else if(action=="settleExit"){
            const auto began=std::chrono::steady_clock::now();
            out.AddMember("settled",p::ContinuousRemuxController::settleOnExit(std::chrono::milliseconds(d["milliseconds"].GetUint())),a);
            out.AddMember("seconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count(),a);
        }
        else if(action=="reader"){
            auto source=manager->decoder(p::Text(d["jobId"],"jobId"));p::Require(bool(source),"fixture","Decoder not ready","decoder-not-ready");auto reader=source->reader();out.AddMember("readerId",p::Value(reader->id().c_str(),a),a);readers[p::Text(d["name"],"name")]=std::move(reader);
        }else if(action=="decoderCursor"){
            auto source=manager->decoder(p::Text(d["jobId"],"jobId"));p::Require(bool(source),"fixture","Decoder not ready","decoder-not-ready");
            const auto name=p::Text(d["name"],"name");p::Require(!cursors.count(name),"fixture","Cursor name already used");
            auto cursor=std::make_shared<p::StreamDecoderCursor>(source);out.AddMember("readerId",p::Value(cursor->id().c_str(),a),a);cursors[name]=std::move(cursor);
        }else if(action=="cursorRead"){
            auto cursor=cursors.at(p::Text(d["name"],"name"));char byte;out.AddMember("result",cursor->read(&byte,1),a);
        }else if(action=="cursorStatus"){
            const auto cursor=cursors.at(p::Text(d["name"],"name"));out.AddMember("readerId",p::Value(cursor->id().c_str(),a),a);
            out.AddMember("eofCount",cursor->eofCount(),a);out.AddMember("waiting",cursor->isWaiting(),a);
        }else if(action=="cursorWait"){
            const auto name=p::Text(d["name"],"name");auto cursor=cursors.at(name);p::Require(!pendingReads.count(name),"fixture","Read already pending");
            p::Require(cursor->seek(p::Unsigned(d["offset"],"offset"))>=0,"fixture","Invalid cursor seek");
            pendingReads[name]=std::async(std::launch::async,[cursor]{char byte;return cursor->read(&byte,1);});
        }else if(action=="cursorPoll"){
            auto& future=pendingReads.at(p::Text(d["name"],"name"));const bool ready=future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;
            out.AddMember("ready",ready,a);if(ready)out.AddMember("result",future.get(),a);
        }else if(action=="cursorClose")cursors.erase(p::Text(d["name"],"name"));
        else if(action=="close")readers.erase(p::Text(d["name"],"name"));
        else if(action=="read"){
            std::vector<char> bytes(65536);const auto result=readers.at(p::Text(d["name"],"name"))->read(p::Unsigned(d["offset"],"offset"),bytes.data(),bytes.size());
            out.AddMember("code",p::Value(result.code.c_str(),a),a);out.AddMember("bytes",uint64_t(result.bytes),a);if(result.data()){p::Sha256 h;h.update(bytes.data(),result.bytes);out.AddMember("sha256",p::Value(h.hex().c_str(),a),a);}
        }else if(action=="holdRegistry"){
            p::lease_detail::Directory dir(registry,true);guard=std::make_shared<p::lease_detail::Handle>(dir.file(".guard",true));p::Require(guard->lock(),"fixture","Registry busy");
        }else if(action=="releaseRegistry")guard.reset();
        else if(action=="readerStatus" || action=="readerForget"){std::cout<<p::ReaderRequest(registry,line)<<std::endl;continue;}
        else p::Require(false,"fixture","Unknown command");
        std::cout<<p::lease_detail::Encode(out)<<std::endl;
    }catch(const std::exception& e){std::cout<<"{\"ok\":false,\"code\":\"fixture-error\"}"<<std::endl;std::cerr<<e.what()<<std::endl;}
    prepare->release();publish->release();guard.reset();manager.reset();pendingReads.clear();cursors.clear();readers.clear();if(!p::ContinuousRetirement::waitEmpty(std::chrono::seconds(10)))return 3;return 0;
}
