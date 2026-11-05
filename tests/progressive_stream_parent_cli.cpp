// Private native fixture for the managed parent; never an application API.
#include "../shared/progressivestreamremux.h"
#include <iostream>
#include <fstream>
#include <cstdio>
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
        // Do not let stdio prefetch the deliberately unread reply remainder.
        std::setvbuf(stdin,nullptr,_IONBF,0);
        const std::string spec=mode;const auto split=spec.find('|');const auto m=spec.substr(0,split);std::string line;std::getline(std::cin,line);
        if(m=="pipe-send" || m=="pipe-receive"){
            // A private child/marker handshake proves pipe progress before the
            // external test injects death/cancel. No production worker hooks.
            if(split==std::string::npos)return 2;const auto hex=spec.substr(split+1);std::string name;
            auto digit=[](char c){return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:-1;};
            if(hex.size()%2)return 2;for(size_t i=0;i<hex.size();i+=2){const auto a=digit(hex[i]),b=digit(hex[i+1]);if(a<0 || b<0)return 2;name+=char(a*16+b);}
            unsigned capacity=0,transferred=0;std::string prefixSha;
#ifdef _WIN32
            DWORD outSize=0;if(!GetNamedPipeInfo(GetStdHandle(m=="pipe-send"?STD_INPUT_HANDLE:STD_OUTPUT_HANDLE),nullptr,&outSize,nullptr,nullptr))return 3;capacity=outSize;
            const auto pid=GetCurrentProcessId();
#else
            const auto size=fcntl(m=="pipe-send"?0:1,F_GETPIPE_SZ);if(size<=0)return 3;capacity=unsigned(size);const auto pid=getpid();
#endif
            if(m=="pipe-receive"){
                std::cout<<"{\"io\":\"read\",\"offset\":\"0\",\"length\":4096}"<<std::endl;
                if(!std::getline(std::cin,line))return 4;p::Sha256 digest;digest.update(line.data(),line.size());prefixSha=digest.hex();
                // Larger than either transport buffer, still below frame limit;
                // completing this write requires the parent to consume a prefix.
                const auto partial=std::string("{\"io\":\"write\",\"offset\":\"0\",\"hex\":\"")+std::string(131072,'0');
                if(capacity>=partial.size())return 3;std::cout<<partial<<std::flush;if(!std::cout)return 4;transferred=unsigned(partial.size());
            }else{
                std::cout<<"{\"io\":\"read\",\"offset\":\"0\",\"length\":65536}"<<std::endl;
                std::string prefix(4096,'\0');size_t at=0;
                while(at<prefix.size()){
#ifdef _WIN32
                    DWORD count=0;if(!ReadFile(GetStdHandle(STD_INPUT_HANDLE),prefix.data()+at,DWORD(prefix.size()-at),&count,nullptr) || !count)return 4;at+=count;
#else
                    const auto count=::read(0,prefix.data()+at,prefix.size()-at);if(count<0 && errno==EINTR)continue;if(count<=0)return 4;at+=size_t(count);
#endif
                }
                // Parent reply is >131072 bytes. Keep its remainder unread,
                // exceeding the reported pipe capacity after this prefix.
                if(capacity+prefix.size()>=131072)return 3;
                p::Sha256 digest;digest.update(prefix.data(),prefix.size());prefixSha=digest.hex();transferred=unsigned(prefix.size());
            }
            const auto marker=std::filesystem::u8path(name),temporary=std::filesystem::u8path(name+".tmp");
            {std::ofstream file(temporary,std::ios::binary);file<<"{\"mode\":\""<<m<<"\",\"pid\":"<<pid<<",\"transferredBytes\":"<<transferred<<",\"channelCapacityBytes\":"<<capacity<<",\"prefixSha256\":\""<<prefixSha<<"\"}";file.close();if(!file)return 5;}
            std::filesystem::rename(temporary,marker);
        }
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
            auto child=worker;if(d.HasMember("childMode")){child=std::filesystem::absolute(argv[0]);auto mode=p::Text(d["childMode"],"childMode");if(d.HasMember("childMarker")){mode+='|';const auto name=p::Text(d["childMarker"],"childMarker",8192);const char* digits="0123456789abcdef";for(unsigned char c:name){mode+=digits[c>>4];mode+=digits[c&15];}}env(mode.c_str());}
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
