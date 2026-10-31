// Native confirmed-range I/O fixture. Test-only hex output, never a player API.
#include "../shared/progressiveresourceprobe.h"
#include <future>
#include <iostream>
#ifdef _WIN32
#include <winioctl.h>
#endif
namespace p=foxicmp::progressive;
std::string read(std::shared_ptr<p::ResourceSource> source,const rapidjson::Value& d) {
    const auto id=p::Id(d["resourceId"],"resourceId");const auto offset=p::Unsigned(d["offset"],"offset",p::MaxByte);
    const auto count=size_t(p::Unsigned(d["length"],"length",65536));const auto wait=d.HasMember("waitMilliseconds")?d["waitMilliseconds"].GetUint():0;
    std::vector<char> bytes(count);auto result=source->read(id,offset,bytes.data(),count,std::chrono::milliseconds(wait));
    rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);out.AddMember("code",p::Value(result.code.c_str(),a),a);out.AddMember("bytesRead",uint32_t(result.bytes),a);
    std::string hex;const char* digits="0123456789abcdef";for(size_t i=0;i<result.bytes;++i){const auto value=static_cast<unsigned char>(bytes[i]);hex+=digits[value>>4];hex+=digits[value&15];}
    out.AddMember("hex",p::Value(hex.c_str(),a),a);return p::lease_detail::Encode(out);
}
int main(int argc,char** argv) {
    if(argc!=3 || std::string(argv[1])!="--registry")return 2;
    const auto registry=std::filesystem::u8path(argv[2]);p::ResourceProbeManager probes;
    std::shared_ptr<p::ResourceSource> source,retained;std::shared_ptr<std::atomic<bool>> receipt;
    std::future<std::string> pending;
    std::string line;
    while(std::getline(std::cin,line)) {
        try {
            rapidjson::Document d;d.Parse(line.c_str());p::Require(!d.HasParseError() && d.IsObject(),"$","Invalid command");
            const auto action=p::Text(d["action"],"action");
            if(action=="dumpRemux") {
                auto bytes=probes.stagedRemux(p::Text(d["probeId"],"probeId"));
                if(bytes){std::ofstream file(std::filesystem::u8path(p::Text(d["path"],"path",8192)),std::ios::binary);file.write(bytes->data(),std::streamsize(bytes->size()));file.close();p::Require(file.good(),"test","Cannot dump stage");}
                std::cout<<"{\"ok\":true,\"retained\":"<<(bytes?"true":"false")<<"}"<<std::endl;continue;
            }
            if(action=="handles") {
                unsigned count=0;
#ifdef _WIN32
                DWORD handles=0;p::Require(GetProcessHandleCount(GetCurrentProcess(),&handles)!=FALSE,"test","Cannot inspect handles");count=handles;
#else
                DIR* directory=opendir("/proc/self/fd");p::Require(directory!=nullptr,"test","Cannot inspect handles");
                while(auto entry=readdir(directory))if(entry->d_name[0]!='.')++count;closedir(directory);
#endif
                std::cout<<"{\"ok\":true,\"handles\":"<<count<<"}"<<std::endl;continue;
            }
            if(action=="sha256") {
                p::Require(d.HasMember("text") && d["text"].IsString(),"text","Expected hash test data");
                const auto count=d["text"].GetStringLength();const auto step=d.HasMember("step")?d["step"].GetUint():65536;
                p::Require(step>0,"step","Invalid SHA test step");p::Sha256 hash;
                for(size_t i=0;i<count;i+=step)hash.update(d["text"].GetString()+i,(std::min)(size_t(step),size_t(count)-i));
                std::cout<<"{\"ok\":true,\"sha256\":\""<<hash.hex()<<"\"}"<<std::endl;continue;
            }
            if(action=="sparseFixture") {
                const auto path=std::filesystem::u8path(p::Text(d["path"],"path",8192));const auto at=p::Unsigned(d["offset"],"offset",p::MaxByte-1);
#ifdef _WIN32
                p::lease_detail::Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,CREATE_NEW,0,nullptr));DWORD count=0;LARGE_INTEGER position{};position.QuadPart=int64_t(at);
                p::Require(file.value!=INVALID_HANDLE_VALUE && DeviceIoControl(file.value,FSCTL_SET_SPARSE,nullptr,0,nullptr,0,&count,nullptr) && SetFilePointerEx(file.value,position,nullptr,FILE_BEGIN) && WriteFile(file.value,"Z",1,&count,nullptr) && count==1 && FlushFileBuffers(file.value),"fixture","Cannot create sparse fixture");
#else
                p::lease_detail::Handle file(::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600));p::Require(file.value>=0 && pwrite(file.value,"Z",1,off_t(at))==1 && !fsync(file.value),"fixture","Cannot create sparse fixture");
#endif
                std::cout<<"{\"ok\":true}"<<std::endl;continue;
            }
            if(action.rfind("resourceProbe",0)==0){std::cout<<probes.request(registry,line)<<std::endl;continue;}
            if(action=="readerStatus" || action=="readerForget" || action=="readers"){std::cout<<p::ReaderRequest(registry,line)<<std::endl;continue;}
            if(action=="open" || action=="openCatalog")source=std::make_shared<p::ResourceSource>(std::filesystem::u8path(p::Text(d["directory"],"directory",8192)),registry,p::lease_detail::Encode(d["session"]),d.HasMember("isolateOptionalSrt") && d["isolateOptionalSrt"].GetBool()?p::ResourceFailurePolicy::IsolateOptionalSrt:p::ResourceFailurePolicy::Strict,action=="openCatalog"?p::ResourceMode::BoundedCatalog:p::ResourceMode::Inline);
            else if(action=="read") {p::Require(bool(source),"source","No source");std::cout<<read(source,d)<<std::endl;continue;}
            else if(action=="beginRead") {
                p::Require(bool(source) && !pending.valid(),"source","Pending read already exists");const auto raw=p::lease_detail::Encode(d);
                pending=std::async(std::launch::async,[owned=source,raw]{rapidjson::Document command;command.Parse(raw.c_str());return read(owned,command);});
            }else if(action=="result") {
                p::Require(pending.valid(),"read","No pending read");
                if(pending.wait_for(std::chrono::milliseconds(0))==std::future_status::ready)std::cout<<pending.get()<<std::endl;
                else std::cout<<"{\"ok\":true,\"pending\":true}"<<std::endl;
                continue;
            }else if(action=="publish") {p::Require(bool(source),"source","No source");source->publish(p::lease_detail::Encode(d["session"]));}
            else if(action=="retain")retained=source;
            else if(action=="dropRetained")retained.reset();
            else if(action=="cancel") {if(source)source->cancel();}
            else if(action=="release") {if(source){receipt=source->releaseReceipt();source->cancel();source.reset();}}
            else if(action=="released") {std::cout<<"{\"ok\":true,\"released\":"<<(receipt && receipt->load()?"true":"false")<<"}"<<std::endl;continue;}
            else if(action=="bindings") {
                p::Require(bool(source),"source","No source");rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();p::Value ids(rapidjson::kArrayType);
                for(const auto& id:source->readerIds())ids.PushBack(p::Value(id.c_str(),a),a);out.AddMember("readerIds",ids,a);out.AddMember("ok",true,a);std::cout<<p::lease_detail::Encode(out)<<std::endl;continue;
            }else p::Require(false,"action","Unknown command");
            std::cout<<"{\"ok\":true}"<<std::endl;
        }catch(const p::Error& error){rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);out.AddMember("code",p::Value(error.code.c_str(),a),a);out.AddMember("error",p::Value(error.what(),a),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;}
        catch(const std::exception&){std::cout<<"{\"ok\":false,\"code\":\"resource-io-error\"}"<<std::endl;}
    }
    if(source)source->cancel();if(retained)retained->cancel();source.reset();retained.reset();
    if(pending.valid())pending.wait();return 0;
}
