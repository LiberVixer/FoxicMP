// FoxicMP progressive local playback. GPL-3.0-or-later.
#include "stdafx.h"
#include "HarvesterSession.h"
#include "../../shared/progressivereaderlease.h"
#include "../../shared/progressiveretirement.h"
#include "../../shared/progressivefmp4bridge.h"
#include <map>
#include <aclapi.h>
#include "ExtLib/rapidjson/include/rapidjson/writer.h"
#include "ExtLib/rapidjson/include/rapidjson/stringbuffer.h"

namespace {
std::wstring Wide(const std::string& s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);
    if (!n) throw std::runtime_error("Invalid UTF-8 path");
    std::wstring out(n,L'\0'); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),out.data(),n); return out;
}
std::wstring LocalPath(const std::wstring& path, bool inspect=true) {
    if (path.empty() || path.size()>240 || path.find(L"://")!=std::wstring::npos) throw std::runtime_error("Invalid local session path");
    WCHAR full[MAX_PATH];
    DWORD n=GetFullPathNameW(path.c_str(),MAX_PATH,full,nullptr);
    if (!n || n>=MAX_PATH || full[1]!=L':' || full[2]!=L'\\' || GetDriveTypeW(std::wstring(full,3).c_str())!=DRIVE_FIXED) throw std::runtime_error("Session must use a local fixed drive");
    std::wstring result(full);
    if (result.find(L':',2)!=std::wstring::npos) throw std::runtime_error("Alternate data streams are not allowed");
    for (size_t i=3;inspect && i<=result.size();++i) {
        if (i!=result.size() && result[i]!=L'\\') continue;
        DWORD attrs=GetFileAttributesW(result.substr(0,i).c_str());
        if (attrs==INVALID_FILE_ATTRIBUTES || (attrs&FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("Missing file or reparse point in session path");
    }
    return result;
}
HANDLE OpenLocal(const std::wstring& path,bool asynchronous) {
    const auto full=LocalPath(path);
    HANDLE h=CreateFileW(full.c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|(asynchronous?FILE_FLAG_OVERLAPPED:0),nullptr);
    if (h==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open session file with read/write sharing");
    BY_HANDLE_FILE_INFORMATION info{};
    WCHAR final[MAX_PATH+4];
    DWORD n=GetFinalPathNameByHandleW(h,final,MAX_PATH+4,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if (!GetFileInformationByHandle(h,&info) || (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) ||
        !n || n>=MAX_PATH+4 || _wcsicmp(final,(L"\\\\?\\"+full).c_str())) {
        CloseHandle(h); throw std::runtime_error("Session path changed or is not a regular local file");
    }
    return h;
}
bool HeadIndexReady(const std::wstring& path,uint64_t prefix,uint64_t total) {
    HANDLE file=OpenLocal(path,false);
    auto check=[&]() {
        uint64_t position=0;bool ftyp=false;
        while(harvester::RangeReady(position,8,prefix)) {
            LARGE_INTEGER offset{};offset.QuadPart=position;BYTE header[16]={};DWORD read=0;
            if(!SetFilePointerEx(file,offset,nullptr,FILE_BEGIN) || !ReadFile(file,header,8,&read,nullptr) || read!=8)return false;
            uint64_t size=0;for(int i=0;i<4;++i)size=(size<<8)|header[i];uint64_t headerSize=8;
            if(size==1) {
                if(!harvester::RangeReady(position,16,prefix) || !ReadFile(file,header+8,8,&read,nullptr) || read!=8)return false;
                size=0;for(int i=8;i<16;++i)size=(size<<8)|header[i];headerSize=16;
            }
            if(size<headerSize || (total && !harvester::RangeReady(position,size,total)))return false;
            if(!memcmp(header+4,"ftyp",4))ftyp=position==0 && size<=4096;
            if(!memcmp(header+4,"mdat",4) || !memcmp(header+4,"moof",4))return false;
            if(!memcmp(header+4,"moov",4))return ftyp && size<=64*1024*1024 && harvester::RangeReady(position,size,prefix);
            if(!harvester::RangeReady(position,size,prefix))return false;
            position+=size;
        }
        return false;
    };
    bool ready=check();CloseHandle(file);return ready;
}
bool SameUser(DWORD pid) {
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid), theirs=nullptr, ours=nullptr;
    bool ok=false;
    if (process && OpenProcessToken(process,TOKEN_QUERY,&theirs) && OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&ours)) {
        DWORD a=0,b=0; GetTokenInformation(theirs,TokenUser,nullptr,0,&a); GetTokenInformation(ours,TokenUser,nullptr,0,&b);
        std::vector<BYTE> ta(a),tb(b);
        if (a && b && GetTokenInformation(theirs,TokenUser,ta.data(),a,&a) && GetTokenInformation(ours,TokenUser,tb.data(),b,&b))
            ok=EqualSid(((TOKEN_USER*)ta.data())->User.Sid,((TOKEN_USER*)tb.data())->User.Sid)!=FALSE;
    }
    if(ours)CloseHandle(ours); if(theirs)CloseHandle(theirs); if(process)CloseHandle(process); return ok;
}
bool PipeWrite(HANDLE pipe,const std::string& data) {
    OVERLAPPED ov{}; ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr); DWORD written=0;
    bool ok=WriteFile(pipe,data.data(),(DWORD)data.size(),&written,&ov)!=FALSE;
    if (!ok && GetLastError()==ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent,250)==WAIT_OBJECT_0) ok=GetOverlappedResult(pipe,&ov,&written,FALSE)!=FALSE;
        else { CancelIoEx(pipe,&ov); GetOverlappedResult(pipe,&ov,&written,TRUE); }
    }
    CloseHandle(ov.hEvent); return ok && written==data.size();
}
}
CHarvesterSession::CHarvesterSession() {
    m_cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr); m_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
}
CHarvesterSession::~CHarvesterSession() {
    Cancel(); SetEvent(m_stop); if(m_thread.joinable())m_thread.join();
    for(auto& file:m_files) { if(file!=INVALID_HANDLE_VALUE)CloseHandle(file); file=INVALID_HANDLE_VALUE; }
    m_v2Bridge.reset();
    if(m_readerLease)m_readerLease->filesClosed();
    // Context survives until the last reader and graph have released their references.
    if(!m_published && m_pipe==INVALID_HANDLE_VALUE)ConnectPipe();
    if(m_pipe!=INVALID_HANDLE_VALUE)Send("released");
    DisconnectPipe(); if(m_cancel)CloseHandle(m_cancel); if(m_stop)CloseHandle(m_stop);
    m_released->store(true);
}
std::shared_ptr<CHarvesterSession> CHarvesterSession::Open(const std::wstring& path,CStringW& error) {
    std::shared_ptr<CHarvesterSession> p;
    try {
        p=foxicmp::progressive::SourceRetirement::create<CHarvesterSession>([]{return new CHarvesterSession;});
        p->m_manifest=LocalPath(path); p->m_root=p->m_manifest.substr(0,p->m_manifest.find_last_of(L'\\')+1);
        p->ReadManifest(p->m_state);
        if(p->m_state.state==harvester::State::Cancelled || p->m_state.state==harvester::State::Failed) throw std::runtime_error("Session is cancelled or failed");
        for(int i=0;i<2;++i) {
            const auto& t=p->m_state.tracks[i];
            if(!t.initializationReady || t.state==harvester::State::Cancelled || t.state==harvester::State::Failed) throw std::runtime_error("Track is not initialized");
            p->m_files[i]=OpenLocal(p->Path(i),true);
            LARGE_INTEGER size{};
            if(!GetFileSizeEx(p->m_files[i],&size) || t.available>uint64_t(size.QuadPart)) throw std::runtime_error("Confirmed prefix exceeds physical data");
            if(!HeadIndexReady(p->Path(i),t.available,t.total)) throw std::runtime_error("MP4 must have a complete head index before media data");
        }
        BY_HANDLE_FILE_INFORMATION video{},audio{};
        if(!GetFileInformationByHandle(p->m_files[0],&video) || !GetFileInformationByHandle(p->m_files[1],&audio) ||
           (video.dwVolumeSerialNumber==audio.dwVolumeSerialNumber && video.nFileIndexHigh==audio.nFileIndexHigh && video.nFileIndexLow==audio.nFileIndexLow))
            throw std::runtime_error("Track files must be distinct regular files");
        p->RefreshFragments();
        p->m_lastUpdate=GetTickCount64();
        if(!p->ConnectPipe()) throw std::runtime_error("Harvester session pipe is unavailable or belongs to another user");
        if(!p->Send("attached")) throw std::runtime_error("Cannot attach to Harvester");
        p->m_thread=std::thread([context=p.get()]{context->Watch();}); return p;
    } catch(const std::exception& e) { error=Wide(e.what()).c_str(); return nullptr; }
}
namespace {
std::wstring PublishedDirectory(const rapidjson::Value& input, const std::wstring* bound=nullptr) {
    if(!input.HasMember("sessionDirectory"))return bound?*bound:std::wstring();
    const auto raw=Wide(harvester::String(input,"sessionDirectory"));
    if(raw.size()<3 || raw[1]!=L':' || (raw[2]!=L'\\' && raw[2]!=L'/'))
        throw std::runtime_error("sessionDirectory must be an absolute local directory");
    auto root=LocalPath(raw,false);
    while(root.size()>3 && root.back()==L'\\')root.pop_back();
    if(bound && _wcsicmp(root.c_str(),bound->c_str()))
        throw std::runtime_error("sessionDirectory cannot change within a session");
    return bound?*bound:root;
}
std::wstring PublishedPath(const std::wstring& root, const rapidjson::Value& track, bool inspect) {
    auto raw=Wide(harvester::String(track,"path"));
    const bool absolute=raw.size()>2 && raw[1]==L':' && (raw[2]==L'\\' || raw[2]==L'/');
    if(!absolute) {
        if(root.empty())
            throw std::runtime_error("Relative track paths require sessionDirectory");
        if(raw.empty() || raw.find_first_of(L"\\/:<>|?*\"")!=std::wstring::npos || raw==L"." || raw==L"..")
            throw std::runtime_error("Track name must be a plain local filename");
        raw=root+L"\\"+raw;
    }
    return LocalPath(raw,inspect);
}
void NormalizePublishedPaths(rapidjson::Document& input, std::array<std::wstring,2>& paths,
                             const std::wstring& root, bool inspect) {
    if(!input.HasMember("tracks") || !input["tracks"].IsObject()) throw std::runtime_error("Tracks missing");
    for(int i=0;i<2;++i) {
        const char* name=i?"audio":"video";auto& tracks=input["tracks"];
        if(!tracks.HasMember(name) || !tracks[name].IsObject()) throw std::runtime_error("Track missing");
        auto& track=tracks[name];paths[i]=PublishedPath(root,track,inspect);
        track["path"].SetString(i?"audio.part":"video.part",input.GetAllocator());
    }
    if(input.HasMember("sessionDirectory")) input.RemoveMember("sessionDirectory");
}
harvester::Snapshot PublishedSnapshot(const std::string& json, std::array<std::wstring, 2>& paths,
                                      std::wstring& root) {
    rapidjson::Document input;
    input.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(json.data(),json.size());
    if(input.HasParseError() || !input.IsObject())throw std::runtime_error("Invalid session JSON");
    harvester::UniqueKeys(input);
    root=PublishedDirectory(input);
    NormalizePublishedPaths(input,paths,root,true);
    rapidjson::StringBuffer buffer;rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);input.Accept(writer);
    return harvester::Parse(std::string(buffer.GetString(),buffer.GetSize()));
}
void RequireOwned(HANDLE file) {
    PSID owner=nullptr;PSECURITY_DESCRIPTOR security=nullptr;HANDLE token=nullptr;bool ok=false;
    if(GetSecurityInfo(file,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION,&owner,nullptr,nullptr,nullptr,&security)==ERROR_SUCCESS && OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) {
        DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<BYTE> data(size);
        if(size && GetTokenInformation(token,TokenUser,data.data(),size,&size))ok=EqualSid(owner,reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid)!=FALSE;
        size=0;GetTokenInformation(token,TokenOwner,nullptr,0,&size);data.resize(size);
        if(size && GetTokenInformation(token,TokenOwner,data.data(),size,&size))ok=ok || EqualSid(owner,reinterpret_cast<TOKEN_OWNER*>(data.data())->Owner)!=FALSE;
    }
    if(token)CloseHandle(token);if(security)LocalFree(security);
    if(!ok)throw std::runtime_error("Track must belong to the current user");
}
}
bool CHarvesterSession::AllowsAv1Fragments() const {return m_v2Bridge && m_v2Bridge->videoCodec()=="av1";}
bool CHarvesterSession::AllowsVp9Fragments() const {return m_v2Bridge && m_v2Bridge->videoCodec()=="vp9";}
bool CHarvesterSession::AllowsHevcFragments() const {return m_v2Bridge && m_v2Bridge->videoCodec()=="hevc";}
std::string CHarvesterSession::RegisteredReaderId() const {return m_readerLease?m_readerLease->id():std::string();}
std::string CHarvesterSession::PlaybackDetails() const {
    if(!m_v2Bridge)return "{\"schemaVersion\":1}";
    std::lock_guard<std::mutex> lock(m_mutex);rapidjson::Document d;d.Parse(m_v2Bridge->details().c_str());
    auto& a=d.GetAllocator();d["revision"].SetString(std::to_string(m_state.revision).c_str(),a);
    return foxicmp::progressive::lease_detail::Encode(d);
}
std::shared_ptr<CHarvesterSession> CHarvesterSession::OpenPublished(const std::string& json,CStringW& error,const std::wstring& readerRegistry,std::string* reasonCode,bool trackGroup) {
    std::shared_ptr<CHarvesterSession> context;
    try {
        context=trackGroup?foxicmp::progressive::SourceRetirement::create<CHarvesterSession>([]{return new CHarvesterSession;}):std::make_shared<CHarvesterSession>();
        context->m_published=true;
        rapidjson::Document descriptor;descriptor.Parse(json.c_str());
        if(descriptor.IsObject() && descriptor.HasMember("schemaVersion") && descriptor["schemaVersion"].IsUint() && descriptor["schemaVersion"].GetUint()==2) {
            context->m_publishedRoot=PublishedDirectory(descriptor);descriptor.RemoveMember("sessionDirectory");
            rapidjson::StringBuffer b;rapidjson::Writer<rapidjson::StringBuffer>w(b);descriptor.Accept(w);
            context->m_v2Bridge=std::make_shared<foxicmp::progressive::Fmp4V2Bridge>(std::filesystem::path(context->m_publishedRoot),std::filesystem::path(readerRegistry),std::string(b.GetString(),b.GetSize()));
            context->m_state=context->m_v2Bridge->snapshot();
            for(int i=0;i<2;++i)context->m_publishedPaths[i]=context->m_publishedRoot+L"\\"+Wide(context->m_state.tracks[i].path);
        }else context->m_state=PublishedSnapshot(json,context->m_publishedPaths,context->m_publishedRoot);
        if(context->m_state.state==harvester::State::Cancelled || context->m_state.state==harvester::State::Failed)
            throw std::runtime_error("Cannot start a terminal failed session");
        for(int i=0;i<2;++i) {
            const auto& track=context->m_state.tracks[i];
            if(!track.initializationReady)throw std::runtime_error("Initialization is not ready");
            if(context->m_v2Bridge)continue;
            context->m_files[i]=OpenLocal(context->m_publishedPaths[i],true);
            RequireOwned(context->m_files[i]);
            LARGE_INTEGER size{};
            if(!GetFileSizeEx(context->m_files[i],&size) || size.QuadPart<0 || track.available>uint64_t(size.QuadPart))throw std::runtime_error("Confirmed bytes are missing");
            // Fragmented MP4 has no complete moov index until the download ends;
            // RefreshFragments validates its available initialization/moof data.
            if(!track.fragmented && !HeadIndexReady(context->m_publishedPaths[i],track.available,track.total))
                throw std::runtime_error("Head MP4 index is not ready");
        }
        BY_HANDLE_FILE_INFORMATION a{},b{};
        if(!context->m_v2Bridge && (!GetFileInformationByHandle(context->m_files[0],&a) || !GetFileInformationByHandle(context->m_files[1],&b) ||
           (a.dwVolumeSerialNumber==b.dwVolumeSerialNumber && a.nFileIndexHigh==b.nFileIndexHigh && a.nFileIndexLow==b.nFileIndexLow))
            )throw std::runtime_error("Tracks must be distinct files");
        context->RefreshFragments();context->PublishedHeartbeat();
        if(context->m_v2Bridge && trackGroup) {
            const auto bindings=context->m_v2Bridge->bindings();
            context->m_readerLease=foxicmp::progressive::ReaderLease::Create(std::filesystem::path(readerRegistry),context->m_state.sessionId,context->m_state.generation,
                {{"video",bindings[0].device,bindings[0].fileId},{"audio",bindings[1].device,bindings[1].fileId}});
        }else if(!context->m_v2Bridge && !readerRegistry.empty())context->m_readerLease=foxicmp::progressive::ReaderLease::Create(
            std::filesystem::path(readerRegistry),context->m_state.sessionId,context->m_state.generation,
            {{"video",a.dwVolumeSerialNumber,(uint64_t(a.nFileIndexHigh)<<32)|a.nFileIndexLow},
             {"audio",b.dwVolumeSerialNumber,(uint64_t(b.nFileIndexHigh)<<32)|b.nFileIndexLow}});
        context->m_thread=std::thread([raw=context.get()] {
            while(WaitForSingleObject(raw->m_stop,100)==WAIT_TIMEOUT && WaitForSingleObject(raw->m_cancel,0)==WAIT_TIMEOUT) {
                if(raw->m_v2Bridge) {
                    try{raw->m_v2Bridge->verify();}catch(const std::exception& e){if(WaitForSingleObject(raw->m_cancel,0)!=WAIT_OBJECT_0)raw->Fail(e.what());return;}
                    continue;
                }
                const auto state=raw->Snapshot();
                for(int i=0;i<2;++i) {
                    LARGE_INTEGER size{};
                    if(!GetFileSizeEx(raw->m_files[i],&size) || size.QuadPart<0 || uint64_t(size.QuadPart)<state.tracks[i].available) {
                        raw->Fail("Confirmed track bytes were truncated");return;
                    }
                }
            }
        });
        return context;
    } catch(const foxicmp::progressive::RetirementBusy& e) {if(reasonCode)*reasonCode="source_limit";error=Wide(e.what()).c_str();return nullptr;}
    catch(const foxicmp::progressive::Error& e) {if(reasonCode)*reasonCode=e.code;error=Wide(e.what()).c_str();return nullptr;}
    catch(const std::exception& e) {if(reasonCode)*reasonCode="source_not_ready";error=Wide(e.what()).c_str();return nullptr;}
}
bool CHarvesterSession::Publish(const std::string& json,CStringW& error) {
    std::lock_guard<std::mutex> publication(m_publishMutex);
    try {
        harvester::Snapshot previous;
        {std::lock_guard<std::mutex> lock(m_mutex);
         if(!m_published || !m_error.empty() || WaitForSingleObject(m_cancel,0)==WAIT_OBJECT_0)throw std::runtime_error("Session is closed");
         previous=m_state;}
        harvester::Snapshot next;
        rapidjson::Document input;input.Parse(json.data(),json.size());
        if(input.HasParseError() || !input.IsObject())throw std::runtime_error("Invalid snapshot JSON");
        harvester::UniqueKeys(input);
        if(m_v2Bridge) {
            PublishedDirectory(input,&m_publishedRoot);input.RemoveMember("sessionDirectory");
            m_v2Bridge->publish(foxicmp::progressive::lease_detail::Encode(input));next=m_v2Bridge->snapshot();
        }else {
            std::array<std::wstring,2> paths;const auto root=PublishedDirectory(input,&m_publishedRoot);
            NormalizePublishedPaths(input,paths,root,false);
            next=harvester::Parse(foxicmp::progressive::lease_detail::Encode(input));
            if(paths!=m_publishedPaths)throw std::runtime_error("Track paths changed");
            if(!harvester::Advance(previous,next))throw std::runtime_error("Stale revision");
            for(int i=0;i<2;++i){LARGE_INTEGER size{};if(!GetFileSizeEx(m_files[i],&size) || size.QuadPart<0 || uint64_t(size.QuadPart)<next.tracks[i].available)throw std::runtime_error("Confirmed bytes are missing");}
        }
        Fragments fragments;
        try{fragments=BuildFragments(next);}catch(const std::exception& e){if(WaitForSingleObject(m_cancel,0)!=WAIT_OBJECT_0)Fail(e.what());throw;}
        Fragments retired;
        {std::lock_guard<std::mutex> lock(m_mutex);
         if(!m_error.empty() || WaitForSingleObject(m_cancel,0)==WAIT_OBJECT_0)throw std::runtime_error("Session is closed");
         m_state=next;retired=std::move(m_fragments);m_fragments=std::move(fragments);
         for(int i=0;i<2;++i)if(next.tracks[i].fragmented)m_duration[i]=next.duration;
        }
        PublishedHeartbeat();
        if(next.state==harvester::State::Cancelled || next.state==harvester::State::Failed)Fail("Downloader cancelled or failed");
        return true;
    }catch(const std::exception& e){error=Wide(e.what()).c_str();return false;}
}

