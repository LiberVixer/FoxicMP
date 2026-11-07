// Private bounded continuous-remux output, complete-fragment publication and leases.
// Internal component only. GPL-3.0-or-later.
#pragma once
#include "progressiveresourceio.h"
#include "progressivestreamjournal.h"
#include "DSUtil/HarvesterMP4.h"
#include "progressivestreamavindex.h"

namespace foxicmp { namespace progressive {
class StreamCacheBudget {
    mutable std::mutex mutex;uint64_t used=0,limit;
public:
    explicit StreamCacheBudget(uint64_t bytes=64*1024*1024):limit(bytes){Require(bytes>0 && bytes<=64*1024*1024,"remuxCache","Invalid shared cache budget","invalid_request");}
    void acquire(uint64_t bytes){std::lock_guard<std::mutex> lock(mutex);Require(bytes<=limit-used,"remuxCache","Shared output cache budget exhausted","cache-budget-exceeded");used+=bytes;}
    void release(uint64_t bytes){std::lock_guard<std::mutex> lock(mutex);used-=bytes;}
    uint64_t bytes()const{std::lock_guard<std::mutex> lock(mutex);return used;}
};
namespace stream_cache_detail {
using Handle=lease_detail::Handle;
inline void close(Handle& h){
#ifdef _WIN32
    if(h.value!=INVALID_HANDLE_VALUE){CloseHandle(h.value);h.value=INVALID_HANDLE_VALUE;}
#else
    if(h.value>=0){::close(h.value);h.value=-1;}
#endif
}
inline void check(bool ok,const char* code="cache-io-error"){Require(ok,"remuxCache","Private output cache operation failed",code);}
inline void write(const Handle& file,uint64_t at,const std::vector<char>& data){
#ifdef _WIN32
    OVERLAPPED op{};op.Offset=DWORD(at);op.OffsetHigh=DWORD(at>>32);op.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(op.hEvent!=nullptr);
    DWORD count=0;bool pending=false;DWORD error=ERROR_SUCCESS;
    if(!WriteFile(file.value,data.data(),DWORD(data.size()),&count,&op)){
        error=GetLastError();pending=error==ERROR_IO_PENDING;
        if(pending){
            const auto wait=WaitForSingleObject(op.hEvent,2000);
            if(wait!=WAIT_OBJECT_0){CancelIoEx(file.value,&op);GetOverlappedResult(file.value,&op,&count,TRUE);error=ERROR_TIMEOUT;}
            else if(!GetOverlappedResult(file.value,&op,&count,FALSE))error=GetLastError();else error=ERROR_SUCCESS;
        }
    }
    CloseHandle(op.hEvent);
    check(error==ERROR_SUCCESS && count==data.size(),error==ERROR_DISK_FULL || error==ERROR_HANDLE_DISK_FULL?"cache-disk-full":error==ERROR_TIMEOUT?"cache-io-timeout":"cache-io-error");
#else
    size_t written=0;while(written<data.size()){
        const auto n=pwrite(file.value,data.data()+written,data.size()-written,off_t(at+written));
        if(n<0 && errno==EINTR)continue;
        check(n>0,n<0 && (errno==ENOSPC || errno==EDQUOT)?"cache-disk-full":"cache-io-error");written+=size_t(n);
    }
#endif
}
}
struct StreamCacheStatus {
    uint64_t written=0,visible=0,fragments=0,samples=0;size_t activeReaders=0;
    bool initialized=false,complete=false,producerClosed=false,originalsClosed=false,retiring=false,removed=false;
    std::string failure,cleanupCode,audioWindow;std::vector<std::string> readerIds;
    uint64_t videoSamples=0,audioSamples=0;
};
class StreamOutputStore {
    using Handle=lease_detail::Handle;
    struct Core {
        std::mutex mutex;std::condition_variable changed;
        std::shared_ptr<StreamCacheBudget> budget;
        std::filesystem::path registry,folder,path;
        std::unique_ptr<lease_detail::Directory> parent,directory;
        Handle writer;resource_detail::Info identity;std::unique_ptr<StreamCacheJournal> journal;
        std::string session,resource;uint64_t generation,limit,charged=0,scan=0;
        struct Chunk {uint64_t offset;size_t length;std::array<unsigned char,32> digest;};
        std::vector<Chunk> chunks;
        std::vector<std::string> originals;
        StreamCacheStatus state;
        Sha256 digest;harvester::FragmentIndex index;
        std::unique_ptr<StreamAvIndex> avIndex;
        bool ftyp=false,trailer=false;
        std::atomic<bool> cancelled{false};
        Core(const std::filesystem::path& root,const std::filesystem::path& receipts,const std::string& id,uint64_t gen,std::shared_ptr<StreamCacheBudget> shared,uint64_t bytes)
            :budget(std::move(shared)),registry(receipts),session(id),generation(gen),limit(bytes){
            check(bool(budget) && lease_detail::HexId(id) && gen>0 && bytes>0 && bytes<=64*1024*1024,"invalid_request");
            // Validate a local, owner-only parent; one new private folder per store.
            parent=std::make_unique<lease_detail::Directory>(root,true);resource_detail::Inspect(parent->handle,true);
            resource="remux-"+lease_detail::RandomId();folder=root/std::filesystem::u8path(resource);path=folder/"output.fmp4";
            #ifdef _WIN32
            {lease_detail::Security security;check(CreateDirectoryW(lease_detail::NativePath(folder).c_str(),&security.attributes)!=FALSE,"cache-directory-exists");}
#else
            check(mkdir(folder.c_str(),0700)==0,"cache-directory-exists");
#endif
            directory=std::make_unique<lease_detail::Directory>(folder,false);
            resource_detail::Inspect(directory->handle,true);
#ifdef _WIN32
            lease_detail::Security security;
            writer.value=CreateFileW(lease_detail::NativePath(path).c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,&security.attributes,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_OVERLAPPED,nullptr);
            check(writer.value!=INVALID_HANDLE_VALUE);
#else
            writer.value=openat(directory->handle.value,"output.fmp4",O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);check(writer.value>=0);
#endif
            identity=resource_detail::Inspect(writer);lease_detail::Owned(writer.value,false);
            journal=std::make_unique<StreamCacheJournal>(*directory,registry,resource,session,generation,writer,limit);
        }
        static void check(bool ok,const char* code="cache-io-error"){stream_cache_detail::check(ok,code);}
        Handle openReader(bool checkSize=true){
#ifdef _WIN32
            Handle file(CreateFileW(lease_detail::NativePath(path).c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_OVERLAPPED,nullptr));check(file.value!=INVALID_HANDLE_VALUE,"cache-path-replaced");
#else
            Handle file(openat(directory->handle.value,"output.fmp4",O_RDONLY|O_CLOEXEC|O_NOFOLLOW));check(file.value>=0,"cache-path-replaced");
#endif
            verify(file,checkSize);return file;
        }
        void verify(const Handle& file,bool checkSize=true){const auto now=resource_detail::Inspect(file);check(now.device==identity.device && now.fileId==identity.fileId,"cache-path-replaced");lease_detail::Owned(file.value,false);if(checkSize)check(now.size==state.written,"cache-size-changed");}
        void protectedRead(const Handle& file,uint64_t at,void* out,size_t n){
            check(at<=state.written && n<=state.written-at,"cache-invalid-read");if(!n)return;
            verify(file);auto* destination=static_cast<char*>(out);size_t copied=0;
            auto i=std::upper_bound(chunks.begin(),chunks.end(),at,[](uint64_t p,const Chunk& c){return p<c.offset;});if(i!=chunks.begin())--i;
            while(copied<n){check(i!=chunks.end() && at>=i->offset && at<i->offset+i->length,"cache-integrity-missing");
                std::vector<char> bytes(i->length);check(resource_detail::ReadAt(file,i->offset,bytes.data(),bytes.size(),cancelled)==bytes.size());Sha256 hash;hash.update(bytes.data(),bytes.size());check(hash.finish()==i->digest,"cache-integrity-mismatch");
                const auto delta=size_t(at-i->offset),count=(std::min)(n-copied,bytes.size()-delta);std::memcpy(destination+copied,bytes.data()+delta,count);at+=count;copied+=count;++i;
            }
        }
        struct Atom {uint64_t at=0,size=0;uint32_t type=0;size_t header=8;};
        bool header(uint64_t at,Atom& atom){
            if(at>state.written || state.written-at<8)return false;
            uint8_t b[16]{};protectedRead(writer,at,b,8);harvester::BoxData d{b,16};atom={at,d.U32(0),d.U32(4),8};
            if(atom.size==1){if(state.written-at<16)return false;protectedRead(writer,at+8,b+8,8);atom.size=d.U64(8);atom.header=16;}
            check(atom.size>=atom.header && atom.size<=limit-at,"cache-invalid-fragment");return true;
        }
        void advance(){
            auto raw=[&](uint64_t at,void* out,size_t n){protectedRead(writer,at,out,n);return true;};
            for(;;){
                Atom a;if(!header(scan,a))break;
                check(!trailer,"cache-invalid-fragment");
                const auto kind=a.type;
                if(kind==harvester::FourCC("moof")){
                    check(state.initialized && a.size<=1048576,"cache-invalid-fragment");if(a.size>state.written-a.at)break;
                    Atom m;if(!header(a.at+a.size,m))break;check(m.type==harvester::FourCC("mdat") && m.size<=16*1024*1024+16,"cache-invalid-fragment");if(m.size>state.written-m.at)break;
                    std::vector<uint8_t> metadata(size_t(a.size-a.header));raw(a.at+a.header,metadata.data(),metadata.size());harvester::BoxData body{metadata.data(),metadata.size()};
                    if(avIndex){
                        auto next=*avIndex;next.update(raw,m.at+m.size);check(next.position()==m.at+m.size && next.samples()>state.samples,"cache-invalid-fragment");
                        *avIndex=std::move(next);state.samples=avIndex->samples();state.videoSamples=avIndex->views().tracks[0].index.Samples().size();state.audioSamples=avIndex->views().tracks[1].index.Samples().size();
                    }else{
                        uint64_t samples=0;for(const auto& child:body.One("traf").Children())if(child.first==harvester::FourCC("trun")){const auto n=child.second.U32(4);check(n>0 && n<=50000-state.samples-samples,"cache-sample-limit");samples+=n;}
                        check(samples>0,"cache-invalid-fragment");auto next=index;next.Update(raw,m.at+m.size,0);check(next.Position()==m.at+m.size && next.Samples().size()==state.samples+samples,"cache-invalid-fragment");index=std::move(next);state.samples+=samples;
                    }
                    ++state.fragments;scan=m.at+m.size;state.visible=scan;
                }else{
                    check(a.size<=1048576,"cache-invalid-fragment");if(a.size>state.written-a.at)break;
                    if(kind==harvester::FourCC("ftyp")){check(!ftyp && scan==0 && a.size>=16,"cache-invalid-fragment");ftyp=true;}
                    else if(kind==harvester::FourCC("moov")){
                        check(ftyp && !state.initialized && a.at+a.size<=1048576,"cache-invalid-fragment");
                        if(avIndex){auto next=*avIndex;next.update(raw,a.at+a.size);check(next.initialized() && next.samples()==0,"cache-invalid-fragment");*avIndex=std::move(next);}
                        else{auto next=index;next.Update(raw,a.at+a.size,0);check(next.Initialized() && next.Samples().empty(),"cache-invalid-fragment");index=std::move(next);}
                        state.initialized=true;state.visible=a.at+a.size;
                    }else if(kind==harvester::FourCC("mfra")){check(state.fragments>0,"cache-invalid-fragment");trailer=true;}
                    else check(false,"cache-invalid-fragment");
                    scan=a.at+a.size;
                }
            }
        }
        void fail(const std::string& code){if(state.failure.empty())state.failure=code;state.complete=false;changed.notify_all();}
        bool cleanup(){
            if(state.removed)return true;
            if(!state.retiring || !state.producerClosed || !state.originalsClosed || state.activeReaders){state.cleanupCode="cache-in-use";return false;}
            try{
                lease_detail::Directory receipts(registry,true);journal->verifyRegistry(receipts);auto guard=receipts.file(".guard",true);
                if(!guard.lock()){state.cleanupCode="reader_registry_busy";return false;}
                const auto ids=receipts.ids();
                for(const auto& id:ids){auto record=lease_detail::Inspect(receipts,id);bool relevant=std::find(originals.begin(),originals.end(),id)!=originals.end();
                    for(const auto& binding:record["resources"].GetArray())if(Unsigned(binding["device"],"device")==identity.device && Unsigned(binding["fileId"],"fileId")==identity.fileId)relevant=true;
                    if(relevant && !record["released"].GetBool()){state.cleanupCode="cache-in-use";return false;}
                }
                // Refuse to unlink a replacement at the old name, including links.
                {auto file=openReader(false);}
#ifdef _WIN32
                check(DeleteFileW(lease_detail::NativePath(path).c_str())!=FALSE,"cache-cleanup-failed");
#else
                check(unlinkat(directory->handle.value,"output.fmp4",0)==0,"cache-cleanup-failed");
#endif
                journal->remove();journal.reset();directory.reset();std::error_code error;std::filesystem::remove(
#ifdef _WIN32
                    lease_detail::NativePath(folder),
#else
                    folder,
#endif
                    error);
                // An unexpected extra file is never recursively removed.
                state.removed=true;state.cleanupCode=error?"removed-folder-retained":"removed";budget->release(charged);charged=0;return true;
            }catch(const Error& e){state.cleanupCode=e.code;return false;}catch(...){state.cleanupCode="cache-cleanup-failed";return false;}
        }
        ~Core(){stream_cache_detail::close(writer);state.producerClosed=true;state.retiring=true;cleanup();}
    };
    std::shared_ptr<Core> core;
public:
    class Reader {
        std::shared_ptr<Core> core;Handle file;std::unique_ptr<ReaderLease> lease;std::string readerId;bool closed=false;
        friend class StreamOutputStore;
        explicit Reader(std::shared_ptr<Core> value):core(std::move(value)){
            std::lock_guard<std::mutex> lock(core->mutex);Core::check(core->state.readerIds.size()<MaxReaders,"cache-reader-limit");Core::check(!core->state.retiring && !core->state.removed && core->state.failure.empty(),"cache-closed");lease_detail::Directory receipts(core->registry,true);core->journal->verifyRegistry(receipts);auto opened=core->openReader();file.value=std::exchange(opened.value,
#ifdef _WIN32
            INVALID_HANDLE_VALUE
#else
            -1
#endif
            );lease=ReaderLease::Create(core->registry,core->session,core->generation,{{core->resource,core->identity.device,core->identity.fileId}});readerId=lease->id();core->state.readerIds.push_back(readerId);++core->state.activeReaders;
        }
    public:
        ~Reader(){close();}
        Reader(const Reader&)=delete;Reader& operator=(const Reader&)=delete;
        std::string id()const{return readerId;}
        ResourceRead read(uint64_t at,void* out,size_t count,std::chrono::milliseconds wait=std::chrono::milliseconds(0)){
            if(count>65536 || (!out && count) || at>64*1024*1024 || wait.count()<0 || wait>std::chrono::seconds(10))return {"invalid-read","Invalid cache read",0};
            std::unique_lock<std::mutex> lock(core->mutex);const auto deadline=std::chrono::steady_clock::now()+wait;
            for(;;){
                if(closed)return {"cancelled","Output reader closed",0};
                if(!core->state.failure.empty())return {core->state.failure,"Output producer failed",0};
                if(!count)return {"data","Empty cache read",0};
                if(at<core->state.visible){const auto n=size_t((std::min)(uint64_t(count),core->state.visible-at));try{core->protectedRead(file,at,out,n);return {"data","Complete validated output",n};}catch(const Error& e){core->fail(e.code);return {e.code,e.what(),0};}}
                if(core->state.complete)return at==core->state.visible?ResourceRead{"eof","Confirmed output EOF",0}:ResourceRead{"out-of-range","Offset past final output",0};
                if(std::chrono::steady_clock::now()>=deadline)return {"waiting-range","Complete output fragment unavailable",0};
                core->changed.wait_until(lock,deadline);
            }
        }
        void close(){std::lock_guard<std::mutex> lock(core->mutex);if(closed)return;stream_cache_detail::close(file);lease->filesClosed();lease.reset();closed=true;--core->state.activeReaders;core->changed.notify_all();core->cleanup();}
    };
    StreamOutputStore(const std::filesystem::path& root,const std::filesystem::path& registry,const std::string& session,uint64_t generation,std::shared_ptr<StreamCacheBudget> budget,uint64_t limit=64*1024*1024)
        :core(std::make_shared<Core>(root,registry,session,generation,std::move(budget),limit)){}
    ~StreamOutputStore(){std::lock_guard<std::mutex> lock(core->mutex);core->state.retiring=true;core->cleanup();}
    void acceptAudioWindow(const StreamAudioWindow& window){
        std::lock_guard<std::mutex> lock(core->mutex);
        Core::check(!core->state.written && !core->avIndex && !core->state.producerClosed && !core->state.retiring && core->state.failure.empty(),"cache-invalid-audio-window");
        // Validate again at the cache boundary; all readers share this immutable plan.
        rapidjson::Document d;d.Parse(window.json.c_str());Core::check(!d.HasParseError(),"cache-invalid-audio-window");
        auto verified=StreamAudioWindow::Parse(d);Core::check(verified.sha256==window.sha256,"cache-invalid-audio-window");
        core->avIndex=std::make_unique<StreamAvIndex>(verified);core->state.audioWindow=verified.json;
    }
    void append(uint64_t at,const std::vector<char>& bytes){
        std::lock_guard<std::mutex> lock(core->mutex);
        try{
            Core::check(!core->state.producerClosed && !core->state.retiring && core->state.failure.empty(),"cache-closed");Core::check(at==core->state.written && !bytes.empty() && bytes.size()<=65536,"cache-invalid-append");
            Core::check(bytes.size()<=core->limit-core->state.written,"cache-output-limit");Core::check(core->chunks.size()<8192,"cache-chunk-limit");
            core->budget->acquire(bytes.size());core->charged+=bytes.size();stream_cache_detail::write(core->writer,at,bytes);
            Sha256 hash;hash.update(bytes.data(),bytes.size());core->chunks.push_back({at,bytes.size(),hash.finish()});core->digest.update(bytes.data(),bytes.size());core->state.written+=bytes.size();
            core->advance();core->changed.notify_all();
        }catch(const Error& e){core->fail(e.code);throw;}catch(const std::exception&){core->fail("cache-invalid-fragment");throw Error("cache-invalid-fragment","remuxCache","Invalid output structure");}
    }
    void producerClosed(const std::vector<std::string>& originalReaders,const std::string& error,const std::string& expectedSha256={}){
        std::lock_guard<std::mutex> lock(core->mutex);if(core->state.producerClosed)return;
        if(!error.empty())core->fail(error);
        if(core->state.failure.empty())try{
            Core::check(core->scan==core->state.written && core->state.initialized && core->state.fragments>0,"cache-incomplete-output");Core::check(core->digest.hex()==expectedSha256,"cache-integrity-mismatch");
            if(core->avIndex)core->avIndex->update([&](uint64_t at,void* out,size_t n){core->protectedRead(core->writer,at,out,n);return true;},core->state.written,true);
            core->verify(core->writer);core->state.visible=core->state.written;core->state.complete=true;
        }catch(const Error& e){core->fail(e.code);}
        core->originals=originalReaders;core->state.originalsClosed=true;stream_cache_detail::close(core->writer);core->state.producerClosed=true;core->changed.notify_all();core->cleanup();
    }
    std::shared_ptr<Reader> reader(){return std::shared_ptr<Reader>(new Reader(core));}
    StreamCacheStatus status()const{std::lock_guard<std::mutex> lock(core->mutex);return core->state;}
    harvester::muxmp4::Layout avLayout()const{
        std::lock_guard<std::mutex> lock(core->mutex);
        Core::check(bool(core->avIndex) && core->state.initialized && core->state.failure.empty(),"cache-invalid-audio-window");return core->avIndex->views();
    }
    // Internal index diagnostics, physical offsets into this same protected cache.
    // Does not grant decoder admission or bypass Reader SHA/lease checks.
    std::vector<harvester::Sample> avSamples(unsigned kind)const{
        std::lock_guard<std::mutex> lock(core->mutex);Core::check(kind<2 && bool(core->avIndex),"cache-invalid-audio-window");
        const auto& view=core->avIndex->views().tracks[kind];auto values=view.index.Samples();
        for(auto& sample:values){
            auto i=std::upper_bound(view.spans.begin(),view.spans.end(),sample.offset,[](uint64_t at,const harvester::muxmp4::Span& span){return at<span.offset;});
            Core::check(i!=view.spans.begin(),"cache-invalid-fragment");--i;
            Core::check(!i->memory && sample.offset>=i->offset && sample.offset-i->offset<=i->length && sample.size<=i->length-(sample.offset-i->offset),"cache-invalid-fragment");
            sample.offset=i->physical+sample.offset-i->offset;
        }
        return values;
    }
    std::string sessionId()const{return core->session;}
    uint64_t generation()const{return core->generation;}
    // Immutable binding: no cache mutex, file inspection or registry I/O.
    BoundResource readerResource()const{return {core->resource,core->identity.device,core->identity.fileId};}
    bool retire(){std::lock_guard<std::mutex> lock(core->mutex);core->state.retiring=true;core->changed.notify_all();return core->cleanup();}
    // Private diagnostics for tests/owner only, never a decoder filesystem URL.
    std::filesystem::path privatePath()const{return core->path;}
};
// Service-owned queue. No periodic timer and no age-based deletion. Keep this
// owner for the lifetime of the service; retry collect on close/status/release.
class StreamCacheOwner {
    mutable std::mutex mutex;
    std::shared_ptr<StreamCacheBudget> budget;
    std::vector<std::shared_ptr<StreamOutputStore>> stores;
    size_t limit;
    struct Fence {std::unique_ptr<stream_journal_detail::Root> root;std::filesystem::path registry;resource_detail::Info registryIdentity;bool ready=false;};
    std::map<std::filesystem::path,Fence> roots;
    void admit(const std::filesystem::path& root,const std::filesystem::path& registry){
        // Retained or unprovable orphans block new allocation, rather than
        // silently forgetting their disk use in a fresh process budget.
        for(auto& entry:roots)if(!entry.second.ready){entry.second.ready=RecoverStreamCachePinned(*entry.second.root,entry.second.registry).ready;Require(entry.second.ready,"remuxCache","Old cache still has readers or lacks ownership proof","cache-recovery-required");}
        const auto key=root.lexically_normal();auto found=roots.find(key);
        if(found==roots.end()){
            Require(roots.size()<16,"remuxCache","Cache root limit reached","cache-root-limit");Fence fence;fence.registry=registry.lexically_normal();fence.root=std::make_unique<stream_journal_detail::Root>(key,true);
            found=roots.emplace(key,std::move(fence)).first;{lease_detail::Directory receipts(registry,true);found->second.registryIdentity=resource_detail::Inspect(receipts.handle,true);}found->second.ready=RecoverStreamCachePinned(*found->second.root,registry).ready;
        }
        Require(found->second.registry==registry.lexically_normal(),"remuxCache","Cache registry changed","cache-registry-replaced");
        {lease_detail::Directory receipts(registry,true);Require(stream_journal_detail::same(found->second.registryIdentity,resource_detail::Inspect(receipts.handle,true)),"remuxCache","Cache registry identity changed","cache-registry-replaced");}
        Require(found->second.ready,"remuxCache","Old cache still has readers or lacks ownership proof","cache-recovery-required");
    }
    size_t collectLocked(){
        size_t removed=0;
        for(auto i=stores.begin();i!=stores.end();){
            if((*i)->status().retiring && (*i)->retire()){i=stores.erase(i);++removed;}else ++i;
        }
        return removed;
    }
public:
    explicit StreamCacheOwner(std::shared_ptr<StreamCacheBudget> shared,size_t sessions=16):budget(std::move(shared)),limit(sessions){Require(bool(budget) && limit>0 && limit<=16,"remuxCache","Invalid cache owner bounds","invalid_request");}
    std::shared_ptr<StreamOutputStore> create(const std::filesystem::path& root,const std::filesystem::path& registry,const std::string& session,uint64_t generation,uint64_t bytes=64*1024*1024){
        std::lock_guard<std::mutex> lock(mutex);collectLocked();Require(stores.size()<limit,"remuxCache","Retire old output stores first","cache-session-limit");
        admit(root,registry);
        try{auto store=std::make_shared<StreamOutputStore>(root,registry,session,generation,budget,bytes);stores.push_back(store);return store;}catch(...){roots.at(root.lexically_normal()).ready=false;throw;}
    }
    bool retire(const std::shared_ptr<StreamOutputStore>& store){
        std::lock_guard<std::mutex> lock(mutex);const auto found=std::find(stores.begin(),stores.end(),store);
        if(found==stores.end()){Require(store->status().removed,"remuxCache","Foreign output store","cache-owner-mismatch");return true;}
        const auto removed=store->retire();if(removed)stores.erase(found);collectLocked();return store->status().removed;
    }
    size_t collect(){std::lock_guard<std::mutex> lock(mutex);return collectLocked();}
    size_t count()const{std::lock_guard<std::mutex> lock(mutex);return stores.size();}
    // Background service retirement only. Unknown/orphan roots remain fenced
    // until recovery proves them clear; zero allocated bytes is not that proof.
    bool settle(){
        std::lock_guard<std::mutex> lock(mutex);collectLocked();
        for(auto& entry:roots)if(!entry.second.ready)
            entry.second.ready=RecoverStreamCachePinned(*entry.second.root,entry.second.registry).ready;
        return stores.empty() && std::all_of(roots.begin(),roots.end(),[](const auto& item){return item.second.ready;});
    }
};

}}
