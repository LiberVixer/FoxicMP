// Test-only syscall-result injection, restricted to one owned physical file.
// Reader receipt I/O is compiled before redirection and is never intercepted.
#include "../shared/progressivereaderlease.h"
#include <future>
#include <iostream>
#include <mutex>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace io_fault {
struct Identity {uint64_t device=0,file=0;};
#ifdef _WIN32
using Native=HANDLE;
Identity identity(Native file){BY_HANDLE_FILE_INFORMATION v{};if(!GetFileInformationByHandle(file,&v))return {};return {v.dwVolumeSerialNumber,(uint64_t(v.nFileIndexHigh)<<32)|v.nFileIndexLow};}
#else
using Native=int;
Identity identity(Native file){struct stat v{};if(fstat(file,&v))return {};return {uint64_t(v.st_dev),uint64_t(v.st_ino)};}
#endif
struct Event {std::string call,mode;uint64_t offset=0,requested=0,actualRequest=0;int error=0;};
std::mutex mutex;Identity target;std::string mode;unsigned phase=0;std::vector<Event> events;
bool match(Native file,bool write){if(mode.empty() || (mode.find("write")==0)!=write)return false;const auto v=identity(file);return v.device==target.device && v.file==target.file;}
void arm(const std::filesystem::path& path,const std::string& next){
#ifdef _WIN32
    foxicmp::progressive::lease_detail::Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr));
    if(file.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot arm test file");
#else
    foxicmp::progressive::lease_detail::Handle file(open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));if(file.value<0)throw std::runtime_error("Cannot arm test file");
