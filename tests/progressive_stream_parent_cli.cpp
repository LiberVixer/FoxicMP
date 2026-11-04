// Private native fixture for the managed parent; never an application API.
#include "../shared/progressivestreamremux.h"
#include <iostream>
#include <fstream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace p=foxicmp::progressive;
static void env(const char* value){
#ifdef _WIN32
    SetEnvironmentVariableA("FOXICMP_STREAM_PARENT_TEST_CHILD",value);
#else
    if(value)setenv("FOXICMP_STREAM_PARENT_TEST_CHILD",value,1);else unsetenv("FOXICMP_STREAM_PARENT_TEST_CHILD");
#endif
}
int main(int argc,char** argv){
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    if(const char* mode=std::getenv("FOXICMP_STREAM_PARENT_TEST_CHILD")){
        const std::string m=mode;std::string line;std::getline(std::cin,line);
        if(m=="bad-offset"){std::cout<<"{\"io\":\"write\",\"offset\":\"1\",\"hex\":\"00\"}"<<std::endl;std::getline(std::cin,line);return 1;}
        if(m=="partial-frame"){std::cout<<"{\"io\":"<<std::flush;}
        std::this_thread::sleep_for(std::chrono::seconds(30));return 0;
    }
    if(argc!=3)return 2;const auto registry=std::filesystem::u8path(argv[1]),worker=std::filesystem::u8path(argv[2]);
    std::ofstream output;std::atomic<bool> sinkFails{false};std::unique_ptr<p::StreamRemuxSession> session;std::string line;
    while(std::getline(std::cin,line))try{
        rapidjson::Document d;d.Parse(line.c_str());p::Require(!d.HasParseError() && d.IsObject() && d.HasMember("action"),"test","Invalid command");
        const auto action=p::Text(d["action"],"action");
        if(action=="start"){
            session.reset();output.close();output.clear();sinkFails=false;
            output.open(std::filesystem::u8path(p::Text(d["output"],"output",8192)),std::ios::binary|std::ios::trunc);p::Require(bool(output),"test","Cannot open private output");
            p::StreamRemuxLimits limits;
            if(d.HasMember("lifetime"))limits.lifetime=std::chrono::milliseconds(d["lifetime"].GetUint());
            if(d.HasMember("operation"))limits.operation=std::chrono::milliseconds(d["operation"].GetUint());
            if(d.HasMember("heartbeat"))limits.heartbeat=std::chrono::milliseconds(d["heartbeat"].GetUint());
            auto child=worker;if(d.HasMember("childMode")){child=std::filesystem::absolute(argv[0]);env(p::Text(d["childMode"],"childMode").c_str());}
            try{session=std::make_unique<p::StreamRemuxSession>(std::filesystem::u8path(p::Text(d["directory"],"directory",8192)),registry,p::lease_detail::Encode(d["session"]),"clip",child,[&](uint64_t at,const std::vector<char>& bytes){
                if(sinkFails.load())throw p::Error("output-store-failed","test","Injected private sink failure");
                p::Require(uint64_t(output.tellp())==at,"test","Wrong sink offset");output.write(bytes.data(),std::streamsize(bytes.size()));output.flush();
                p::Require(output.good(),"test","Cannot append private output","output-store-failed");
            },limits);}catch(...){env(nullptr);throw;}
            if(d.HasMember("childMode"))while(!session->status().workerId && !session->status().done)std::this_thread::sleep_for(std::chrono::milliseconds(5));
            env(nullptr);
        }else if(action=="status"){
            p::Require(bool(session),"test","No session");const auto s=session->status();rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();
            auto text=[&](const char* k,const std::string& v){out.AddMember(p::Value(k,a),p::Value(v.c_str(),a),a);};
            out.AddMember("ok",true,a);text("code",s.code);text("reason",s.reason);text("revision",s.appliedRevision);text("result",s.result);
            out.AddMember("done",s.done,a);out.AddMember("filesClosed",s.filesClosed,a);out.AddMember("sourceComplete",s.sourceComplete,a);
            out.AddMember("workerId",s.workerId,a);out.AddMember("bytesRead",s.bytesRead,a);out.AddMember("bytesWritten",s.bytesWritten,a);out.AddMember("maximumReadEnd",s.maximumReadEnd,a);out.AddMember("neededOffset",s.neededOffset,a);out.AddMember("neededLength",s.neededLength,a);
            p::Value ids(rapidjson::kArrayType);for(auto& id:s.readerIds)ids.PushBack(p::Value(id.c_str(),a),a);out.AddMember("readerIds",ids,a);
            std::cout<<p::lease_detail::Encode(out)<<std::endl;continue;
        }else if(action=="publish"){p::Require(bool(session),"test","No session");session->publish(p::lease_detail::Encode(d["session"]));}
        else if(action=="heartbeat"){p::Require(bool(session),"test","No session");session->heartbeat();}
        else if(action=="cancel"){if(session)session->cancel();}
        else if(action=="sinkFail")sinkFails=true;
        else if(action=="drop"){session.reset();output.close();}
        else if(action=="readerStatus" || action=="readerForget" || action=="readers"){std::cout<<p::ReaderRequest(registry,line)<<std::endl;continue;}
        else p::Require(false,"test","Unknown test command");
        std::cout<<"{\"ok\":true}"<<std::endl;
    }catch(const p::Error& e){rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);out.AddMember("code",p::Value(e.code.c_str(),a),a);out.AddMember("error",p::Value(e.what(),a),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;}
    catch(const std::exception& e){env(nullptr);std::cerr<<e.what()<<std::endl;std::cout<<"{\"ok\":false,\"code\":\"test-error\"}"<<std::endl;}
    session.reset();return 0;
}