bool CHarvesterSession::ReadManifest(harvester::Snapshot& state) {
    HANDLE h=INVALID_HANDLE_VALUE;const auto deadline=GetTickCount64()+1000;
    for(;;) {
        try {h=OpenLocal(m_manifest,false);break;}
        catch(const std::exception&) {
            const auto e=GetLastError();
            if((e!=ERROR_ACCESS_DENIED && e!=ERROR_SHARING_VIOLATION && e!=ERROR_LOCK_VIOLATION && e!=ERROR_FILE_NOT_FOUND) || GetTickCount64()>=deadline)throw;
            if(WaitForSingleObject(m_cancel,10)!=WAIT_TIMEOUT)throw;
        }
    }
    LARGE_INTEGER size{}; DWORD read=0;
    if(!GetFileSizeEx(h,&size) || size.QuadPart<=0 || size.QuadPart>harvester::MaxManifestBytes) {CloseHandle(h);throw std::runtime_error("Invalid manifest size");}
    std::string text((size_t)size.QuadPart,'\0');
    BOOL ok=ReadFile(h,text.data(),(DWORD)text.size(),&read,nullptr); CloseHandle(h);
    if(!ok || read!=text.size())throw std::runtime_error("Cannot read manifest");
    state=harvester::Parse(text); return true;
}
std::wstring CHarvesterSession::Path(int track) const { std::lock_guard<std::mutex> lock(m_mutex); return m_published ? m_publishedPaths[track] : m_root+Wide(m_state.tracks[track].path); }
harvester::Snapshot CHarvesterSession::Snapshot() const {std::lock_guard<std::mutex> lock(m_mutex);return m_state;}
std::string CHarvesterSession::Error() const {std::lock_guard<std::mutex> lock(m_mutex);return m_error;}
void CHarvesterSession::Fail(const char* reason) { {std::lock_guard<std::mutex> lock(m_mutex); if(m_error.empty())m_error=reason; m_status="error";} Cancel(); }
void CHarvesterSession::Cancel() {SetEvent(m_cancel);if(m_v2Bridge)m_v2Bridge->cancel();}
void CHarvesterSession::Notify(const char* status) {std::lock_guard<std::mutex> lock(m_mutex);m_status=status;}
bool CHarvesterSession::ConnectPipe() {
    auto s=Snapshot(); auto name=L"\\\\.\\pipe\\FoxicMP.Harvester."+Wide(s.sessionId);
    m_pipe=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
    if(m_pipe==INVALID_HANDLE_VALUE)return false;
    ULONG pid=0;
    if(!GetNamedPipeServerProcessId(m_pipe,&pid) || !SameUser(pid)){DisconnectPipe();return false;}
    m_connected=true; return true;
}
void CHarvesterSession::DisconnectPipe() {m_connected=false;if(m_pipe!=INVALID_HANDLE_VALUE)CloseHandle(m_pipe);m_pipe=INVALID_HANDLE_VALUE;}
bool CHarvesterSession::Send(const char* type) {
    const auto s=Snapshot(); rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
    w.StartObject(); w.Key("schemaVersion");w.Uint(1); w.Key("sessionId");w.String(s.sessionId.c_str());
    w.Key("generation");w.Uint64(s.generation);w.Key("revision");w.Uint64(s.revision);
    w.Key("type");w.String(type);w.Key("pid");w.Uint(GetCurrentProcessId());
    w.Key("position100ns");w.Int64(m_position.load());w.Key("readyEnd100ns");w.Int64(ReadyEnd());w.Key("completionWaiters");w.Uint(m_completionWaiters.load());w.EndObject();
    return PipeWrite(m_pipe,std::string(buffer.GetString(),buffer.GetSize())+"\n");
}
void CHarvesterSession::Watch() {
    std::string sent="attached", incoming;
    ULONGLONG lastSent=0;
    while(WaitForSingleObject(m_stop,100)!=WAIT_OBJECT_0 && WaitForSingleObject(m_cancel,0)!=WAIT_OBJECT_0) {
        try {
            harvester::Snapshot next;ReadManifest(next);const auto previous=Snapshot();
            const bool advanced=harvester::Advance(previous,next);const auto& checked=advanced?next:previous;
            // Native metadata queries and fragment parsing must not hold the
            // mutex used by UI Snapshot/ReadyEnd/Notify. Check unchanged files too.
            for(int i=0;i<2;++i) {LARGE_INTEGER size{};if(!GetFileSizeEx(m_files[i],&size) || checked.tracks[i].available>uint64_t(size.QuadPart))throw std::runtime_error("Confirmed data disappeared");}
            if(advanced) {
                auto fragments=BuildFragments(next);Fragments retired;
                {std::lock_guard<std::mutex> lock(m_mutex);
                 if(WaitForSingleObject(m_cancel,0)==WAIT_OBJECT_0 || !m_error.empty())break;
                 m_state=next;retired=std::move(m_fragments);m_fragments=std::move(fragments);
                 for(int i=0;i<2;++i)if(next.tracks[i].fragmented)m_duration[i]=next.duration;
                 m_lastUpdate=GetTickCount64();}
            }
            const auto current=Snapshot();
            if(current.state==harvester::State::Cancelled || current.tracks[0].state==harvester::State::Cancelled || current.tracks[1].state==harvester::State::Cancelled)
                Fail("Harvester download cancelled");
            else if(current.state==harvester::State::Failed || current.tracks[0].state==harvester::State::Failed || current.tracks[1].state==harvester::State::Failed)
                Fail("Harvester download failed; source tracks are retained for retry");
        }catch(const std::exception& e){if(WaitForSingleObject(m_cancel,0)!=WAIT_OBJECT_0)Fail(e.what());}
        if(m_pipe==INVALID_HANDLE_VALUE && ConnectPipe()) {sent.clear();Send("attached");}
        std::string status;{std::lock_guard<std::mutex> lock(m_mutex);status=m_status;}
        if(m_pipe!=INVALID_HANDLE_VALUE &&  (status!=sent || GetTickCount64()-lastSent>=1000)) {if(Send(status.c_str())) {sent=status;lastSent=GetTickCount64();} else DisconnectPipe();}
        if(m_pipe!=INVALID_HANDLE_VALUE) {
            DWORD count=0;
            if(!PeekNamedPipe(m_pipe,nullptr,0,nullptr,&count,nullptr))DisconnectPipe();
            else if(count>harvester::MaxManifestBytes) {Fail("Oversized pipe message");DisconnectPipe();}
            else if(count) { // Notifications carry no authority; the atomic manifest is read above.
                std::vector<char> bytes(count);OVERLAPPED ov{};ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);DWORD read=0;
                BOOL ok=ReadFile(m_pipe,bytes.data(),count,&read,&ov);
                if(!ok && GetLastError()==ERROR_IO_PENDING) {if(WaitForSingleObject(ov.hEvent,250)==WAIT_OBJECT_0)ok=GetOverlappedResult(m_pipe,&ov,&read,FALSE);else {CancelIoEx(m_pipe,&ov);GetOverlappedResult(m_pipe,&ov,&read,TRUE);}}
                CloseHandle(ov.hEvent);
                if (!ok) {DisconnectPipe();incoming.clear();}
                else {
                    incoming.append(bytes.data(),read);
                    if (incoming.size()>harvester::MaxManifestBytes) {Fail("Oversized pipe frame");DisconnectPipe();incoming.clear();}
                    size_t newline;
                    while ((newline=incoming.find('\n'))!=std::string::npos) {
                        rapidjson::Document notification;
                        auto line=incoming.substr(0,newline);incoming.erase(0,newline+1);
                        notification.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(line.data(),line.size());
                        const auto current=Snapshot();
                        try {
                            if(notification.HasParseError() || !notification.IsObject())throw std::runtime_error("Invalid pipe JSON");
                            harvester::UniqueKeys(notification);
                            auto type=harvester::String(notification,"type");
                            if(harvester::Number(notification,"schemaVersion")!=1 || harvester::String(notification,"sessionId")!=current.sessionId ||
                               harvester::Number(notification,"generation")!=current.generation || (type!="heartbeat" && type!="changed"))throw std::runtime_error("Invalid pipe notification");
                            // Future revisions are hints only; manifest validation remains authoritative.
                            if(harvester::Number(notification,"revision")>=current.revision)m_lastUpdate=GetTickCount64();
                        } catch(const std::exception& e) {Fail(e.what());DisconnectPipe();incoming.clear();break;}
                    }
                }
            }
        }
    }
}
HRESULT CHarvesterSession::WaitRange(int track,uint64_t offset,uint64_t size,HANDLE flush,HANDLE breaker,bool completion) {
    for(;;) {
        if(WaitForSingleObject(m_cancel,0)==WAIT_OBJECT_0 || (flush && WaitForSingleObject(flush,0)==WAIT_OBJECT_0) || (breaker && WaitForSingleObject(breaker,0)==WAIT_OBJECT_0))return E_ABORT;
        auto s=Snapshot(); const auto& t=s.tracks[track];
        if(t.total && !harvester::RangeReady(offset,size,t.total))return E_FAIL;
        if(completion ? harvester::EndReady(t) : harvester::RangeReady(offset,size,t.available))return S_OK;
        if(t.state==harvester::State::Complete)return S_FALSE;
        HANDLE events[3]={m_cancel,flush,breaker}; DWORD n=1;if(flush)events[n++]=flush;if(breaker)events[n++]=breaker;
        if(WaitForMultipleObjects(n,events,FALSE,100)!=WAIT_TIMEOUT)return E_ABORT;
    }
}
bool CHarvesterSession::SetIndex(int track,std::vector<harvester::Sample> samples,int64_t duration) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if(samples.empty() || samples.size()>2000000 || duration<=0 || (track==0 && !samples[0].sync))return false;
    for(const auto& s:samples)if(!harvester::RangeReady(s.offset,s.size,m_state.tracks[track].total))return false;
    m_samples[track]=std::move(samples);m_duration[track]=duration;m_indexPosition[track]=0;m_indexEnd[track]=0;return true;
}
int64_t CHarvesterSession::ReadyEnd() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    int64_t ready[2] = {};
    for(int track=0;track<2;++track) {
        const auto& samples=Samples(track);auto& position=m_indexPosition[track];auto& end=m_indexEnd[track];
        while(position<samples.size() && harvester::RangeReady(samples[position].offset,samples[position].size,m_state.tracks[track].available)) {
            end=std::max(end,samples[position].end);++position;
        }
        ready[track]=position<samples.size() ? std::max<int64_t>(0,std::min(end,std::min(samples[position].start,samples[position].decode))) : std::max<int64_t>(0,std::min(end,m_duration[track]));
        if(m_state.tracks[track].fragmented && !harvester::EndReady(m_state.tracks[track]) && !samples.empty())
            ready[track]=std::min(ready[track],harvester::AddTime(samples.back().decode,samples.back().end-samples.back().start));
    }
    return std::min(ready[0],ready[1]);
}
bool CHarvesterSession::Complete() const {auto s=Snapshot();return s.tracks[0].state==harvester::State::Complete && s.tracks[1].state==harvester::State::Complete;}

