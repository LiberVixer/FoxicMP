// Native private cache fixture; files/paths are test diagnostics, not player API.
#include "../shared/progressivecachedremux.h"
#include <future>
#include <iostream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace p=foxicmp::progressive;
static std::string read(const std::shared_ptr<p::StreamOutputStore::Reader>& reader,const rapidjson::Value& d){
    const auto offset=p::Unsigned(d["offset"],"offset"),length=p::Unsigned(d["length"],"length",65536);
    const auto wait=d.HasMember("wait")?d["wait"].GetUint():0;std::vector<char> bytes(length);const auto v=reader->read(offset,bytes.data(),bytes.size(),std::chrono::milliseconds(wait));
    rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);out.AddMember("code",p::Value(v.code.c_str(),a),a);out.AddMember("bytes",uint64_t(v.bytes),a);
    const char* digits="0123456789abcdef";std::string hex;for(size_t k=0;k<v.bytes;++k){auto b=uint8_t(bytes[k]);hex+=digits[b>>4];hex+=digits[b&15];}out.AddMember("hex",p::Value(hex.c_str(),a),a);return p::lease_detail::Encode(out);
}
int main(int argc,char** argv){
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    if(argc!=3)return 2;const auto registry=std::filesystem::u8path(argv[1]),worker=std::filesystem::u8path(argv[2]);
    auto budget=std::make_shared<p::StreamCacheBudget>();auto owner=std::make_shared<p::StreamCacheOwner>(budget);std::map<std::string,std::shared_ptr<p::StreamOutputStore::Reader>> readers;
    std::map<std::string,std::shared_ptr<p::StreamOutputStore>> held;std::shared_ptr<p::StreamOutputStore> store;std::unique_ptr<p::CachedRemuxSession> session;std::future<std::string> pending;std::string line;
    while(std::getline(std::cin,line))try{
        rapidjson::Document d;d.Parse(line.c_str());p::Require(!d.HasParseError() && d.IsObject(),"test","Invalid command");const auto action=p::Text(d["action"],"action");
        if(action=="collect"){const auto removed=owner->collect();std::cout<<"{\"ok\":true,\"removed\":"<<removed<<",\"count\":"<<owner->count()<<",\"bytes\":"<<budget->bytes()<<"}"<<std::endl;continue;}
        else if(action=="ownerStore")store=owner->create(std::filesystem::u8path(p::Text(d["root"],"root",8192)),registry,p::Text(d["sessionId"],"sessionId"),1);
        else if(action=="ownerRetire"){std::cout<<"{\"ok\":true,\"removed\":"<<(owner->retire(store)?"true":"false")<<"}"<<std::endl;continue;}
        else if(action=="budgetStatus"){std::cout<<"{\"ok\":true,\"bytes\":"<<budget->bytes()<<"}"<<std::endl;continue;}
        else if(action=="budget"){p::Require(!store && !session && readers.empty() && owner->count()==0,"test","Budget still in use");budget=std::make_shared<p::StreamCacheBudget>(p::Unsigned(d["bytes"],"bytes"));owner=std::make_shared<p::StreamCacheOwner>(budget);}
        else if(action=="store")store=std::make_shared<p::StreamOutputStore>(std::filesystem::u8path(p::Text(d["root"],"root",8192)),registry,p::Text(d["sessionId"],"sessionId"),1,budget,d.HasMember("limit")?p::Unsigned(d["limit"],"limit"):64*1024*1024);
        else if(action=="holdStore")held[p::Text(d["name"],"name")]=store;
        else if(action=="selectStore")store=held.at(p::Text(d["name"],"name"));
        else if(action=="dropHeld")held.erase(p::Text(d["name"],"name"));
        else if(action=="repeatAppend"){for(unsigned i=0;i<d["count"].GetUint();++i)store->append(store->status().written,std::vector<char>{0});}
        else if(action=="append"){
            p::Require(bool(store),"test","No store");const auto hex=p::Text(d["hex"],"hex",131072,true);p::Require(!(hex.size()&1),"test","Invalid hex");std::vector<char> bytes;
            for(size_t k=0;k<hex.size();k+=2)bytes.push_back(char(std::stoul(hex.substr(k,2),nullptr,16)));store->append(p::Unsigned(d["offset"],"offset"),bytes);
        }else if(action=="producerClosed"){p::Require(bool(store),"test","No store");std::vector<std::string> originals;if(d.HasMember("originalReaders"))for(auto& id:d["originalReaders"].GetArray())originals.push_back(p::Text(id,"id"));store->producerClosed(originals,p::Text(d["error"],"error",64,true),d.HasMember("sha256")?p::Text(d["sha256"],"sha256",64):std::string());}
        else if(action=="start"){
            session=std::make_unique<p::CachedRemuxSession>(std::filesystem::u8path(p::Text(d["directory"],"directory",8192)),registry,std::filesystem::u8path(p::Text(d["root"],"root",8192)),p::lease_detail::Encode(d["session"]),"clip",worker,owner,d.HasMember("limit")?p::Unsigned(d["limit"],"limit"):64*1024*1024);
        }else if(action=="publish")session->publish(p::lease_detail::Encode(d["session"]));
        else if(action=="heartbeat")session->heartbeat();
        else if(action=="cancel")session->cancel();
        else if(action=="reader"){
            const auto name=p::Text(d["name"],"name");p::Require(!readers.count(name),"test","Reader exists");auto reader=session?session->reader():store->reader();const auto id=reader->id();readers.emplace(name,std::move(reader));std::cout<<"{\"ok\":true,\"readerId\":\""<<id<<"\"}"<<std::endl;continue;
        }else if(action=="close"){readers.at(p::Text(d["name"],"name"))->close();}
        else if(action=="dropReader")readers.erase(p::Text(d["name"],"name"));
        else if(action=="read" || action=="beginRead"){
            auto reader=readers.at(p::Text(d["name"],"name"));if(action=="read"){std::cout<<read(reader,d)<<std::endl;continue;}
            p::Require(!pending.valid(),"test","Read already pending");auto command=p::lease_detail::Encode(d);pending=std::async(std::launch::async,[reader,command]{rapidjson::Document input;input.Parse(command.c_str());return read(reader,input);});
        }else if(action=="readResult"){
            if(pending.valid() && pending.wait_for(std::chrono::milliseconds(0))==std::future_status::ready)std::cout<<pending.get()<<std::endl;else std::cout<<"{\"ok\":true,\"pending\":true}"<<std::endl;continue;
        }else if(action=="status"){
            const auto s=session?session->status().output:store->status();rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();
            out.AddMember("ok",true,a);out.AddMember("written",s.written,a);out.AddMember("visible",s.visible,a);out.AddMember("fragments",s.fragments,a);out.AddMember("samples",s.samples,a);out.AddMember("activeReaders",uint64_t(s.activeReaders),a);out.AddMember("budgetBytes",budget->bytes(),a);
            out.AddMember("initialized",s.initialized,a);out.AddMember("complete",s.complete,a);out.AddMember("producerClosed",s.producerClosed,a);out.AddMember("originalsClosed",s.originalsClosed,a);out.AddMember("retiring",s.retiring,a);out.AddMember("removed",s.removed,a);out.AddMember("failure",p::Value(s.failure.c_str(),a),a);out.AddMember("cleanupCode",p::Value(s.cleanupCode.c_str(),a),a);
            auto path=session?session->privatePath():store->privatePath();out.AddMember("path",p::Value(path.u8string().c_str(),a),a);
            if(session){auto v=session->status();out.AddMember("done",v.done,a);out.AddMember("sourceCode",p::Value(v.source.code.c_str(),a),a);out.AddMember("sourceResult",p::Value(v.source.result.c_str(),a),a);p::Value ids(rapidjson::kArrayType);for(auto& id:v.source.readerIds)ids.PushBack(p::Value(id.c_str(),a),a);out.AddMember("sourceReaderIds",ids,a);out.AddMember("workerId",v.source.workerId,a);}
            std::cout<<p::lease_detail::Encode(out)<<std::endl;continue;
        }else if(action=="retire"){const auto removed=session?session->retire():store->retire();std::cout<<"{\"ok\":true,\"removed\":"<<(removed?"true":"false")<<"}"<<std::endl;continue;}
        else if(action=="drop"){session.reset();store.reset();}
        else if(action=="readerStatus" || action=="readerForget" || action=="readers"){std::cout<<p::ReaderRequest(registry,line)<<std::endl;continue;}
        else p::Require(false,"test","Unknown command");
        std::cout<<"{\"ok\":true}"<<std::endl;
    }catch(const p::Error& e){rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);out.AddMember("code",p::Value(e.code.c_str(),a),a);out.AddMember("message",p::Value(e.what(),a),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;}
    catch(const std::exception& e){std::cerr<<e.what()<<std::endl;std::cout<<"{\"ok\":false,\"code\":\"test-error\"}"<<std::endl;}
    session.reset();if(store){store->producerClosed({},"cancelled");store->retire();}for(auto& reader:readers)reader.second->close();readers.clear();store.reset();if(pending.valid())pending.wait();return 0;
}
