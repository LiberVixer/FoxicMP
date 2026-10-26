#include "stdafx.h"
#include "ClassicProgressiveApi.h"
#include "MainFrm.h"
#include "mplayerc.h"
#include "ExtLib/rapidjson/include/rapidjson/writer.h"
#include "ExtLib/rapidjson/include/rapidjson/stringbuffer.h"
#include <sddl.h>
#include <initializer_list>
#include "../../../shared/progressivesession.h"
#include "../../../shared/progressivereaderlease.h"
#include "../../../shared/progressiveretirement.h"
#include "../../../shared/progressiveresourceprobe.h"
#include "../../../shared/progressivefmp4bridge.h"

namespace {
using rapidjson::Document;
using rapidjson::Value;
std::string Utf8(const std::wstring& text) {
    int size=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(size,'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),out.data(),size,nullptr,nullptr);return out;
}
CStringW Wide(const std::string& text) {
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
    CStringW out;auto* buffer=out.GetBuffer(count);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),buffer,count);out.ReleaseBuffer(count);return out;
}
std::string Encode(const Value& value) {rapidjson::StringBuffer b;rapidjson::Writer<rapidjson::StringBuffer>w(b);value.Accept(w);return {b.GetString(),b.GetSize()};}
std::string Failure(const char* code,const std::string& text) {
    Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("version",1,a);d.AddMember("ok",false,a);
    d.AddMember("code",Value(code,a),a);d.AddMember("error",Value(text.c_str(),a),a);return Encode(d);
}
bool Keys(const Value& d,std::initializer_list<const char*> required,std::initializer_list<const char*> optional={}) {
    for(auto key:required)if(!d.HasMember(key))return false;
    for(auto it=d.MemberBegin();it!=d.MemberEnd();++it) {
        bool found=false;for(auto key:required)found|=!strcmp(key,it->name.GetString());for(auto key:optional)found|=!strcmp(key,it->name.GetString());if(!found)return false;
    }return true;
}
std::wstring UserSid() {
    HANDLE token=nullptr;DWORD count=0;std::wstring result;
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) {
        GetTokenInformation(token,TokenUser,nullptr,0,&count);std::vector<BYTE> bytes(count);LPWSTR text=nullptr;
        if(count && GetTokenInformation(token,TokenUser,bytes.data(),count,&count) && ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&text)) {result=text;LocalFree(text);}
        CloseHandle(token);
    }return result;
}
bool Transfer(HANDLE pipe,HANDLE stop,void* data,DWORD size,DWORD& count,bool write,DWORD timeout=5000) {
    OVERLAPPED ov{};ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    BOOL ok=write?WriteFile(pipe,data,size,&count,&ov):ReadFile(pipe,data,size,&count,&ov);
    if(!ok && GetLastError()==ERROR_IO_PENDING) {
        HANDLE events[]={stop,ov.hEvent};
        if(WaitForMultipleObjects(2,events,FALSE,timeout)==WAIT_OBJECT_0+1)ok=GetOverlappedResult(pipe,&ov,&count,FALSE);
        else {CancelIoEx(pipe,&ov);GetOverlappedResult(pipe,&ov,&count,TRUE);}
    }
    CloseHandle(ov.hEvent);return ok!=FALSE;
}
}
std::wstring ClassicProgressiveApi::ProfileDirectory() {
    WCHAR path[MAX_PATH];GetModuleFileNameW(nullptr,path,MAX_PATH);std::wstring result(path);return result.substr(0,result.find_last_of(L'\\'));
}
std::wstring ClassicProgressiveApi::Endpoint() {
    auto identity=ProfileDirectory()+L"|"+UserSid();CharLowerBuffW(identity.data(),DWORD(identity.size()));
    uint64_t hash=14695981039346656037ULL;for(auto c:identity){hash^=uint16_t(c);hash*=1099511628211ULL;}
    WCHAR name[128];swprintf_s(name,L"\\\\.\\pipe\\FoxicMPClassic-%016llx.progressive",hash);return name;
}
std::string ClassicProgressiveApi::Information() {
    Document d;d.Parse(R"({"version":1,"schemaVersion":1,"releaseAcknowledgement":true,"playbackSchemaVersions":[1],"validationSchemaVersions":[2],"protocol":"progressive-manifest-v1","transport":"windows-pipe-json-lines","edition":"classic","profileSelectable":false,"maxRequestBytes":65536,"heartbeatSeconds":5,"disconnectAfterSeconds":15,"maxTrackPathChars":240,"actions":["capabilities","validate","probe","start","update","complete","heartbeat","status","cancel","release","error"],"containers":["fast-start-mp4","fragmented-mp4"],"videoCodecs":["h264"],"audioCodecs":["aac-lc"],"progressiveModes":["native-progressive","post-download"],"fallback":{"available":false,"reasonCode":"unsupported-native-format","networkRetry":false},"separateTracksRequired":true,"initializedTracksRequired":true,"startBufferSeconds":10,"resumeBufferSeconds":5,"pauseBufferSeconds":1})");
    auto& a=d.GetAllocator();d.AddMember("endpoint",Value(Utf8(Endpoint()).c_str(),a),a);d.AddMember("profile",Value(Utf8(ProfileDirectory()).c_str(),a),a);
    Document graph;graph.Parse(R"({"owner":"classic-sta","abortOpenWaitPumpsSentMessages":true,"closeWaitPumpsSentMessages":true,"nativeTeardownDetached":false,"fullInputPump":false})");d.AddMember("graphShutdown",Value(graph,a),a);
    Document maintenance;maintenance.Parse(R"({"fileIoOutsideMetadataLock":true,"snapshotIndexCommitAtomic":true,"notificationsMetadataOnly":true})");d.AddMember("legacyRefresh",Value(maintenance,a),a);
    Value legacy(rapidjson::kObjectType);legacy.AddMember("offUi",true,a);legacy.AddMember("maxConcurrent",1,a);legacy.AddMember("responseDeadlineMilliseconds",5000,a);legacy.AddMember("cancellableDiskIo",false,a);legacy.AddMember("previousPlaybackUntilCommit",true,a);d.AddMember("legacyPreparation",legacy,a);
    d.AddMember("readerRegistryOffUi",true,a);Value retirement(rapidjson::kObjectType);retirement.AddMember("maxSources",uint32_t(foxicmp::progressive::SourceRetirement::Capacity),a);retirement.AddMember("workerLimit",uint32_t(foxicmp::progressive::SourceRetirement::WorkerLimit),a);retirement.AddMember("processOwned",true,a);retirement.AddMember("filesClosedBeforeRelease",true,a);d.AddMember("sourceRetirement",retirement,a);
    d.AddMember("readerLeaseProtocol",1,a);d.AddMember("readerLeaseOptIn",true,a);d.AddMember("maxReaderReceipts",128,a);
    for(const auto* action:{"readerStatus","readerRelease","readerForget","readers","resourceProbe","resourceProbeUpdate","resourceProbeStatus","resourceProbeCancel","resourceProbeForget"})d["actions"].PushBack(Value(action,a),a);
    d.AddMember("resourceIoProbeVersion",1,a);Value schemas(rapidjson::kArrayType);schemas.PushBack(2,a);
    d.AddMember("resourceIoSchemaVersions",schemas,a);d.AddMember("resourceIoPlaybackSupported",false,a);
    d["playbackSchemaVersions"].PushBack(2,a);Document profile;profile.Parse(foxicmp::progressive::Fmp4V2Capabilities());d.AddMember("schema2Playback",Value(profile,a),a);
    Document preparation;preparation.Parse(R"({"offUi":["probe","start","update","complete"],"maxConcurrent":1,"responseDeadlineMilliseconds":5000,"updatesOffUi":true,"cancellableDiskIo":false})");d.AddMember("mediaPreparation",Value(preparation,a),a);
    return Encode(d);
}
std::wstring ClassicProgressiveApi::ReaderRegistry() {
    WCHAR temp[32768];const auto length=GetTempPathW(32768,temp);
    if(!length || length>=32768)throw std::runtime_error("Cannot obtain private reader runtime path");
    const auto endpoint=Endpoint();return std::wstring(temp,length)+endpoint.substr(endpoint.find_last_of(L'\\')+1)+L".readers";
}
ClassicProgressiveApi::ClassicProgressiveApi(CMainFrame& frame):frame(frame) {
    stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(stop)worker=std::thread([this]{Serve();});
}
ClassicProgressiveApi::Request::~Request() {
    if(done)CloseHandle(done);
}
ClassicProgressiveApi::~ClassicProgressiveApi() {frame.KillTimer(TIMER_CLASSIC_PROGRESSIVE_OPEN);if(legacyOpening)legacyOpening->abandoned.store(true);if(preparation)preparation->abandoned.store(true);if(registryPreparation)registryPreparation->abandoned.store(true);if(stop){SetEvent(stop);if(worker.joinable())worker.join();CloseHandle(stop);}}
bool ClassicProgressiveApi::BeginLegacy(const std::wstring& manifest) {
    if(legacyOpening && legacyOpening->abandoned.load() && legacyOpening->finished.load(std::memory_order_acquire))legacyOpening.reset();
    if(manifest.empty() || legacyOpening)return false;
    PlaybackIntentChanged();legacyManifest=manifest;
    try {
        legacyOpening=foxicmp::progressive::SourceOpening<CHarvesterSession>::launch([manifest]{
            CStringW error;auto candidate=CHarvesterSession::Open(manifest,error);
            if(!candidate)throw std::runtime_error(Utf8(error.GetString()));return candidate;
        });
    }catch(const std::exception& e){frame.SendStatusMessage(L"Harvester: "+Wide(e.what()),5000);return false;}
    if(!frame.SetTimer(TIMER_CLASSIC_PROGRESSIVE_OPEN,20,nullptr)){legacyOpening->abandoned.store(true);return false;}
    frame.SendStatusMessage(L"Harvester: подготовка источника…",5000);return true;
}
void ClassicProgressiveApi::PollLegacy() {
    auto job=legacyOpening;
    if(!job){frame.KillTimer(TIMER_CLASSIC_PROGRESSIVE_OPEN);return;}
    if(job->expired() && !job->abandoned.exchange(true))frame.SendStatusMessage(L"Harvester: истекло время подготовки источника",5000);
    if(!job->finished.load(std::memory_order_acquire))return;
    frame.KillTimer(TIMER_CLASSIC_PROGRESSIVE_OPEN);legacyOpening.reset();
    if(job->abandoned.load())return;
    if(!job->candidate){frame.SendStatusMessage(L"Harvester: "+Wide(job->error),5000);return;}
    auto data=std::make_unique<OpenFileData>();data->harvesterManifest=legacyManifest.c_str();
    data->fi=CStringW(job->candidate->Path(0).c_str());data->auds.emplace_back(job->candidate->Path(1).c_str());data->bAddRecent=FALSE;
    frame.SendMessageW(WM_COMMAND,ID_FILE_CLOSEMEDIA);frame.m_harvester=job->candidate;frame.OpenMedia(std::move(data));
}
void ClassicProgressiveApi::Prepare(const std::shared_ptr<Request>& job) {
    try {
        if(!job->abandoned.load() && job->readerQuery) {
            job->preparedReply=foxicmp::progressive::ReaderRequest(std::filesystem::path(job->registry),job->descriptor);
        }else if(!job->abandoned.load()) {
            Document descriptor;descriptor.Parse(job->descriptor.c_str());
            const bool v2=descriptor.HasMember("schemaVersion") && descriptor["schemaVersion"].IsUint() && descriptor["schemaVersion"].GetUint()==2;
            CStringW error;std::string reason;
            if(job->publication){if(!job->candidate->Publish(job->descriptor,error))job->preparedReply=Failure("invalid_update",Utf8(error.GetString()));}
            else job->candidate=CHarvesterSession::OpenPublished(job->descriptor,error,job->registry,&reason,!job->probe);
            if(job->publication){} // The existing source remains owned by its graph.
            else if(job->probe) {
                Document result;result.SetObject();auto& a=result.GetAllocator();const auto candidate=job->candidate;
                result.AddMember("version",1,a);result.AddMember("ok",true,a);result.AddMember("action","probe",a);
                result.AddMember("supported",candidate!=nullptr,a);result.AddMember("mode",Value(candidate?"native-progressive":v2?"unsupported":"post-download",a),a);
                result.AddMember("reasonCode",Value(candidate?"supported":v2?reason.c_str():"native-progressive-rejected",a),a);
                result.AddMember("reason",Value(Utf8(error.GetString()).c_str(),a),a);
                if(candidate){result.AddMember("readySeconds",double(candidate->ReadyEnd())/10000000.,a);result.AddMember("complete",candidate->Complete(),a);}
                else result.AddMember("requiresCompleteFile",!v2,a);
                job->preparedReply=Encode(result);job->candidate.reset();
            }else if(!job->candidate)job->preparedReply=Failure(v2?reason.c_str():"source_not_ready",Utf8(error.GetString()));
        }
    }catch(const std::exception& e){job->preparedReply=Failure("source_not_ready",e.what());}
    if(job->abandoned.load())job->candidate.reset();
    job->finished.store(true,std::memory_order_release);SetEvent(job->done);
}
void ClassicProgressiveApi::Serve() {
    const auto sid=UserSid();if(sid.empty())return;
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW((L"D:P(A;;GA;;;"+sid+L")").c_str(),SDDL_REVISION_1,&descriptor,nullptr))return;
    SECURITY_ATTRIBUTES security={sizeof(security),descriptor,FALSE};
    // A single bounded connection and FIRST_PIPE_INSTANCE preserve the endpoint
    // of the first Classic window; other installations/Qt have separate names.
    // Keep the endpoint private through its user-only DACL.  Some older
    // Windows builds reject PIPE_REJECT_REMOTE_CLIENTS/FIRST_PIPE_INSTANCE
    // when combined with an overlapped byte pipe, so avoid those optional
    // flags and let the endpoint hash provide uniqueness.
    HANDLE pipe=CreateNamedPipeW(Endpoint().c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,65536,65536,5000,&security);
    LocalFree(descriptor);if(pipe==INVALID_HANDLE_VALUE)return;
    while(WaitForSingleObject(stop,0)==WAIT_TIMEOUT) {
        OVERLAPPED connected{};connected.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        BOOL ok=ConnectNamedPipe(pipe,&connected);const DWORD error=GetLastError();
        if(!ok && error==ERROR_IO_PENDING) {
            HANDLE events[]={stop,connected.hEvent};
            if(WaitForMultipleObjects(2,events,FALSE,INFINITE)==WAIT_OBJECT_0+1){DWORD count=0;ok=GetOverlappedResult(pipe,&connected,&count,FALSE);}
            else {CancelIoEx(pipe,&connected);DWORD count=0;GetOverlappedResult(pipe,&connected,&count,TRUE);}
        }else if(!ok && error==ERROR_PIPE_CONNECTED)ok=TRUE;
        CloseHandle(connected.hEvent);
        if(!ok) {
            if(WaitForSingleObject(stop,0)!=WAIT_TIMEOUT)break;
            DisconnectNamedPipe(pipe);continue; // A closed client must not kill the listener.
        }
        std::string input;char bytes[4096];DWORD count=0;
        const auto deadline=GetTickCount64()+5000;
        while(input.size()<=65536 && input.find('\n')==std::string::npos && GetTickCount64()<deadline &&
              Transfer(pipe,stop,bytes,sizeof(bytes),count,false,DWORD(deadline-GetTickCount64())) && count)input.append(bytes,count);
        std::string output;
        if(input.empty() || input.size()>65536 || input.find('\n')!=input.size()-1)output=Failure("invalid_request","One JSON line of at most 65536 bytes is required");
        else {
            auto request=std::make_shared<Request>();request->input=std::move(input);
            const auto dispatch=[&]{
                ResetEvent(request->done);auto* message=new std::shared_ptr<Request>(request);
                if(!frame.PostMessageW(WM_CLASSIC_PROGRESSIVE_REQUEST,0,reinterpret_cast<LPARAM>(message))){delete message;return false;}
                HANDLE events[]={stop,request->done};return WaitForMultipleObjects(2,events,FALSE,5000)==WAIT_OBJECT_0+1;
            };
            if(dispatch()) {
                while(request->requested && (!request->ready || request->readerRepeat)) {
                    if(request->readerRepeat){request->readerRepeat=false;++request->readerPass;request->ready=false;request->finished.store(false);}
                    ResetEvent(request->done);
                    try{std::thread([request]{Prepare(request);}).detach();}
                    catch(const std::exception& e){request->preparedReply=Failure("preparation_unavailable",e.what());request->finished.store(true);SetEvent(request->done);}
                    HANDLE events[]={stop,request->done};
                    if(WaitForMultipleObjects(2,events,FALSE,5000)==WAIT_OBJECT_0+1) {
                        if(!PeekNamedPipe(pipe,nullptr,0,nullptr,nullptr,nullptr))request->abandoned.store(true);
                        request->ready=true;
                        if(dispatch())output=request->output;else{request->abandoned.store(true);break;}
                    }else{request->abandoned.store(true);output=Failure("preparation_timeout","Request exceeded the response deadline");break;}
                }
                if(!request->requested)output=request->output;
            }else request->abandoned.store(true);
        }
        if(!output.empty()) {
            output+='\n';DWORD written=0;
            if(Transfer(pipe,stop,output.data(),DWORD(output.size()),written,true)) {
                // DisconnectNamedPipe discards unread output. Let the client
                // consume the JSON and close, with a cancellable bounded wait.
                char unused;DWORD received=0;
                Transfer(pipe,stop,&unused,1,received,false,1000);
            }
        }
        // No unbounded FlushFileBuffers: stop still interrupts the drain wait.
        DisconnectNamedPipe(pipe);
    }
    CloseHandle(pipe);
}
void ClassicProgressiveApi::Ended(const std::shared_ptr<CHarvesterSession>& ended,bool natural) {
    if(ended!=source.lock())return;
    Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("version",1,a);d.AddMember("ok",true,a);d.AddMember("active",false,a);
    d.AddMember("sessionId",Value(sessionId.c_str(),a),a);const auto error=ended->Error();
    d.AddMember("state",Value(error.empty()?(natural?"finished":"stopped"):"error",a),a);d.AddMember("error",Value(error.c_str(),a),a);
    Document details;details.Parse(ended->PlaybackDetails().c_str());for(auto it=details.MemberBegin();it!=details.MemberEnd();++it)d.AddMember(Value(it->name,a),Value(it->value,a),a);lastOutcome=Encode(d);
}
std::string ClassicProgressiveApi::Status() {
    auto session=source.lock();
    if(!session || frame.m_harvester!=session) {
        if(!lastOutcome.empty()) {
            Document result;result.Parse(lastOutcome.c_str());
            result.AddMember("released",releaseReceipt && releaseReceipt->load(),result.GetAllocator());
            if(!readerId.empty())result.AddMember("readerId",Value(readerId.c_str(),result.GetAllocator()),result.GetAllocator());
            return Encode(result);
        }
        return Failure("session_closed","Playback was stopped or replaced");
    }
    const auto error=session->Error();const auto snapshot=session->Snapshot();
    const char* state=!error.empty()?"error":frame.m_bGraphEventComplete?"finished":!session->Connected()?"waiting-downloader":
        !frame.m_harvesterStarted || frame.m_harvesterBuffering?"buffering":!frame.m_harvesterWantPlay?"paused":"playing";
    Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("version",1,a);d.AddMember("ok",true,a);d.AddMember("active",true,a);
    d.AddMember("sessionId",Value(sessionId.c_str(),a),a);d.AddMember("state",Value(state,a),a);d.AddMember("error",Value(error.c_str(),a),a);
    d.AddMember("released",false,a);d.AddMember("connected",session->Connected(),a);d.AddMember("complete",session->Complete(),a);
    if(!readerId.empty())d.AddMember("readerId",Value(readerId.c_str(),a),a);
    Document details;details.Parse(session->PlaybackDetails().c_str());for(auto it=details.MemberBegin();it!=details.MemberEnd();++it)d.AddMember(Value(it->name,a),Value(it->value,a),a);
    d.AddMember("positionSeconds",double(frame.GetPos())/10000000.,a);d.AddMember("availableSeconds",double(session->ReadyEnd())/10000000.,a);
    d.AddMember("durationSeconds",double(snapshot.duration)/10000000.,a);return Encode(d);
}
std::string ClassicProgressiveApi::Handle(const std::string& raw,Request* prepared) {
    try {
        if(prepared && prepared->ready && !prepared->readerQuery && preparation.get()==prepared)preparation.reset();
        if(prepared && prepared->ready && prepared->readerQuery && registryPreparation.get()==prepared && prepared->readerPass)registryPreparation.reset();
        if(prepared && (prepared->abandoned.load() || (prepared->ready && !prepared->readerQuery && prepared->epoch!=intentEpoch)))return Failure("preparation_cancelled","Playback intent changed while preparing the source");
        Document d;d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(raw.data(),raw.size());
        if(d.HasParseError() || !d.IsObject())return Failure("invalid_request","Invalid JSON");
        harvester::UniqueKeys(d);
        if(harvester::Number(d,"version")!=1)return Failure("invalid_request","Expected version 1");
        const auto action=harvester::String(d,"action");
        if(action=="resourceProbe" || action=="resourceProbeUpdate" || action=="resourceProbeStatus" || action=="resourceProbeCancel" || action=="resourceProbeForget") {
            if(!resourceProbes)resourceProbes=std::make_unique<foxicmp::progressive::ResourceProbeManager>();
            return resourceProbes->request(std::filesystem::path(ReaderRegistry()),raw);
        }
        if(action=="readerStatus" || action=="readerRelease" || action=="readerForget" || action=="readers") {
            if(prepared && !prepared->ready) {
                try{foxicmp::progressive::ReaderCommand(raw);}catch(const foxicmp::progressive::Error& e){return Failure(e.code.c_str(),e.what());}
                if(registryPreparation && !registryPreparation->finished.load(std::memory_order_acquire))return Failure("reader_registry_busy","Another registry request is active");
                registryPreparation=prepared->shared_from_this();prepared->requested=true;prepared->readerQuery=true;prepared->descriptor=raw;prepared->registry=ReaderRegistry();
                if(action=="readerRelease" && harvester::String(d,"readerId")==readerId){auto active=source.lock();if(active && frame.m_harvester==active)prepared->candidate=active;}
                return {};
            }
            auto reply=prepared?prepared->preparedReply:foxicmp::progressive::ReaderRequest(std::filesystem::path(ReaderRegistry()),raw);
            Document result;result.Parse(reply.c_str());
            if(action=="readerRelease" && result["ok"].GetBool() && !readerId.empty() && harvester::String(d,"readerId")==readerId && (!prepared || !prepared->readerPass)) {
                auto active=source.lock();
                if(active && frame.m_harvester==active && (!prepared || prepared->candidate==active)) {
                    frame.SendMessageW(WM_COMMAND,ID_FILE_CLOSEMEDIA);
                    if(prepared){prepared->readerRepeat=true;return {};}
                    reply=foxicmp::progressive::ReaderRequest(std::filesystem::path(ReaderRegistry()),raw);
                }
            }
            if(prepared && registryPreparation.get()==prepared)registryPreparation.reset();
            return reply;
        }

        if(action=="capabilities") {
            if(!Keys(d,{"version","action"}))return Failure("invalid_request","Unexpected fields");
            Document info;info.Parse(Information().c_str());info.RemoveMember("endpoint");info.RemoveMember("profile");
            auto& a=info.GetAllocator();info.AddMember("ok",true,a);info.AddMember("pid",uint32_t(GetCurrentProcessId()),a);info.AddMember("legacyPreparationPending",bool(legacyOpening),a);return Encode(info);
        }
        if(action=="validate") {
            if(!Keys(d,{"version","action","session"},{"previous"}) || !d["session"].IsObject() ||
                (d.HasMember("previous") && !d["previous"].IsObject()))return Failure("invalid_request","Expected a schema-2 session and optional previous snapshot");
            return foxicmp::progressive::ValidateReply(Encode(d["session"]),d.HasMember("previous")?Encode(d["previous"]):std::string());
        }
        const bool v2=d.HasMember("session") && d["session"].IsObject() && d["session"].HasMember("schemaVersion") && d["session"]["schemaVersion"].IsUint() && d["session"]["schemaVersion"].GetUint()==2;
        if((action=="start" || action=="probe") && v2) {
            try {foxicmp::progressive::Fmp4V2Bridge::CheckProfile(Encode(d["session"]));}
            catch(const foxicmp::progressive::Error& e) {
                if(action=="start")return Failure(e.code.c_str(),e.what());
                Document result;result.SetObject();auto& a=result.GetAllocator();result.AddMember("version",1,a);result.AddMember("ok",true,a);result.AddMember("supported",false,a);result.AddMember("mode","unsupported",a);result.AddMember("reasonCode",Value(e.code.c_str(),a),a);result.AddMember("reason",Value(e.what(),a),a);return Encode(result);
            }
        }
        if(action=="probe") {
            if(!Keys(d,{"version","action","session"},{"sessionDirectory"}) || !d["session"].IsObject())return Failure("invalid_request","Invalid probe request");
            Value descriptor(d["session"],d.GetAllocator());
            if(d.HasMember("sessionDirectory")) descriptor.AddMember("sessionDirectory",Value(d["sessionDirectory"],d.GetAllocator()),d.GetAllocator());
            if(prepared){
                if(prepared->ready)return prepared->preparedReply;
                if(preparation && !preparation->finished.load(std::memory_order_acquire))return Failure("preparation_busy","Another media source is being prepared");
                preparation=prepared->shared_from_this();
                prepared->requested=true;prepared->probe=true;prepared->descriptor=Encode(descriptor);prepared->registry=v2?ReaderRegistry():std::wstring();prepared->epoch=intentEpoch;return {};
            }
            CStringW probeError;std::string reason;auto candidate=CHarvesterSession::OpenPublished(Encode(descriptor),probeError,v2?ReaderRegistry():std::wstring(),&reason,false);
            Document result;result.SetObject();auto& a=result.GetAllocator();result.AddMember("version",1,a);result.AddMember("ok",true,a);result.AddMember("action","probe",a);
            result.AddMember("supported",candidate!=nullptr,a);
            result.AddMember("mode",Value(candidate?"native-progressive":v2?"unsupported":"post-download",a),a);
            result.AddMember("reasonCode",Value(candidate?"supported":v2?reason.c_str():"native-progressive-rejected",a),a);
            result.AddMember("reason",Value(Utf8(probeError.GetString()).c_str(),a),a);
            if(candidate) {
                result.AddMember("readySeconds",double(candidate->ReadyEnd())/10000000.,a);
                result.AddMember("complete",candidate->Complete(),a);
            } else {
                result.AddMember("requiresCompleteFile",!v2,a);
            }
            if(candidate)candidate->Cancel();return Encode(result);
        }
        if(action=="start") {
            if(!Keys(d,{"version","action","session"},{"replace","activate","title","sessionDirectory","trackReader"}) || !d["session"].IsObject() ||
                (d.HasMember("replace") && !d["replace"].IsBool()) || (d.HasMember("activate") && !d["activate"].IsBool()) ||
                (d.HasMember("title") && (!d["title"].IsString() || d["title"].GetStringLength()>4096)))return Failure("invalid_request","Invalid start request");
            if(d.HasMember("trackReader") && !d["trackReader"].IsBool())return Failure("invalid_request","Invalid reader tracking option");
            const auto id=harvester::String(d["session"],"sessionId");
            const auto requestedGeneration=v2?foxicmp::progressive::Unsigned(d["session"]["generation"],"generation",UINT64_MAX,true):harvester::Number(d["session"],"generation");
            if(source.lock() && source.lock()==frame.m_harvester && id==sessionId && sessionSchema==(v2?2:1) && requestedGeneration==sessionGeneration) {
                if(v2){Document details;details.Parse(source.lock()->PlaybackDetails().c_str());if(harvester::String(details,"writerInstanceId")!=harvester::String(d["session"]["writer"],"instanceId"))return Failure("generation_changed","Writer incarnation changed; use a new generation");}
                if(d.HasMember("trackReader") && d["trackReader"].GetBool() && readerId.empty())return Failure("reader_not_tracked","Existing session was opened without a reader lease");
                return Status();
            }
            if(frame.m_harvester && !(d.HasMember("replace") && d["replace"].GetBool()))return Failure("session_busy","Explicit replacement is required");
            std::wstring title;
            if(d.HasMember("title") && d["title"].GetStringLength()) {
                const auto& text=d["title"];
                int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.GetString(),int(text.GetStringLength()),nullptr,0);
                title.resize(size);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.GetString(),int(text.GetStringLength()),title.data(),size);
                for(auto& c:title)if(c<32 || c==127)c=L' ';
            }
            Value descriptor(d["session"],d.GetAllocator());
            if(d.HasMember("sessionDirectory")) descriptor.AddMember("sessionDirectory",Value(d["sessionDirectory"],d.GetAllocator()),d.GetAllocator());
            if(prepared && !prepared->ready){
                if(preparation && !preparation->finished.load(std::memory_order_acquire))return Failure("preparation_busy","Another media source is being prepared");
                PlaybackIntentChanged();
                preparation=prepared->shared_from_this();
                prepared->requested=true;prepared->descriptor=Encode(descriptor);prepared->registry=v2 || (d.HasMember("trackReader") && d["trackReader"].GetBool())?ReaderRegistry():std::wstring();prepared->epoch=intentEpoch;return {};
            }
            if(prepared && !prepared->preparedReply.empty())return prepared->preparedReply;
            if(!prepared)PlaybackIntentChanged();
            CStringW error;std::string reason;auto next=prepared?prepared->candidate:CHarvesterSession::OpenPublished(Encode(descriptor),error,
                v2 || (d.HasMember("trackReader") && d["trackReader"].GetBool())?ReaderRegistry():std::wstring(),&reason);
            if(!next)return Failure(v2?reason.c_str():"source_not_ready",Utf8(error.GetString()));
            frame.SendMessageW(WM_COMMAND,ID_FILE_CLOSEMEDIA);
            auto data=std::make_unique<OpenFileData>();data->harvesterManifest=L"published-api";data->fi=CStringW(next->Path(0).c_str());data->auds.emplace_back(next->Path(1).c_str());data->bAddRecent=FALSE;
            if(!title.empty())data->fi.SetTitle(title.c_str());
            frame.m_harvester=next;frame.m_harvesterStarted=false;frame.m_harvesterBuffering=true;frame.m_harvesterWantPlay=true;source=next;releaseReceipt=next->ReleaseReceipt();sessionId=id;sessionGeneration=next->Snapshot().generation;sessionSchema=v2?2:1;readerId=next->RegisteredReaderId();lastOutcome.clear();frame.OpenMedia(std::move(data));
            if(!d.HasMember("activate") || d["activate"].GetBool()){frame.ShowWindow(SW_RESTORE);frame.SetForegroundWindow();}
            return Status();
        }
        const bool update=action=="update" || action=="complete", failure=action=="error";
        if(action!="status" && action!="cancel" && action!="release" && action!="heartbeat" && !update && !failure)return Failure("invalid_request","Unknown action");
        if(!Keys(d,{"version","action","sessionId"},update?std::initializer_list<const char*>{"session","sessionDirectory","readerId"}:failure?std::initializer_list<const char*>{"message","readerId"}:std::initializer_list<const char*>{"readerId"}) ||
            harvester::String(d,"sessionId")!=sessionId || sessionId.empty())return Failure("unknown_session","Unknown session identifier");
        if(d.HasMember("readerId") && (!d["readerId"].IsString() || readerId.empty() || harvester::String(d,"readerId")!=readerId))
            return Failure("reader_mismatch","Command belongs to a different reader; use readerStatus/readerRelease for its receipt");
        auto active=source.lock();
        if(!active || frame.m_harvester!=active)return (action=="status" || action=="cancel" || action=="release")?Status():Failure("session_closed","Playback was stopped or replaced");
        if(update) {
            if(!d.HasMember("session") || !d["session"].IsObject() || (action=="complete" && harvester::String(v2?harvester::Field(d["session"],"writer"):d["session"],"state")!="complete"))return Failure("invalid_request","Expected a session snapshot");
            Value descriptor(d["session"],d.GetAllocator());
            if(d.HasMember("sessionDirectory")) descriptor.AddMember("sessionDirectory",Value(d["sessionDirectory"],d.GetAllocator()),d.GetAllocator());
            if(prepared && !prepared->ready){
                if(preparation && !preparation->finished.load(std::memory_order_acquire))return Failure("preparation_busy","Another media source is being prepared");
                preparation=prepared->shared_from_this();prepared->requested=true;prepared->publication=true;
                prepared->descriptor=Encode(descriptor);prepared->candidate=active;prepared->epoch=intentEpoch;return {};
            }
            if(prepared){
                if(!prepared->preparedReply.empty())return prepared->preparedReply;
                if(prepared->candidate!=active)return Failure("reader_mismatch","Prepared update belongs to another reader");
            }else{CStringW error;if(!active->Publish(Encode(descriptor),error))return Failure("invalid_update",Utf8(error.GetString()));}
        }else if(action=="heartbeat")active->PublishedHeartbeat();
        else if(action=="cancel" || action=="release") {
            frame.SendMessageW(WM_COMMAND,ID_FILE_CLOSEMEDIA);Document result;result.Parse(lastOutcome.c_str());
            if(result.IsObject()){result["state"].SetString("cancelled",result.GetAllocator());lastOutcome=Encode(result);}return Status();
        }else if(failure) {
            const auto message=harvester::String(d,"message");if(message.empty() || message.size()>4096)return Failure("invalid_request","Expected a short error message");active->Fail(message.c_str());
        }
        return Status();
    }catch(const std::exception& e){return Failure("invalid_request",e.what());}
}