#endif
    std::lock_guard<std::mutex> lock(mutex);target=identity(file.value);mode=next;phase=0;events.clear();
}
#ifdef _WIN32
BOOL WINAPI read(Native file,LPVOID buffer,DWORD count,LPDWORD received,LPOVERLAPPED op){
    std::lock_guard<std::mutex> lock(mutex);if(!match(file,false))return ReadFile(file,buffer,count,received,op);
    const auto offset=op?uint64_t(op->Offset)+(uint64_t(op->OffsetHigh)<<32):0;
    if(mode=="read-error"){events.push_back({"ReadFile",mode,offset,count,0,ERROR_READ_FAULT});if(received)*received=0;SetLastError(ERROR_READ_FAULT);return FALSE;}
    const DWORD actual=count/2;events.push_back({"ReadFile",mode,offset,count,actual,0});return ReadFile(file,buffer,actual,received,op);
}
BOOL WINAPI write(Native file,LPCVOID buffer,DWORD count,LPDWORD written,LPOVERLAPPED op){
    std::lock_guard<std::mutex> lock(mutex);if(!match(file,true))return WriteFile(file,buffer,count,written,op);
    const auto offset=op?uint64_t(op->Offset)+(uint64_t(op->OffsetHigh)<<32):0;
    if(mode=="write-partial"){const DWORD actual=count/2;events.push_back({"WriteFile",mode,offset,count,actual,0});return WriteFile(file,buffer,actual,written,op);}
    const DWORD error=mode=="write-full"?ERROR_DISK_FULL:ERROR_WRITE_FAULT;events.push_back({"WriteFile",mode,offset,count,0,int(error)});if(written)*written=0;SetLastError(error);return FALSE;
}
#else
ssize_t read(Native file,void* buffer,size_t count,off_t offset){
    std::lock_guard<std::mutex> lock(mutex);if(!match(file,false))return pread(file,buffer,count,offset);
    if(mode=="read-eintr"){events.push_back({"pread",mode,uint64_t(offset),count,0,EINTR});mode.clear();errno=EINTR;return -1;}
    if(mode=="read-error"){events.push_back({"pread",mode,uint64_t(offset),count,0,EIO});errno=EIO;return -1;}
    const auto actual=count/2;events.push_back({"pread",mode,uint64_t(offset),count,actual,0});return pread(file,buffer,actual,offset);
}
ssize_t write(Native file,const void* buffer,size_t count,off_t offset){
    std::lock_guard<std::mutex> lock(mutex);if(!match(file,true))return pwrite(file,buffer,count,offset);
    if(mode=="write-eintr"){events.push_back({"pwrite",mode,uint64_t(offset),count,0,EINTR});mode.clear();errno=EINTR;return -1;}
    if(mode=="write-short" || (mode=="write-partial" && phase++==0)){
        const auto actual=(std::max)(size_t(1),count/2);events.push_back({"pwrite",mode,uint64_t(offset),count,actual,0});if(mode=="write-short")mode.clear();return pwrite(file,buffer,actual,offset);
    }
    const auto error=mode=="write-full"?ENOSPC:EIO;events.push_back({"pwrite",mode,uint64_t(offset),count,0,error});errno=error;return -1;
}
#endif
}
#ifdef _WIN32
#define ReadFile io_fault::read
#define WriteFile io_fault::write
#else
#define pread io_fault::read
#define pwrite io_fault::write
#endif
#include "../shared/progressivecachedremux.h"
#ifdef _WIN32
#undef ReadFile
#undef WriteFile
#else
#undef pread
#undef pwrite
#endif
namespace p=foxicmp::progressive;
int main(int argc,char** argv){
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    if(argc!=3)return 2;const auto registry=std::filesystem::u8path(argv[1]),worker=std::filesystem::u8path(argv[2]);
    auto budget=std::make_shared<p::StreamCacheBudget>();auto owner=std::make_shared<p::StreamCacheOwner>(budget);
    std::unique_ptr<p::CachedRemuxSession> session;std::shared_ptr<p::StreamDecoderSource> source;
    std::shared_ptr<p::StreamDecoderCursor> cursor;std::unique_ptr<p::StreamFragmentDecoder> classic;
    std::future<int64_t> pending;std::vector<char> data(65536);std::string line;
    while(std::getline(std::cin,line))try{
        rapidjson::Document d;d.Parse(line.c_str());p::Require(!d.HasParseError() && d.IsObject(),"fixture","Invalid command");const auto action=p::Text(d["action"],"action");
        rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);
        auto text=[&](const char* k,const std::string& v){out.AddMember(p::Value(k,a),p::Value(v.c_str(),a),a);};
        if(action=="start"){
            session=std::make_unique<p::CachedRemuxSession>(std::filesystem::u8path(p::Text(d["directory"],"directory",8192)),registry,std::filesystem::u8path(p::Text(d["root"],"root",8192)),p::lease_detail::Encode(d["session"]),"clip",worker,owner);source=session->decoderSource();
        }else if(action=="views"){
            cursor=std::make_shared<p::StreamDecoderCursor>(source);classic=std::make_unique<p::StreamFragmentDecoder>(source);classic->refresh();text("cursorId",cursor->id());text("classicId",classic->id());out.AddMember("samples",uint64_t(classic->fragments().Samples().size()),a);
        }else if(action=="fault")io_fault::arm(std::filesystem::u8path(p::Text(d["path"],"path",8192)),p::Text(d["mode"],"mode"));
        else if(action=="faultClear"){std::lock_guard<std::mutex> lock(io_fault::mutex);io_fault::mode.clear();}
        else if(action=="faultStatus"){
            std::lock_guard<std::mutex> lock(io_fault::mutex);out.AddMember("device",io_fault::target.device,a);out.AddMember("fileId",io_fault::target.file,a);p::Value rows(rapidjson::kArrayType);
            for(const auto& e:io_fault::events){p::Value row(rapidjson::kObjectType);row.AddMember("call",p::Value(e.call.c_str(),a),a);row.AddMember("mode",p::Value(e.mode.c_str(),a),a);row.AddMember("offset",e.offset,a);row.AddMember("requested",e.requested,a);row.AddMember("actualRequest",e.actualRequest,a);row.AddMember("error",e.error,a);rows.PushBack(row,a);}out.AddMember("events",rows,a);
        }else if(action=="publish")session->publish(p::lease_detail::Encode(d["session"]));
        else if(action=="seek")out.AddMember("offset",cursor->seek(d["offset"].GetInt64()),a);
        else if(action=="beginRead"){p::Require(!pending.valid(),"fixture","Pending read");auto next=cursor;pending=std::async(std::launch::async,[&,next]{return next->read(data.data(),data.size());});}
        else if(action=="readResult"){
            if(pending.valid() && pending.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){out.AddMember("bytes",pending.get(),a);text("error",cursor->error());out.AddMember("eofs",cursor->eofCount(),a);}else out.AddMember("pending",true,a);
        }else if(action=="fragmentError"){
            try{classic->refresh();text("error","");}catch(const p::Error& e){text("error",e.code);}
        }else if(action=="closeViews"){if(cursor)cursor->close();cursor.reset();classic.reset();}
        else if(action=="status"){
            const auto s=session->status();const auto& o=s.output;const auto& v=s.source;
            out.AddMember("done",s.done,a);out.AddMember("originalsClosed",o.originalsClosed,a);out.AddMember("filesClosed",v.filesClosed,a);out.AddMember("complete",o.complete,a);out.AddMember("removed",o.removed,a);out.AddMember("producerClosed",o.producerClosed,a);out.AddMember("activeReaders",uint64_t(o.activeReaders),a);
            out.AddMember("workerId",v.workerId,a);out.AddMember("written",o.written,a);out.AddMember("visible",o.visible,a);out.AddMember("samples",o.samples,a);out.AddMember("maximumReadEnd",v.maximumReadEnd,a);out.AddMember("budgetBytes",budget->bytes(),a);out.AddMember("ownerCount",uint64_t(owner->count()),a);
            text("failure",o.failure);text("sourceCode",v.code);text("sourceResult",v.result);text("cleanupCode",o.cleanupCode);text("path",session->privatePath().u8string());
            p::Value ids(rapidjson::kArrayType);for(const auto& id:v.readerIds)ids.PushBack(p::Value(id.c_str(),a),a);out.AddMember("sourceReaderIds",ids,a);out.AddMember("cursorWaiting",cursor && cursor->isWaiting(),a);
        }else if(action=="retire")out.AddMember("removed",session->retire(),a);
        else if(action=="collect"){out.AddMember("removed",uint64_t(owner->collect()),a);out.AddMember("count",uint64_t(owner->count()),a);out.AddMember("bytes",budget->bytes(),a);}
        else if(action=="drop"){session.reset();source.reset();}
        else if(action=="readerStatus" || action=="readerForget" || action=="readers"){std::cout<<p::ReaderRequest(registry,line)<<std::endl;continue;}
        else p::Require(false,"fixture","Unknown command");
        std::cout<<p::lease_detail::Encode(out)<<std::endl;
    }catch(const p::Error& e){rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);out.AddMember("code",p::Value(e.code.c_str(),a),a);out.AddMember("message",p::Value(e.what(),a),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;}
    catch(const std::exception& e){std::cerr<<e.what()<<std::endl;std::cout<<"{\"ok\":false,\"code\":\"fixture-error\"}"<<std::endl;}
    {std::lock_guard<std::mutex> lock(io_fault::mutex);io_fault::mode.clear();}
    if(cursor)cursor->interrupt();if(pending.valid())pending.wait();cursor.reset();classic.reset();session.reset();source.reset();owner->collect();return 0;
}