const std::vector<harvester::Sample>& CHarvesterSession::Samples(int track) const {
    return m_state.tracks[track].fragmented?m_fragments[track]->Samples():m_samples[track];
}
CHarvesterSession::Fragments CHarvesterSession::BuildFragments(const harvester::Snapshot& snapshot) {
    Fragments result;
    {std::lock_guard<std::mutex> lock(m_mutex);result=m_fragments;}
    for(int track=0;track<2;++track)if(snapshot.tracks[track].fragmented) {
        // Clone and parse outside the metadata mutex used by UI readiness/seek.
        auto next=std::make_shared<harvester::FragmentIndex>(*result[track]);
        const auto& state=snapshot.tracks[track];
        auto diskRead=[&](uint64_t offset,void* data,size_t size) {
            if(WaitForSingleObject(m_cancel,0)==WAIT_OBJECT_0)return false;
            if(!size)return true;
            if(m_v2Bridge)return m_v2Bridge->readIndex(track,offset,data,size);
            OVERLAPPED ov{};ov.Offset=DWORD(offset);ov.OffsetHigh=DWORD(offset>>32);ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            DWORD n=0;BOOL ok=ReadFile(m_files[track],data,DWORD(size),&n,&ov);
            if(!ok && GetLastError()==ERROR_IO_PENDING) {
                HANDLE events[]={ov.hEvent,m_cancel};
                if(WaitForMultipleObjects(2,events,FALSE,2000)==WAIT_OBJECT_0)ok=GetOverlappedResult(m_files[track],&ov,&n,FALSE);
                else {CancelIoEx(m_files[track],&ov);GetOverlappedResult(m_files[track],&ov,&n,TRUE);}
            }
            CloseHandle(ov.hEvent);return ok && n==size;
        };
        next->Update(diskRead,state.available,track,harvester::EndReady(state),snapshot.timestampOffset,m_v2Bridge && m_v2Bridge->videoCodec()=="hevc",m_v2Bridge && m_v2Bridge->videoCodec()=="av1",m_v2Bridge && m_v2Bridge->videoCodec()=="vp9");
        result[track]=std::move(next);
    }
    return result;
}
void CHarvesterSession::RefreshFragments() {
    const auto snapshot=Snapshot();auto fragments=BuildFragments(snapshot);Fragments retired;
    {std::lock_guard<std::mutex> lock(m_mutex);retired=std::move(m_fragments);m_fragments=std::move(fragments);
     for(int i=0;i<2;++i)if(snapshot.tracks[i].fragmented)m_duration[i]=snapshot.duration;}
}

