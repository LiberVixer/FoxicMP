// Immutable private cache ownership journal and conservative restart recovery.
// No source paths/URLs, decoder activation, or age-based deletion. GPL-3.0-or-later.
#pragma once
#include "progressiveresourceio.h"
namespace foxicmp { namespace progressive { namespace stream_journal_detail {
using Handle=lease_detail::Handle;
using Directory=lease_detail::Directory;
inline void check(bool ok,const char* code="cache-journal-error"){Require(ok,"cacheJournal","Cache ownership proof failed",code);}
inline void close(Handle& h){
#ifdef _WIN32
    if(h.value!=INVALID_HANDLE_VALUE){CloseHandle(h.value);h.value=INVALID_HANDLE_VALUE;}
#else
    if(h.value>=0){::close(h.value);h.value=-1;}
#endif
}
inline Handle open(const Directory& dir,const std::string& name,bool create=false){
    check(name=="journal.next" || name=="journal.json" || name==".service","cache-journal-name");
#ifdef _WIN32
    lease_detail::Security security;
    Handle file(CreateFileW(lease_detail::NativePath(dir.path/std::filesystem::u8path(name)).c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,create?&security.attributes:nullptr,create?(name==".service"?OPEN_ALWAYS:CREATE_NEW):OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));check(file.value!=INVALID_HANDLE_VALUE,"cache-journal-unavailable");
#else
    Handle file(openat(dir.handle.value,name.c_str(),O_RDWR|O_CLOEXEC|O_NOFOLLOW|(create?O_CREAT:0)|(create && name!=".service"?O_EXCL:0),0600));check(file.value>=0,"cache-journal-unavailable");
#endif
    lease_detail::Owned(file.value,false);resource_detail::Inspect(file);return file;
}
inline std::string birth(const Handle& file){
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION info{};check(GetFileInformationByHandle(file.value,&info)!=FALSE);return std::to_string((uint64_t(info.ftCreationTime.dwHighDateTime)<<32)|info.ftCreationTime.dwLowDateTime);
#else
    struct statx info{};
    if(statx(file.value,"",AT_EMPTY_PATH,STATX_BTIME,&info) || !(info.stx_mask&STATX_BTIME))return {};
    return std::to_string(info.stx_btime.tv_sec)+":"+std::to_string(info.stx_btime.tv_nsec);
#endif
}
inline bool same(const resource_detail::Info& a,const resource_detail::Info& b){return a.device==b.device && a.fileId==b.fileId;}
inline void erase(const Directory& dir,const std::string& name){
#ifdef _WIN32
    check(DeleteFileW(lease_detail::NativePath(dir.path/std::filesystem::u8path(name)).c_str())!=FALSE,"cache-cleanup-failed");
#else
    check(unlinkat(dir.handle.value,name.c_str(),0)==0,"cache-cleanup-failed");check(fsync(dir.handle.value)==0,"cache-cleanup-failed");
#endif
}
inline bool exists(const Directory& dir,const std::string& name){
#ifdef _WIN32
    const auto attributes=GetFileAttributesW(lease_detail::NativePath(dir.path/std::filesystem::u8path(name)).c_str());if(attributes!=INVALID_FILE_ATTRIBUTES)return true;
    check(GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND);return false;
#else
    struct stat info{};if(fstatat(dir.handle.value,name.c_str(),&info,AT_SYMLINK_NOFOLLOW)==0)return true;check(errno==ENOENT);return false;
#endif
}
inline std::vector<std::string> entries(const Directory& dir,size_t limit){
    std::vector<std::string> names;
#ifdef _WIN32
    lease_detail::ForEachName(dir.path,[&](const std::string& name){check(names.size()<limit,"cache-recovery-scan-limit");names.push_back(name);});
#else
    DIR* raw=fdopendir(dup(dir.handle.value));check(raw!=nullptr);std::unique_ptr<DIR,int(*)(DIR*)> owner(raw,closedir);rewinddir(raw);
    while(auto* entry=readdir(raw)){std::string name=entry->d_name;if(name=="." || name=="..")continue;check(names.size()<limit,"cache-recovery-scan-limit");names.push_back(name);}
#endif
    std::sort(names.begin(),names.end());return names;
}
inline bool cacheName(const std::string& name){return name.size()==38 && name.compare(0,6,"remux-")==0 && lease_detail::HexId(name.substr(6));}
struct Record {
    std::string cache,session,outputBirth,journalBirth;uint64_t generation=0,limit=0;
    ProcessIdentity owner;resource_detail::Info folder,media,registry,journal;
};
inline std::string digest(const Value& value){rapidjson::StringBuffer bytes;rapidjson::Writer<rapidjson::StringBuffer> writer(bytes);Canonical(value,writer);Sha256 hash;hash.update(bytes.GetString(),bytes.GetSize());return hash.hex();}
inline rapidjson::Document encode(const Record& r){
    rapidjson::Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("version",1,a);
    auto text=[&](const char* key,const std::string& value){d.AddMember(Value(key,a),Value(value.c_str(),a),a);};
    text("cacheId",r.cache);text("sessionId",r.session);text("generation",std::to_string(r.generation));d.AddMember("pid",r.owner.pid,a);text("birth",r.owner.birth);
    text("directoryDevice",std::to_string(r.folder.device));text("directoryFileId",std::to_string(r.folder.fileId));text("outputDevice",std::to_string(r.media.device));text("outputFileId",std::to_string(r.media.fileId));text("outputBirth",r.outputBirth);
    text("journalDevice",std::to_string(r.journal.device));text("journalFileId",std::to_string(r.journal.fileId));text("journalBirth",r.journalBirth);
    text("registryDevice",std::to_string(r.registry.device));text("registryFileId",std::to_string(r.registry.fileId));text("limit",std::to_string(r.limit));text("sha256",digest(d));return d;
}
inline Record parse(const std::string& text){
    rapidjson::Document d;d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(text.data(),text.size());check(!d.HasParseError() && d.IsObject(),"cache-journal-invalid");harvester::UniqueKeys(d);
    Keys(d,"cacheJournal",{"version","cacheId","sessionId","generation","pid","birth","directoryDevice","directoryFileId","outputDevice","outputFileId","outputBirth","registryDevice","registryFileId","limit","journalDevice","journalFileId","journalBirth","sha256"});
    const auto expected=Text(d["sha256"],"sha256",64);d.RemoveMember("sha256");check(digest(d)==expected,"cache-journal-integrity");
    check(d["version"].IsUint() && d["version"].GetUint()==1 && d["pid"].IsUint() && d["pid"].GetUint()>0 && d["pid"].GetUint()<=INT32_MAX,"cache-journal-invalid");Record r;
    r.cache=Text(d["cacheId"],"cacheId",38);check(cacheName(r.cache),"cache-journal-invalid");r.session=Id(d["sessionId"],"sessionId",true);r.generation=Unsigned(d["generation"],"generation",UINT64_MAX,true);r.owner={d["pid"].GetUint(),Text(d["birth"],"birth",128)};
    r.folder={Unsigned(d["directoryDevice"],"directoryDevice"),Unsigned(d["directoryFileId"],"directoryFileId"),0};r.media={Unsigned(d["outputDevice"],"outputDevice"),Unsigned(d["outputFileId"],"outputFileId"),0};r.registry={Unsigned(d["registryDevice"],"registryDevice"),Unsigned(d["registryFileId"],"registryFileId"),0};r.journal={Unsigned(d["journalDevice"],"journalDevice"),Unsigned(d["journalFileId"],"journalFileId"),0};r.journalBirth=Text(d["journalBirth"],"journalBirth",128,true);r.outputBirth=Text(d["outputBirth"],"outputBirth",128,true);r.limit=Unsigned(d["limit"],"limit",64*1024*1024,true);return r;
}
struct Root {
    std::unique_ptr<Directory> directory;Handle lock;
    explicit Root(const std::filesystem::path& root,bool create):directory(std::make_unique<Directory>(root,create)){
        resource_detail::Directory checked(root);resource_detail::Inspect(directory->handle,true);auto opened=open(*directory,".service",true);lock.value=std::exchange(opened.value,
#ifdef _WIN32
        INVALID_HANDLE_VALUE
#else
        -1
#endif
        );check(lock.lock(),"cache-owner-active");
    }
};
}}}
namespace foxicmp { namespace progressive {
// Lock is held on the immutable journal itself until Core/media handles close.
class StreamCacheJournal {
    stream_journal_detail::Record record;
    lease_detail::Directory& directory;
    lease_detail::Handle file;
    resource_detail::Info identity;
    std::string bytes;
public:
    StreamCacheJournal(lease_detail::Directory& dir,const std::filesystem::path& registry,const std::string& cache,const std::string& session,uint64_t generation,const lease_detail::Handle& output,uint64_t limit):directory(dir){
        using namespace stream_journal_detail;
        lease_detail::Directory receipts(registry,true);
        record.cache=cache;record.session=session;record.generation=generation;record.limit=limit;record.owner=lease_detail::Current();record.folder=resource_detail::Inspect(dir.handle,true);record.media=resource_detail::Inspect(output);record.outputBirth=birth(output);record.registry=resource_detail::Inspect(receipts.handle,true);
        auto opened=open(directory,"journal.next",true);check(opened.lock(),"cache-journal-busy");file.value=std::exchange(opened.value,
#ifdef _WIN32
        INVALID_HANDLE_VALUE
#else
        -1
#endif
        );record.journal=resource_detail::Inspect(file);record.journalBirth=birth(file);bytes=lease_detail::Encode(encode(record));lease_detail::Write(file,bytes);identity=resource_detail::Inspect(file);
#ifdef _WIN32
        check(MoveFileExW(lease_detail::NativePath(directory.path/"journal.next").c_str(),lease_detail::NativePath(directory.path/"journal.json").c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE);
#else
        check(renameat2(directory.handle.value,"journal.next",directory.handle.value,"journal.json",RENAME_NOREPLACE)==0);check(fsync(directory.handle.value)==0);
#endif
    }
    void verifyRegistry(const lease_detail::Directory& receipts)const{stream_journal_detail::check(stream_journal_detail::same(record.registry,resource_detail::Inspect(receipts.handle,true)),"cache-registry-replaced");}
    void remove(){
        using namespace stream_journal_detail;
        // Verify the name still refers to our locked file, never an arbitrary replacement.
        {auto named=open(directory,"journal.json");check(same(resource_detail::Inspect(named),identity) && lease_detail::Read(named)==bytes,"cache-journal-replaced");}
        erase(directory,"journal.json");close(file);
    }
};
struct StreamCacheRecoveryEntry {std::string cache,code;uint64_t bytes=0;};
struct StreamCacheRecovery {
    size_t removed=0;bool ready=true;std::vector<StreamCacheRecoveryEntry> entries;
};
inline StreamCacheRecovery RecoverStreamCachePinned(stream_journal_detail::Root& root,const std::filesystem::path& registry){
    using namespace stream_journal_detail;
    // Bound admission before changing any candidate. Non-cache user files are ignored.
    const auto names=entries(*root.directory,512);std::vector<std::string> caches;
    for(const auto& name:names)if(cacheName(name)){check(caches.size()<128,"cache-recovery-scan-limit");caches.push_back(name);}
    lease_detail::Directory receipts(registry,true);const auto registryIdentity=resource_detail::Inspect(receipts.handle,true);StreamCacheRecovery out;
    for(const auto& name:caches){
        StreamCacheRecoveryEntry row{name,"",0};
        try{
            const auto path=root.directory->path/std::filesystem::u8path(name);auto directory=std::make_unique<Directory>(path,false);const auto children=entries(*directory,4);
            if(children.empty()){row.code="untracked-empty";out.entries.push_back(row);continue;}
            check(std::all_of(children.begin(),children.end(),[](const auto& child){return child=="journal.json" || child=="output.fmp4";}),"cache-unexpected-entry");
            auto journal=open(*directory,"journal.json");check(journal.lock(),"cache-owner-active");const auto journalIdentity=resource_detail::Inspect(journal);const auto bytes=lease_detail::Read(journal);const auto record=parse(bytes);
            check(same(record.journal,journalIdentity) && !record.journalBirth.empty() && birth(journal)==record.journalBirth,"cache-journal-replaced");
            check(record.cache==name && same(record.folder,resource_detail::Inspect(directory->handle,true)),"cache-directory-replaced");check(same(record.registry,registryIdentity),"cache-registry-replaced");
            const auto observation=lease_detail::Observe(record.owner);
            check(observation!=ProcessObservation::Same,"cache-owner-alive");check(observation==ProcessObservation::Exited || observation==ProcessObservation::ReusedPid,"cache-owner-unknown");
            auto guard=receipts.file(".guard",true);check(guard.lock(),"reader_registry_busy");
            for(const auto& id:receipts.ids()){
                const auto reader=lease_detail::Inspect(receipts,id);bool relevant=Text(reader["sessionId"],"sessionId")==record.session && Unsigned(reader["generation"],"generation")==record.generation;
                for(const auto& binding:reader["resources"].GetArray())if(Unsigned(binding["device"],"device")==record.media.device && Unsigned(binding["fileId"],"fileId")==record.media.fileId)relevant=true;
                check(!relevant || reader["released"].GetBool(),"cache-reader-active");
            }
            const bool haveOutput=exists(*directory,"output.fmp4");
            if(haveOutput){
                auto media=resource_detail::Directory(path).open("output.fmp4");lease_detail::Owned(media.value,false);const auto info=resource_detail::Inspect(media);row.bytes=info.size;
                check(same(info,record.media) && !record.outputBirth.empty() && birth(media)==record.outputBirth,"cache-output-replaced");check(info.size<=record.limit,"cache-output-size-changed");
            }
            // The immutable journal is checked again under its lock before unlink.
            {auto named=open(*directory,"journal.json");check(same(resource_detail::Inspect(named),journalIdentity) && lease_detail::Read(named)==bytes,"cache-journal-replaced");}
            check(entries(*directory,4)==children,"cache-directory-changed");
            if(haveOutput)erase(*directory,"output.fmp4");erase(*directory,"journal.json");close(journal);directory.reset();std::error_code error;std::filesystem::remove(
#ifdef _WIN32
                    lease_detail::NativePath(path),
#else
                    path,
#endif
                    error);check(!error,"cache-cleanup-failed");
            ++out.removed;row.code=haveOutput?"removed":"removed-metadata-only";
        }catch(const Error& e){row.code=e.code;out.ready=false;}catch(...){row.code="cache-recovery-error";out.ready=false;}
        out.entries.push_back(row);
    }
    return out;
}
inline StreamCacheRecovery RecoverStreamCache(const std::filesystem::path& root,const std::filesystem::path& registry){
    stream_journal_detail::Root owner(root,false);return RecoverStreamCachePinned(owner,registry);
}
}}
