// Player-owned reader receipts. GPL-3.0-or-later.
// The source closes ALL media handles before filesClosed(). No time-based release.
#pragma once
#include "progressivereaders.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <random>
#include <mutex>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <cerrno>
#endif

namespace foxicmp { namespace progressive {
namespace lease_detail {
namespace fs = std::filesystem;
constexpr size_t MaxRecordBytes = 16384;
inline void Check(bool value,const char* message,const char* code="reader_registry_error") {
    Require(value,"readerRegistry",message,code);
}
inline bool HexId(const std::string& id) {
    return id.size()==32 && id.find_first_not_of("0123456789abcdef")==std::string::npos;
}
inline std::string RandomId() {
    std::random_device random;const char* hex="0123456789abcdef";std::string out;
    for(unsigned i=0;i<4;++i) {auto value=uint32_t(random());for(unsigned j=0;j<8;++j)out+=hex[(value>>(j*4))&15];}
    return out;
}
#ifdef _WIN32
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):value(h){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE && value)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& b) noexcept:value(std::exchange(b.value,INVALID_HANDLE_VALUE)){}
    bool lock(bool wait=false) {OVERLAPPED o{};o.Offset=65536;return LockFileEx(value,LOCKFILE_EXCLUSIVE_LOCK|(wait?0:LOCKFILE_FAIL_IMMEDIATELY),0,1,0,&o)!=FALSE;}
};
inline std::vector<BYTE> User() {
    HANDLE raw=nullptr;Check(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw)!=FALSE,"Cannot read process owner");Handle token(raw);
    DWORD size=0;GetTokenInformation(token.value,TokenUser,nullptr,0,&size);Check(size>0 && size<65536,"Invalid user token");
    std::vector<BYTE> data(size);Check(GetTokenInformation(token.value,TokenUser,data.data(),size,&size)!=FALSE,"Cannot read user token");return data;
}
struct Security {
    PSECURITY_DESCRIPTOR descriptor=nullptr;SECURITY_ATTRIBUTES attributes{};
    Security() {
        auto user=User();LPWSTR sid=nullptr;
        Check(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid)!=FALSE,"Cannot encode owner");
        std::wstring text=L"O:"+std::wstring(sid)+L"D:P(A;;FA;;;"+std::wstring(sid)+L")";LocalFree(sid);
        Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(),SDDL_REVISION_1,&descriptor,nullptr)!=FALSE,"Cannot create private ACL");
        attributes={sizeof(attributes),descriptor,FALSE};
    }
    ~Security(){if(descriptor)LocalFree(descriptor);}
};
inline void Owned(HANDLE handle,bool directory) {
    BY_HANDLE_FILE_INFORMATION info{};
    Check(GetFileInformationByHandle(handle,&info)!=FALSE && GetFileType(handle)==FILE_TYPE_DISK &&
          !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) && bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory &&
          (directory || info.nNumberOfLinks==1),"Registry object is not a private regular object");
    PSID owner=nullptr;PACL acl=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;
    Check(GetSecurityInfo(handle,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&acl,nullptr,&descriptor)==ERROR_SUCCESS,"Cannot inspect registry ACL");
    auto user=User();auto sid=reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid;SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision=0;
    bool safe=owner && EqualSid(owner,sid) && acl && GetSecurityDescriptorControl(descriptor,&control,&revision) && (control&SE_DACL_PROTECTED);
    for(DWORD i=0;safe && i<acl->AceCount;++i) {
        void* raw=nullptr;safe=GetAce(acl,i,&raw)!=FALSE;
        if(safe) {auto ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);safe=ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE && EqualSid(&ace->SidStart,sid);}
    }
    LocalFree(descriptor);Check(safe,"Registry ACL must grant access only to its owner");
}
#else
struct Handle {
    int value=-1;
    explicit Handle(int fd=-1):value(fd){}
    ~Handle(){if(value>=0)::close(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& b) noexcept:value(std::exchange(b.value,-1)){}
    bool lock(bool wait=false){int result;do{result=flock(value,LOCK_EX|(wait?0:LOCK_NB));}while(wait && result && errno==EINTR);return result==0;}
};
inline void Owned(int fd,bool directory) {
    struct stat s{};
    Check(!fstat(fd,&s) && (directory?S_ISDIR(s.st_mode):S_ISREG(s.st_mode)) && s.st_uid==getuid() &&
          !(s.st_mode&0077) && (directory || s.st_nlink==1),"Registry object must be private, owned and free of links");
}
#endif
inline std::string Read(const Handle& file) {
#ifdef _WIN32
    LARGE_INTEGER size{},zero{};DWORD count=0;
    Check(GetFileSizeEx(file.value,&size) && size.QuadPart>0 && uint64_t(size.QuadPart)<=MaxRecordBytes && SetFilePointerEx(file.value,zero,nullptr,FILE_BEGIN),"Invalid reader receipt size");
    std::string text(size_t(size.QuadPart),'\0');Check(ReadFile(file.value,text.data(),DWORD(text.size()),&count,nullptr) && count==text.size(),"Cannot read reader receipt");
#else
    struct stat s{};Check(!fstat(file.value,&s) && s.st_size>0 && uint64_t(s.st_size)<=MaxRecordBytes,"Invalid reader receipt size");
    std::string text(size_t(s.st_size),'\0');Check(pread(file.value,text.data(),text.size(),0)==ssize_t(text.size()),"Cannot read reader receipt");
#endif
    return text;
}
inline void Write(const Handle& file,const std::string& text) {
    Check(!text.empty() && text.size()<=MaxRecordBytes,"Reader receipt exceeds limit");
#ifdef _WIN32
    LARGE_INTEGER zero{};DWORD count=0;
    Check(SetFilePointerEx(file.value,zero,nullptr,FILE_BEGIN) && WriteFile(file.value,text.data(),DWORD(text.size()),&count,nullptr) && count==text.size() && SetEndOfFile(file.value) && FlushFileBuffers(file.value),"Cannot persist reader receipt");
#else
    Check(pwrite(file.value,text.data(),text.size(),0)==ssize_t(text.size()) && !ftruncate(file.value,off_t(text.size())) && !fsync(file.value),"Cannot persist reader receipt");
#endif
}
struct Directory {
    fs::path path;Handle handle;
    explicit Directory(const fs::path& root,bool create):path(root),handle(openRoot(root,create)){}
    static Handle openRoot(const fs::path& root,bool create) {
        Check(root.is_absolute(),"Registry path must be absolute");
#ifdef _WIN32
        for(auto part=root.begin(),end=root.end();part!=end;++part) {
            fs::path prefix;for(auto p=root.begin();p!=std::next(part);++p)prefix/=*p;
            const auto attrs=GetFileAttributesW(prefix.c_str());
            Check(attrs==INVALID_FILE_ATTRIBUTES || !(attrs&FILE_ATTRIBUTE_REPARSE_POINT),"Reparse point in registry path");
        }
        if(create){Security s;if(!CreateDirectoryW(root.c_str(),&s.attributes))Check(GetLastError()==ERROR_ALREADY_EXISTS,"Cannot create registry directory");}
        Handle h(CreateFileW(root.c_str(),FILE_LIST_DIRECTORY|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
        Check(h.value!=INVALID_HANDLE_VALUE,"Reader registry is unavailable","unknown_reader");Owned(h.value,true);return h;
#else
        if(create && mkdir(root.c_str(),0700))Check(errno==EEXIST,"Cannot create registry directory");
        Handle h(::open(root.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
        Check(h.value>=0,"Reader registry is unavailable","unknown_reader");Owned(h.value,true);return h;
#endif
    }
    Handle file(const std::string& name,bool create) const {
        Check(name==".guard" || (name.size()==38 && name.substr(32)==".lease" && HexId(name.substr(0,32))),"Invalid receipt name");
#ifdef _WIN32
        Security s;Handle h(CreateFileW((path/fs::u8path(name)).c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(create && h.value==INVALID_HANDLE_VALUE)h.value=CreateFileW((path/fs::u8path(name)).c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,&s.attributes,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        else if(create && name!=".guard" && h.value!=INVALID_HANDLE_VALUE)Check(false,"Reader ID collision");
        Check(h.value!=INVALID_HANDLE_VALUE,"Reader receipt is unavailable","unknown_reader");Owned(h.value,false);return h;
#else
        const int flags=O_RDWR|O_CLOEXEC|O_NOFOLLOW|(create?O_CREAT:0)|((create && name!=".guard")?O_EXCL:0);
        Handle h(openat(handle.value,name.c_str(),flags,0600));Check(h.value>=0,"Reader receipt is unavailable","unknown_reader");Owned(h.value,false);return h;
#endif
    }
    std::vector<std::string> ids() const {
        std::vector<std::string> result;
#ifdef _WIN32
        // Holding the directory handle prevents replacement while enumerating.
        for(const auto& entry:fs::directory_iterator(path)) {
            auto name=entry.path().filename().u8string();
            if(name.size()==38 && name.substr(32)==".lease" && HexId(name.substr(0,32)))result.push_back(name.substr(0,32));
            Check(result.size()<=MaxReaders,"Reader receipt limit reached","reader_limit");
        }
#else
        DIR* raw=fdopendir(dup(handle.value));Check(raw!=nullptr,"Cannot enumerate reader receipts");
        std::unique_ptr<DIR,int(*)(DIR*)> entries(raw,closedir);
        rewinddir(raw);
        while(auto* entry=readdir(raw)) {
            std::string name=entry->d_name;
            if(name.size()==38 && name.substr(32)==".lease" && HexId(name.substr(0,32)))result.push_back(name.substr(0,32));
            Check(result.size()<=MaxReaders,"Reader receipt limit reached","reader_limit");
        }
#endif
        return result;
    }
    void erase(const std::string& id) const {
#ifdef _WIN32
        Check(DeleteFileW((path/fs::u8path(id+".lease")).c_str())!=FALSE,"Cannot forget reader receipt");
#else
        Check(!unlinkat(handle.value,(id+".lease").c_str(),0) && !fsync(handle.value),"Cannot forget reader receipt");
#endif
    }
};
inline ProcessObservation Observe(const ProcessIdentity& identity,ProcessIdentity* actual=nullptr) {
#ifdef _WIN32
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,identity.pid));
    if(!process.value)return GetLastError()==ERROR_INVALID_PARAMETER?ProcessObservation::Exited:ProcessObservation::Unknown;
    if(WaitForSingleObject(process.value,0)==WAIT_OBJECT_0)return ProcessObservation::Exited;
    FILETIME start{},end{},kernel{},user{};
    if(!GetProcessTimes(process.value,&start,&end,&kernel,&user))return ProcessObservation::Unknown;
    ProcessIdentity found{identity.pid,std::to_string((uint64_t(start.dwHighDateTime)<<32)|start.dwLowDateTime)};
#else
    std::ifstream boot("/proc/sys/kernel/random/boot_id");std::string bootId;std::getline(boot,bootId);
    std::ifstream stat("/proc/"+std::to_string(identity.pid)+"/stat");std::string line;std::getline(stat,line);
    if(!stat || bootId.size()!=36 || line.size()>8192) {
        return kill(pid_t(identity.pid),0)<0 && errno==ESRCH?ProcessObservation::Exited:ProcessObservation::Unknown;
    }
    const auto end=line.rfind(')');if(end==std::string::npos)return ProcessObservation::Unknown;
    std::istringstream fields(line.substr(end+1));std::string field,state,start;
    for(unsigned i=3;i<=22;++i){if(!(fields>>field))return ProcessObservation::Unknown;if(i==3)state=field;if(i==22)start=field;}
    if(start.empty() || start.find_first_not_of("0123456789")!=std::string::npos)return ProcessObservation::Unknown;
    if(state=="Z" || state=="X")return ProcessObservation::Exited;
    ProcessIdentity found{identity.pid,bootId+":"+start};
#endif
    if(actual)*actual=found;
    return found==identity?ProcessObservation::Same:ProcessObservation::ReusedPid;
}
inline ProcessIdentity Current() {
#ifdef _WIN32
    const auto pid=GetCurrentProcessId();
#else
    const auto pid=uint32_t(getpid());
#endif
    ProcessIdentity identity{pid,""},actual;
    Check(Observe(identity,&actual)==ProcessObservation::ReusedPid && !actual.birth.empty(),"Cannot obtain OS process creation identity");return actual;
}
inline std::string Encode(const Value& value) {rapidjson::StringBuffer b;rapidjson::Writer<rapidjson::StringBuffer>w(b);value.Accept(w);return {b.GetString(),b.GetSize()};}
inline rapidjson::Document Record(const std::string& text,const std::string& id) {
    rapidjson::Document d;d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(text.data(),text.size());
    Check(!d.HasParseError() && d.IsObject(),"Invalid reader receipt");harvester::UniqueKeys(d);
    Keys(d,"receipt",{"version","readerId","sessionId","generation","pid","birth","resources","filesClosed"});
    Check(d["version"].IsUint() && d["version"].GetUint()==1 && Id(d["readerId"],"readerId",true)==id,"Receipt identity changed");
    Id(d["sessionId"],"sessionId",true);Unsigned(d["generation"],"generation",UINT64_MAX,true);Text(d["birth"],"birth",128);
    Check(d["pid"].IsUint() && d["pid"].GetUint()>0 && d["pid"].GetUint()<=INT32_MAX && d["filesClosed"].IsBool(),"Invalid receipt process/state");
    Check(d["resources"].IsArray() && d["resources"].Size()>0 && d["resources"].Size()<=MaxResources,"Invalid reader resource scope");
    std::set<std::string> ids;
    for(const auto& r:d["resources"].GetArray()) {Keys(r,"resource",{"id","device","fileId"});Check(ids.insert(Id(r["id"],"resource.id")).second,"Duplicate bound resource");Unsigned(r["device"],"device");Unsigned(r["fileId"],"fileId");}
    return d;
}
inline rapidjson::Document Inspect(const Directory& directory,const std::string& id) {
    Check(HexId(id),"Invalid reader identifier","invalid_request");
    auto file=directory.file(id+".lease",false);const bool unlocked=file.lock();
    auto d=Record(Read(file),id);bool released=false;const char* reason="lease-held";
    if(unlocked) {
        if(d["filesClosed"].GetBool()){released=true;reason="files-closed";}
        else {
            ProcessIdentity process{d["pid"].GetUint(),Text(d["birth"],"birth",128)};
            const auto observation=Observe(process);
            released=observation==ProcessObservation::Exited || observation==ProcessObservation::ReusedPid;
            reason=observation==ProcessObservation::Exited?"process-exited":observation==ProcessObservation::ReusedPid?"pid-reused":"indeterminate";
        }
    }
    auto& a=d.GetAllocator();d.AddMember("released",released,a);d.AddMember("reason",Value(reason,a),a);return d;
}
} // namespace lease_detail

struct BoundResource {std::string id;uint64_t device=0,fileId=0;};
class ReaderLease {
    lease_detail::Directory directory;
    lease_detail::Handle file;
    ReaderBook book;ProcessIdentity peer;std::string reader,record;
    std::mutex mutex;bool closed=false;
    ReaderLease(const std::filesystem::path& root):directory(root,true){}
public:
    ~ReaderLease()=default; // NO implicit release: source must first close its media handles.
    static std::unique_ptr<ReaderLease> Create(const std::filesystem::path& root,const std::string& session,uint64_t generation,const std::vector<BoundResource>& resources) {
        using namespace lease_detail;
        auto lease=std::unique_ptr<ReaderLease>(new ReaderLease(root));auto guard=lease->directory.file(".guard",true);
        Check(guard.lock(),"Reader registry is busy","reader_registry_busy");Check(lease->directory.ids().size()<MaxReaders,"Acknowledge/forget old reader receipts first","reader_limit");
        Check(HexId(session) && generation>0 && !resources.empty() && resources.size()<=MaxResources,"Invalid session resource binding");
        lease->reader=RandomId();lease->peer=Current();
        // A unique_ptr makes the file handle stable; no copy or inherited handle is allowed.
        auto opened=lease->directory.file(lease->reader+".lease",true);lease->file.value=std::exchange(opened.value,
#ifdef _WIN32
            INVALID_HANDLE_VALUE
#else
            -1
#endif
        );Check(lease->file.lock(),"Cannot lock new reader receipt");
        Snapshot snapshot;snapshot.id=session;snapshot.generation=generation;std::set<std::string> scope;
        rapidjson::Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("version",1,a);
        d.AddMember("readerId",Value(lease->reader.c_str(),a),a);d.AddMember("sessionId",Value(session.c_str(),a),a);d.AddMember("generation",Value(std::to_string(generation).c_str(),a),a);
        d.AddMember("pid",lease->peer.pid,a);d.AddMember("birth",Value(lease->peer.birth.c_str(),a),a);d.AddMember("filesClosed",false,a);Value list(rapidjson::kArrayType);
        for(const auto& resource:resources) {
            Check(scope.insert(resource.id).second,"Duplicate resource binding");Value id;id.SetString(resource.id.c_str(),a);Id(id,"resource.id");
            Resource value;value.id=resource.id;snapshot.resources.emplace(resource.id,value);
            Value r(rapidjson::kObjectType);r.AddMember("id",Value(resource.id.c_str(),a),a);r.AddMember("device",Value(std::to_string(resource.device).c_str(),a),a);r.AddMember("fileId",Value(std::to_string(resource.fileId).c_str(),a),a);list.PushBack(r,a);
        }
        d.AddMember("resources",list,a);lease->book.attach(snapshot,lease->reader,lease->peer,scope);
        lease->record=Encode(d);Write(lease->file,lease->record);
#ifndef _WIN32
        Check(!fsync(lease->directory.handle.value),"Cannot persist reader directory");
#endif
        return lease;
    }
    const std::string& id() const {return reader;}
    void beginRelease() {std::lock_guard<std::mutex> lock(mutex);if(!closed)book.beginRelease(reader,peer);}
    void filesClosed() noexcept {
        std::lock_guard<std::mutex> lock(mutex);if(closed)return;
        try {
            // Serialize receipt mutation with inspection; all media handles are
            // already closed. Cleanup may wait here, never claim release early.
            auto guard=directory.file(".guard",true);lease_detail::Check(guard.lock(true),"Cannot lock reader registry for close");
            book.beginRelease(reader,peer);book.filesClosed(reader,peer);
            auto d=lease_detail::Record(record,reader);d["filesClosed"].SetBool(true);
            lease_detail::Write(file,lease_detail::Encode(d));closed=true;
        }catch(...) { /* Keep a conservative, unacknowledged receipt on I/O failure. */ }
    }
};

// Metadata-only admission, shared by GUI adapters and the worker handler.
inline rapidjson::Document ReaderCommand(const std::string& json) {
    using namespace lease_detail;
    Check(json.size()<=harvester::MaxManifestBytes,"Request exceeds limit","invalid_request");rapidjson::Document input;
    input.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(json.c_str());
    Check(!input.HasParseError() && input.IsObject(),"Invalid reader command","invalid_request");harvester::UniqueKeys(input);
    const auto action=input.HasMember("action")?Text(input["action"],"action",32):std::string();
    Keys(input,"request",action=="readers"?std::initializer_list<const char*>{"version","action","sessionId","generation"}:std::initializer_list<const char*>{"version","action","readerId"},action=="readers"?std::initializer_list<const char*>{"afterReaderId"}:std::initializer_list<const char*>{});
    Check(input["version"].IsUint() && input["version"].GetUint()==1 && (action=="readers" || action=="readerStatus" || action=="readerRelease" || action=="readerForget"),"Invalid reader command","invalid_request");
    if(action=="readers") {
        Id(input["sessionId"],"sessionId",true);Unsigned(input["generation"],"generation",UINT64_MAX,true);
        if(input.HasMember("afterReaderId"))Id(input["afterReaderId"],"afterReaderId",true);
    }else Id(input["readerId"],"readerId",true);
    return input;
}
// Same bounded command handler for Classic and Qt; never opens a media resource.
inline std::string ReaderRequest(const std::filesystem::path& root,const std::string& json) {
    using namespace lease_detail;rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("version",1,a);
    try {
        auto input=ReaderCommand(json);const auto action=Text(input["action"],"action",32);
        Directory directory(root,false);auto guard=directory.file(".guard",true);Check(guard.lock(),"Reader registry is busy","reader_registry_busy");
        if(action=="readers") {
            const auto session=Id(input["sessionId"],"sessionId",true);const auto generation=Unsigned(input["generation"],"generation",UINT64_MAX,true);Value list(rapidjson::kArrayType);
            auto cursor=input.HasMember("afterReaderId")?Id(input["afterReaderId"],"afterReaderId",true):std::string();
            auto ids=directory.ids();std::sort(ids.begin(),ids.end());size_t bytes=0;bool more=false;
            for(const auto& id:ids) {
                if(id<=cursor)continue;
                auto receipt=Inspect(directory,id);
                if(Text(receipt["sessionId"],"sessionId")==session && Unsigned(receipt["generation"],"generation")==generation) {
                    const auto size=Encode(receipt).size();if(list.Size()>=16 || bytes+size>48000){more=true;break;}
                    bytes+=size;list.PushBack(Value(receipt,a),a);
                }
                cursor=id;
            }
            out.AddMember("readers",list,a);out.AddMember("hasMore",more,a);
            if(more)out.AddMember("nextReaderId",Value(cursor.c_str(),a),a);
        }else {
            const auto id=Id(input["readerId"],"readerId",true);auto receipt=Inspect(directory,id);
            if(action=="readerForget") {Check(receipt["released"].GetBool(),"Reader still holds files or its state is uncertain","reader_in_use");directory.erase(id);out.AddMember("forgotten",true,a);}
            else out.AddMember("reader",Value(receipt,a),a);
        }
        out.AddMember("ok",true,a);
    }catch(const Error& e) {out.AddMember("ok",false,a);out.AddMember("code",Value(e.code.c_str(),a),a);out.AddMember("error",Value(e.what(),a),a);}
    catch(const std::exception& e) {out.AddMember("ok",false,a);out.AddMember("code","reader_registry_error",a);out.AddMember("error",Value(e.what(),a),a);}
    return Encode(out);
}
}} // namespace foxicmp::progressive