HRESULT CHarvesterSession::ReadV2(int track,uint64_t offset,size_t size,void* data,HANDLE flush,HANDLE breaker) {
    auto* bytes=static_cast<char*>(data);
    while(size) {
        if(WaitForSingleObject(m_cancel,0)==WAIT_OBJECT_0 || (flush && WaitForSingleObject(flush,0)==WAIT_OBJECT_0) || (breaker && WaitForSingleObject(breaker,0)==WAIT_OBJECT_0))return E_ABORT;
        const auto count=m_v2Bridge->readPayload(track,offset,bytes,size);
        if(count<=0){if(WaitForSingleObject(m_cancel,0)==WAIT_OBJECT_0)return E_ABORT;Fail("Confirmed v2 resource I/O failed");return E_FAIL;}
        bytes+=count;offset+=uint64_t(count);size-=size_t(count);
    }
    return S_OK;
}

int CHarvesterSession::SampleAt(int track,size_t index,harvester::Sample& sample) const {
    std::lock_guard<std::mutex> lock(m_mutex);const auto& samples=Samples(track);
    if(index<samples.size()){sample=samples[index];return 1;}
    const auto& t=m_state.tracks[track];
    return harvester::EndReady(t) && m_fragments[track]->Position()==t.available ? 2 : 0;
}
size_t CHarvesterSession::SeekSample(int track,int64_t position) const {
    std::lock_guard<std::mutex> lock(m_mutex);const auto& samples=Samples(track);size_t found=0;
    if(track==0){for(size_t i=0;i<samples.size();++i)if(samples[i].sync && samples[i].start<=position)found=i;}
    else {while(found<samples.size() && samples[found].end<=position)++found;}
    return found;
}
