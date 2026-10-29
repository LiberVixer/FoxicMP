// Confirmed-range local I/O for manifest v2. GPL-3.0-or-later.
// No decoder, URLs, remux or implicit file deletion.
#pragma once
#include "progressivereaderlease.h"
#include <condition_variable>
#include <chrono>
#include <cstring>
#include <atomic>
#ifndef _WIN32
#include <sys/vfs.h>
#endif

namespace foxicmp { namespace progressive {
namespace resource_detail {
using lease_detail::Handle;
namespace fs=std::filesystem;
inline void Check(bool ok,const char* code,const char* message) {Require(ok,"resourceIo",message,code);}
struct Info {uint64_t device=0,fileId=0,size=0;};
inline Info Inspect(const Handle& file,bool directory=false) {
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION s{};
    Check(GetFileInformationByHandle(file.value,&s) && GetFileType(file.value)==FILE_TYPE_DISK &&
          !(s.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) && bool(s.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory &&
          (directory || s.nNumberOfLinks==1),"unsafe-resource","Expected a regular local object without links");
    PSID owner=nullptr;PSECURITY_DESCRIPTOR security=nullptr;
    const auto result=GetSecurityInfo(file.value,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION,&owner,nullptr,nullptr,nullptr,&security);
    auto user=lease_detail::User();bool owned=result==ERROR_SUCCESS && owner && EqualSid(owner,reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
    // Elevated Windows processes can create files with their token's default
    // owner group (e.g. Administrators). Accept precisely that token owner,
    // never an arbitrary group, account or owner supplied by the caller.
    if(!owned && result==ERROR_SUCCESS && owner) {
        HANDLE raw=nullptr;
        if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw)) {
            Handle token(raw);DWORD size=0;GetTokenInformation(token.value,TokenOwner,nullptr,0,&size);
            if(size>0 && size<65536) {
                std::vector<BYTE> bytes(size);
                if(GetTokenInformation(token.value,TokenOwner,bytes.data(),size,&size))owned=EqualSid(owner,reinterpret_cast<TOKEN_OWNER*>(bytes.data())->Owner)!=FALSE;
            }
        }
    }
    if(security)LocalFree(security);Check(owned,"unsafe-resource","Resource must belong to this OS user or its token's default owner");
    return {s.dwVolumeSerialNumber,(uint64_t(s.nFileIndexHigh)<<32)|s.nFileIndexLow,(uint64_t(s.nFileSizeHigh)<<32)|s.nFileSizeLow};
#else
    struct stat s{};struct statfs volume{};
    Check(!fstat(file.value,&s) && (directory?S_ISDIR(s.st_mode):S_ISREG(s.st_mode)) && s.st_uid==getuid() &&
          (directory || (s.st_nlink==1 && s.st_size>=0)),"unsafe-resource","Resource must be owned, regular and free of links");
    Check(!fstatfs(file.value,&volume),"resource-io-error","Cannot inspect resource volume");
    // NFS, CIFS/SMB and FUSE may fetch network bytes or indefinitely block a reader.
    const auto type=uint64_t(volume.f_type);
    Check(type!=0x6969 && type!=0x517b && type!=0xff534d42 && type!=0xfe534d42 && type!=0x65735546,"unsafe-resource","Network and FUSE resources are outside this local I/O adapter");
    return {uint64_t(s.st_dev),uint64_t(s.st_ino),directory?0:uint64_t(s.st_size)};
#endif
}
class Directory {
    fs::path root;
    Handle file;
#ifdef _WIN32
    static fs::path Final(HANDLE file) {
        std::wstring text(32768,L'\0');const auto count=GetFinalPathNameByHandleW(file,text.data(),DWORD(text.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        Check(count>0 && count<text.size(),"unsafe-resource","Cannot pin final local directory");text.resize(count);return fs::path(text);
    }
#endif
public:
    explicit Directory(const fs::path& path):root(path.lexically_normal()) {
        Check(root.is_absolute() && root.u8string().size()<=8192 && root.u8string().find('\0')==std::string::npos,"unsafe-resource","Expected an absolute local session directory");
#ifdef _WIN32
        Check(root.has_root_name() && root.root_name().wstring().size()==2 && root.root_name().wstring()[1]==L':' &&
              GetDriveTypeW(root.root_path().c_str())==DRIVE_FIXED,"unsafe-resource","Directory must use a local fixed drive");
        fs::path prefix;
        for(const auto& part:root) {prefix/=part;const auto attrs=GetFileAttributesW(prefix.c_str());Check(attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&FILE_ATTRIBUTE_REPARSE_POINT),"unsafe-resource","Missing directory or reparse point in its path");}
        file.value=CreateFileW(root.c_str(),FILE_LIST_DIRECTORY|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        Check(file.value!=INVALID_HANDLE_VALUE,"resource-io-error","Cannot pin session directory");Inspect(file,true);root=Final(file.value);
#else
        Handle current(::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC));Check(current.value>=0,"resource-io-error","Cannot open filesystem root");
        for(const auto& part:root.relative_path()) {
            Check(part!=".." && part!=".","unsafe-resource","Invalid directory component");
            Handle next(openat(current.value,part.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
            Check(next.value>=0,"unsafe-resource","Missing directory or symbolic link in its path");
            ::close(current.value);current.value=std::exchange(next.value,-1);
        }
        file.value=std::exchange(current.value,-1);Inspect(file,true);
#endif
    }
    Handle open(const std::string& name) const {
        PlainName(name,"resource.name");
#ifdef _WIN32
        const auto path=root/fs::u8path(name);
        Handle result(CreateFileW(path.c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_OVERLAPPED,nullptr));
        if(result.value==INVALID_HANDLE_VALUE) {
            const auto error=GetLastError();Check(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND,"waiting-resource","Resource has not appeared yet");
            Check(false,"resource-io-error","Cannot open local resource");
        }
        Inspect(result);const auto final=Final(result.value).wstring(),expected=path.wstring();
        Check(_wcsicmp(final.c_str(),expected.c_str())==0,"unsafe-resource","Resource path changed while opening");return result;
#else
        Handle result(openat(file.value,name.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
        if(result.value<0){Check(errno!=ENOENT,"waiting-resource","Resource has not appeared yet");Check(false,"unsafe-resource","Cannot open owned local resource without following links");}
        Inspect(result);return result;
#endif
    }
};
inline size_t ReadAt(const Handle& file,uint64_t offset,void* data,size_t size,const std::atomic<bool>& cancelled) {
    Check(offset<=MaxByte && size<=MaxByte-offset && size<=65536,"invalid-read","Invalid bounded read");
    if(cancelled.load())throw Error("cancelled","resourceIo","Reader was cancelled");
#ifdef _WIN32
    OVERLAPPED op{};op.Offset=DWORD(offset);op.OffsetHigh=DWORD(offset>>32);Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    Check(event.value!=nullptr,"resource-io-error","Cannot create resource I/O event");op.hEvent=event.value;DWORD received=0;
    BOOL ok=ReadFile(file.value,data,DWORD(size),&received,&op);
    if(!ok && GetLastError()==ERROR_IO_PENDING) {
        DWORD wait=WAIT_TIMEOUT;
        while((wait=WaitForSingleObject(op.hEvent,10))==WAIT_TIMEOUT && !cancelled.load()){}
        if(wait!=WAIT_OBJECT_0){CancelIoEx(file.value,&op);GetOverlappedResult(file.value,&op,&received,TRUE);}
        else ok=GetOverlappedResult(file.value,&op,&received,FALSE);
    }
    if(cancelled.load())throw Error("cancelled","resourceIo","Reader was cancelled");
    Check(ok!=FALSE,"resource-io-error","Cannot read pinned resource");return received;
#else
    ssize_t result;
    do {result=pread(file.value,data,size,off_t(offset));}while(result<0 && errno==EINTR && !cancelled.load());
    if(cancelled.load())throw Error("cancelled","resourceIo","Reader was cancelled");
    Check(result>=0,"resource-io-error","Cannot read pinned resource");return size_t(result);
#endif
}
struct Pin {
    Handle file;Info identity;std::unique_ptr<ReaderLease> lease;
    uint64_t lastOffset=0;std::vector<char> lastRead;
    Pin(Handle opened,Info info):file(std::move(opened)),identity(info){}
    ~Pin() {
#ifdef _WIN32
        if(file.value!=INVALID_HANDLE_VALUE){CloseHandle(file.value);file.value=INVALID_HANDLE_VALUE;}
#else
        if(file.value>=0){::close(file.value);file.value=-1;}
#endif
        if(lease)lease->filesClosed();
    }
    void verify(const Resource& resource,const std::atomic<bool>& cancelled) {
        const auto now=Inspect(file);const auto required=resource.ranges.empty()?0:resource.ranges.back().offset+resource.ranges.back().length;
        Check(now.device==identity.device && now.fileId==identity.fileId,"source-replaced","Pinned resource identity changed");
        Check(now.size>=required,"source-truncated","Confirmed bytes were truncated or never written");
        if(!lastRead.empty()) {
            std::vector<char> bytes(lastRead.size());
            Check(ReadAt(file,lastOffset,bytes.data(),bytes.size(),cancelled)==bytes.size() && bytes==lastRead,"source-rewritten","The last verified read was overwritten");
        }
    }
};
} // namespace resource_detail

struct ResourceRead {
    std::string code,reason;size_t bytes=0;
    bool data() const {return code=="data";}
};
enum class ResourceFailurePolicy {
    Strict,
    IsolateOptionalSrt,
    IsolatePendingAudioAndSrt,
    IsolateOptionalAudioAndSrt
};
class ResourceSource {
    using Directory=resource_detail::Directory;
    using Pin=resource_detail::Pin;
    std::unique_ptr<Directory> directory;
    std::filesystem::path registry;
    Snapshot snapshot;
    std::map<std::string,std::unique_ptr<Pin>> pins;
    mutable std::mutex mutex;std::condition_variable changed;
    std::atomic<bool> cancelled{false};
    std::string failure;
    ResourceFailurePolicy failurePolicy;
    std::map<std::string,ResourceRead> resourceFailures;
    std::set<std::string> activeMedia;
    bool isolate(const std::string& id) const {
        if(failurePolicy==ResourceFailurePolicy::Strict ||
           (activeMedia.count(id) && failurePolicy!=ResourceFailurePolicy::IsolateOptionalAudioAndSrt))return false;
        bool found=false;
        for(const auto& item:snapshot.tracks) {
            const auto& track=item.second;
            if(track.initialization==id)return false;
            for(const auto& segment:track.segments)
                if(segment.resource==id || segment.initialization==id)return false;
            if(track.resource==id) {
                const bool srt=track.kind=="subtitle" && track.container=="srt" && (track.codec.empty() || track.codec=="srt");
                const bool audio=(failurePolicy==ResourceFailurePolicy::IsolatePendingAudioAndSrt ||
                                  failurePolicy==ResourceFailurePolicy::IsolateOptionalAudioAndSrt) &&
                                 track.kind=="audio" && (track.container=="fmp4" || track.container=="webm");
                if(track.required || !(srt || audio))return false;
                found=true;
            }
        }
        return found;
    }
    void failed(const std::string& id,const ResourceRead& error) {
        if(isolate(id))resourceFailures.emplace(id,error);
        else failure=error.code;
        changed.notify_all();
    }
    void verifyPins(const Snapshot& next) {
        for(const auto& item:pins) {
            if(resourceFailures.count(item.first))continue;
            try{item.second->verify(next.resources.at(item.first),cancelled);}
            catch(const Error& error) {
                if(error.code=="cancelled")throw;
                failed(item.first,{error.code,error.what(),0});
                if(!isolate(item.first))throw;
            }
        }
    }
    std::shared_ptr<std::atomic<bool>> released=std::make_shared<std::atomic<bool>>(false);
    Pin& pin(const Resource& resource) {
        auto found=pins.find(resource.id);if(found!=pins.end())return *found->second;
        resource_detail::Check(!resource.retired,"resource-retired","Cannot attach a new reader to a retired resource");
        auto file=directory->open(resource.name);const auto info=resource_detail::Inspect(file);
        for(const auto& item:pins)resource_detail::Check(info.device!=item.second->identity.device || info.fileId!=item.second->identity.fileId,"resource-alias","Different resources refer to the same file");
        auto next=std::make_unique<Pin>(std::move(file),info);next->verify(resource,cancelled);
        next->lease=ReaderLease::Create(registry,snapshot.id,snapshot.generation,{{resource.id,info.device,info.fileId}});
        auto& result=*next;pins.emplace(resource.id,std::move(next));return result;
    }
    static uint64_t contiguous(const Resource& resource,uint64_t offset) {
        const auto start=offset;
        for(const auto& range:resource.ranges) {
            if(range.offset>offset)break;
            const auto end=range.offset+range.length;if(end>offset)offset=end;
        }
        return offset-start;
    }
public:
    ResourceSource(const std::filesystem::path& root,const std::filesystem::path& receipts,const std::string& json,ResourceFailurePolicy policy=ResourceFailurePolicy::Strict):registry(receipts),snapshot(Parse(json)),failurePolicy(policy) {
        directory=std::make_unique<Directory>(root);
        resource_detail::Check(snapshot.state!="failed" && snapshot.state!="cancelled","writer-failed","Cannot attach a failed/cancelled writer");
    }
    ~ResourceSource(){cancel();pins.clear();directory.reset();released->store(true);}
    ResourceSource(const ResourceSource&)=delete;ResourceSource& operator=(const ResourceSource&)=delete;
    void cancel(){cancelled.store(true);changed.notify_all();}
    void publish(const std::string& json) {
        const auto next=Parse(json);std::lock_guard<std::mutex> lock(mutex);
        resource_detail::Check(!cancelled.load() && failure.empty(),"session-closed","Resource source was closed");
        Advance(snapshot,next); // Invalid metadata never changes the active snapshot.
        verifyPins(next);
        snapshot=next;changed.notify_all();
    }
    ResourceRead read(const std::string& id,uint64_t offset,void* buffer,size_t count,std::chrono::milliseconds wait=std::chrono::milliseconds(0)) {
        using namespace resource_detail;
        if(offset>MaxByte || count>65536 || count>MaxByte-offset || (!buffer && count) || wait.count()<0 || wait>std::chrono::seconds(10))return {"invalid-read","Read exceeds limits",0};
        const auto deadline=std::chrono::steady_clock::now()+wait;std::unique_lock<std::mutex> lock(mutex);
        for(;;) {
            if(cancelled.load())return {"cancelled","Reader was cancelled",0};
            if(!failure.empty())return {failure,"Source previously failed",0};
            if(snapshot.state=="failed" || snapshot.state=="cancelled")return {"writer-failed","Writer failed or cancelled",0};
            const auto found=snapshot.resources.find(id);if(found==snapshot.resources.end())return {"unknown-resource","Resource ID is unknown",0};
            const auto& resource=found->second;
            const auto quarantined=resourceFailures.find(id);if(quarantined!=resourceFailures.end())return quarantined->second;
            if(resource.state=="failed" || resource.state=="cancelled")return {"resource-failed","Resource writer failed or cancelled",0};
            if(resource.known && offset>resource.length)return {"out-of-range","Offset exceeds resource length",0};
            if(!count)return {"data","Empty read",0};
            ResourceRead waiting{"waiting-range","Requested bytes are not confirmed",0};
            try {
                auto& opened=pin(resource);opened.verify(resource,cancelled);
                if(resource.state=="complete" && resource.known && offset==resource.length)return {"eof","Confirmed resource EOF",0};
                const auto ready=contiguous(resource,offset);
                if(ready) {
                    const auto size=size_t(std::min<uint64_t>(ready,count));std::vector<char> data(size);
                    Check(ReadAt(opened.file,offset,data.data(),size,cancelled)==size,"source-truncated","Confirmed read returned short physical data");
                    std::memcpy(buffer,data.data(),size);opened.lastOffset=offset;opened.lastRead=std::move(data);
                    return {"data","Confirmed bytes",size};
                }
            }catch(const Error& error) {
                if(error.code=="waiting-resource" && resource.state=="complete") {const ResourceRead result{"missing-resource","Completed resource is missing",0};failed(id,result);return result;}
                else if(error.code=="waiting-resource")waiting={error.code,error.what(),0};
                else if(error.code=="resource-retired" || error.code=="reader_registry_busy" || error.code=="reader_limit")return {error.code,error.what(),0};
                else {const ResourceRead result{error.code,error.what(),0};if(error.code!="cancelled")failed(id,result);return result;}
            }catch(const std::exception& error){const ResourceRead result{"resource-io-error",error.what(),0};failed(id,result);return result;}
            if(std::chrono::steady_clock::now()>=deadline)return waiting;
            changed.wait_until(lock,(std::min)(deadline,std::chrono::steady_clock::now()+std::chrono::milliseconds(25)));
        }
    }
    std::vector<std::string> readerIds() const {std::lock_guard<std::mutex> lock(mutex);std::vector<std::string> ids;for(const auto& pin:pins)ids.push_back(pin.second->lease->id());return ids;}
    BoundResource binding(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex);const auto found=pins.find(id);
        resource_detail::Check(found!=pins.end(),"unknown-resource","Resource has not been pinned");
        return {id,found->second->identity.device,found->second->identity.fileId};
    }
    void verifyPinned() {
        std::lock_guard<std::mutex> lock(mutex);
        resource_detail::Check(!cancelled.load() && failure.empty(),"session-closed","Resource source was closed");
        verifyPins(snapshot);
    }
    // Activation retains strict behavior for the default/pending policies.
    // The bounded optional-audio policy keeps failures local after activation.
    void activateMedia(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex);
        resource_detail::Check(!resourceFailures.count(id),"resource-failed","Cannot activate a quarantined resource");
        activeMedia.insert(id);
    }
    ResourceRead resourceError(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found=resourceFailures.find(id);
        return found==resourceFailures.end()?ResourceRead{}:found->second;
    }
    std::shared_ptr<std::atomic<bool>> releaseReceipt() const {return released;}
};
}} // namespace foxicmp::progressive
