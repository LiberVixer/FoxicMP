#include "stdafx.h"
#include "ClassicProgressiveApi.h"
#include "../../../shared/preparedclip.h"
#include "../../../shared/progressivecompletedseek.h"
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
#include "../../../shared/progressiveclassicplayback.h"
#include "../../../shared/progressivestreamplayback.h"
#include "../../../shared/progressivestreamcapabilities.h"

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
    Document graph;graph.Parse(R"({"owner":"classic-sta","abortOpenWaitPumpsSentMessages":true,"closeWaitPumpsSentMessages":true,"nativeTeardownDetached":false,"fullInputPump":false,"progressiveStaCloseAsync":true,"deferredStart":true,"deferredExit":true,"maxPendingStarts":1,"uiOwnedGraphCloseAsync":false})");d.AddMember("graphShutdown",Value(graph,a),a);
    Document maintenance;maintenance.Parse(R"({"fileIoOutsideMetadataLock":true,"snapshotIndexCommitAtomic":true,"notificationsMetadataOnly":true})");d.AddMember("legacyRefresh",Value(maintenance,a),a);
    Value legacy(rapidjson::kObjectType);legacy.AddMember("offUi",true,a);legacy.AddMember("maxConcurrent",1,a);legacy.AddMember("responseDeadlineMilliseconds",5000,a);legacy.AddMember("cancellableDiskIo",false,a);legacy.AddMember("previousPlaybackUntilCommit",true,a);d.AddMember("legacyPreparation",legacy,a);
    d.AddMember("readerRegistryOffUi",true,a);Value retirement(rapidjson::kObjectType);retirement.AddMember("maxSources",uint32_t(foxicmp::progressive::SourceRetirement::Capacity),a);retirement.AddMember("workerLimit",uint32_t(foxicmp::progressive::SourceRetirement::WorkerLimit),a);retirement.AddMember("processOwned",true,a);retirement.AddMember("filesClosedBeforeRelease",true,a);d.AddMember("sourceRetirement",retirement,a);
    d.AddMember("readerLeaseProtocol",1,a);d.AddMember("readerLeaseOptIn",true,a);d.AddMember("maxReaderReceipts",128,a);
    for(const auto* action:{"readerStatus","readerRelease","readerForget","readers","resourceProbe","resourceProbeUpdate","resourceProbeStatus","resourceProbeCancel","resourceProbeForget","previewRemux","previewStatus","previewCancel","previewControl"})d["actions"].PushBack(Value(action,a),a);
    for(const auto* action:{"continuousRemuxStart","continuousRemuxStatus","continuousRemuxUpdate","continuousRemuxHeartbeat","continuousRemuxCancel","continuousRemuxForget","continuousRemuxPlay","continuousPlaybackStatus","continuousPlaybackControl"})d["actions"].PushBack(Value(action,a),a);
    Document continuous;continuous.Parse(foxicmp::progressive::ContinuousRemuxCapabilities());d.AddMember("continuousRemux",Value(continuous,a),a);
    Document seek;seek.Parse(foxicmp::progressive::CompletedSeekCapabilities());d.AddMember("completedSourceSeek",Value(seek,a),a);
    Document preview;preview.Parse(foxicmp::progressive::PreparedPreviewCapabilities());d.AddMember("preparedRemuxPreview",Value(preview,a),a);
    Document remux;remux.Parse(foxicmp::progressive::BackendRemuxCapabilities());d.AddMember("backendRemuxProbe",Value(remux,a),a);
    Document demux;demux.Parse(foxicmp::progressive::BackendDemuxCapabilities());d.AddMember("backendDemuxProbe",Value(demux,a),a);
    d.AddMember("resourceIoProbeVersion",1,a);Value schemas(rapidjson::kArrayType);schemas.PushBack(2,a);
    d.AddMember("resourceIoSchemaVersions",schemas,a);d.AddMember("resourceIoPlaybackSupported",false,a);d.AddMember("segmentProbeVersion",1,a);
    d["playbackSchemaVersions"].PushBack(2,a);Document profile;profile.Parse(foxicmp::progressive::ClassicPlaybackCapabilities().c_str());d.AddMember("schema2Playback",Value(profile,a),a);
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
ClassicProgressiveApi::~ClassicProgressiveApi() {frame.KillTimer(TIMER_CLASSIC_PROGRESSIVE_OPEN);if(legacyOpening)legacyOpening->abandoned.store(true);if(continuousOpening)continuousOpening->abandoned.store(true);if(preparation)preparation->abandoned.store(true);if(registryPreparation)registryPreparation->abandoned.store(true);if(stop){SetEvent(stop);if(worker.joinable())worker.join();CloseHandle(stop);}}
bool ClassicProgressiveApi::BeginLegacy(const std::wstring& manifest) {
    if(legacyOpening && legacyOpening->abandoned.load() && legacyOpening->finished.load(std::memory_order_acquire))legacyOpening.reset();
    if(manifest.empty() || legacyOpening)return false;
    PlaybackIntentChanged(CStringW(),true);legacyManifest=manifest;
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
    PollContinuous();
    auto job=legacyOpening;
    if(!job){if(!continuousOpening)frame.KillTimer(TIMER_CLASSIC_PROGRESSIVE_OPEN);return;}
    if(job->expired() && !job->abandoned.exchange(true))frame.SendStatusMessage(L"Harvester: истекло время подготовки источника",5000);
    if(!job->finished.load(std::memory_order_acquire))return;
    if(!continuousOpening)frame.KillTimer(TIMER_CLASSIC_PROGRESSIVE_OPEN);legacyOpening.reset();
    if(job->abandoned.load())return;
    if(!job->candidate){frame.SendStatusMessage(L"Harvester: "+Wide(job->error),5000);return;}
    auto data=std::make_unique<OpenFileData>();data->harvesterManifest=legacyManifest.c_str();
    data->fi=CStringW(job->candidate->Path(0).c_str());data->auds.emplace_back(job->candidate->Path(1).c_str());data->bAddRecent=FALSE;
    data->preparedHarvester=job->candidate;frame.OpenMedia(std::move(data));
}
void ClassicProgressiveApi::Prepare(const std::shared_ptr<Request>& job) {
    try {
        if(!job->abandoned.load() && job->readerQuery) {
            job->preparedReply=foxicmp::progressive::ReaderRequest(std::filesystem::path(job->registry),job->descriptor);
        }else if(!job->abandoned.load()) {
            Document descriptor;descriptor.Parse(job->descriptor.c_str());
            const bool v2=descriptor.HasMember("schemaVersion") && descriptor["schemaVersion"].IsUint() && descriptor["schemaVersion"].GetUint()==2;
            CStringW error;std::string reason;
            if(job->publication){if(!job->candidate->Publish(job->descriptor,error,&reason))job->preparedReply=Failure(reason.empty()?"invalid_update":reason.c_str(),Utf8(error.GetString()));}
            else job->candidate=CHarvesterSession::OpenPublished(job->descriptor,error,job->registry,&reason,!job->probe,true);
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
    if(!session || !frame.OwnsHarvester(session)) {
        if(!lastOutcome.empty()) {
            Document result;result.Parse(lastOutcome.c_str());
            result.AddMember("released",releaseReceipt && releaseReceipt->load(),result.GetAllocator());
            if(!readerId.empty())result.AddMember("readerId",Value(readerId.c_str(),result.GetAllocator()),result.GetAllocator());
            return Encode(result);
        }
        return Failure("session_closed","Playback was stopped or replaced");
    }
    const auto error=session->Error();const auto snapshot=session->Snapshot();
    const bool pending=frame.IsGraphClosePending();
    const char* state=!error.empty()?"error":pending?"buffering":frame.m_bGraphEventComplete?"finished":!session->Connected()?"waiting-downloader":
        !frame.m_harvesterStarted || frame.m_harvesterBuffering?"buffering":!frame.m_harvesterWantPlay?"paused":"playing";
    Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("version",1,a);d.AddMember("ok",true,a);d.AddMember("active",true,a);
    d.AddMember("sessionId",Value(sessionId.c_str(),a),a);d.AddMember("state",Value(state,a),a);d.AddMember("error",Value(error.c_str(),a),a);
    d.AddMember("released",false,a);d.AddMember("connected",session->Connected(),a);d.AddMember("complete",session->Complete(),a);
    if(!readerId.empty())d.AddMember("readerId",Value(readerId.c_str(),a),a);
    Document details;details.Parse(session->PlaybackDetails().c_str());for(auto it=details.MemberBegin();it!=details.MemberEnd();++it)d.AddMember(Value(it->name,a),Value(it->value,a),a);
    if(d.HasMember("subtitleTracks"))for(auto& track:d["subtitleTracks"].GetArray())
        track.AddMember("loaded",frame.m_harvesterSubtitles.count(std::wstring(Wide(track["path"].GetString()).GetString()))!=0,a);
    if(!pending && frame.m_eMediaLoadState==MLS_LOADED){CComQIPtr<IAMStreamSelect> select=frame.m_pSwitcherFilter.p;DWORD count=0;int index=-1;
        if(select && SUCCEEDED(select->Count(&count)))for(DWORD i=0;i<count && i<9;++i){DWORD flags=0;if(SUCCEEDED(select->Info(i,nullptr,&flags,nullptr,nullptr,nullptr,nullptr,nullptr)) && (flags&(AMSTREAMSELECTINFO_ENABLED|AMSTREAMSELECTINFO_EXCLUSIVE)))index=int(i);}
        Value selection(rapidjson::kObjectType);selection.AddMember("count",unsigned(count),a);selection.AddMember("index",index,a);d.AddMember("audioSelection",selection,a);
        d.AddMember("loadedSubtitleCount",unsigned(frame.m_pSubStreams.size()),a);
        if(d.HasMember("mediaTracks"))for(auto& track:d["mediaTracks"].GetArray()) {
            const int mediaIndex=track["index"].GetInt();const auto stream=frame.m_harvesterAudioStreams.find(mediaIndex);
            track.AddMember("loaded",mediaIndex==0 || stream!=frame.m_harvesterAudioStreams.end(),a);
            track.AddMember("selected",(stream!=frame.m_harvesterAudioStreams.end() && int(stream->second)==index) || (session->TrackCount()==1 && session->IsAudio(0) && index>=0),a);
            const auto error=frame.m_harvesterAudioErrors.find(mediaIndex);if(error!=frame.m_harvesterAudioErrors.end()){track.AddMember("backendErrorCode","decoder_error",a);track.AddMember("backendError",Value(error->second.c_str(),a),a);}
        }
    }
    if(!pending && frame.m_eMediaLoadState==MLS_LOADED && frame.m_pCB) {
        Value applied(rapidjson::kObjectType),chapters(rapidjson::kArrayType);
        applied.AddMember("revision",Value(frame.m_harvesterMetadataRevision?std::to_string(frame.m_harvesterMetadataRevision).c_str():"",a),a);
        applied.AddMember("title",Value(Utf8(frame.GetMediaTitleOrFileName().GetString()).c_str(),a),a);
        for(DWORD i=0;i<frame.m_pCB->ChapGetCount();++i) {
            REFERENCE_TIME at=0;CComBSTR title;if(FAILED(frame.m_pCB->ChapGet(i,&at,&title)))continue;
            Value chapter(rapidjson::kObjectType);chapter.AddMember("time",Value(std::to_string(at).c_str(),a),a);
            chapter.AddMember("title",Value(Utf8(title.m_str?title.m_str:L"").c_str(),a),a);chapters.PushBack(chapter,a);
        }
        applied.AddMember("chapters",chapters,a);d.AddMember("appliedMetadata",applied,a);
    }
    if(!pending && frame.m_eMediaLoadState==MLS_LOADED && frame.m_pMS) {
        double rate=0;if(SUCCEEDED(frame.m_pMS->GetRate(&rate))) {
            Value controls(rapidjson::kObjectType);controls.AddMember("speed",rate,a);
            controls.AddMember("volumePercent",AfxGetAppSettings().nVolume,a);controls.AddMember("muted",bool(AfxGetAppSettings().fMute),a);
            long nativeVolume=0;if(frame.m_pBA && SUCCEEDED(frame.m_pBA->get_Volume(&nativeVolume)))controls.AddMember("nativeVolumeDbHundredths",int(nativeVolume),a);
            d.AddMember("controls",controls,a);
        }
    }
    d.AddMember("positionSeconds",pending?0.:double(frame.GetPos())/10000000.,a);d.AddMember("availableSeconds",double(session->ReadyEnd())/10000000.,a);
    d.AddMember("durationSeconds",double(snapshot.duration)/10000000.,a);return Encode(d);
}
std::string ClassicProgressiveApi::Handle(const std::string& raw,Request* prepared) {
    try {
        if(prepared && prepared->ready && !prepared->readerQuery && preparation.get()==prepared)preparation.reset();
        if(prepared && prepared->ready && prepared->readerQuery && registryPreparation.get()==prepared && prepared->readerPass)registryPreparation.reset();
        if(prepared && (prepared->abandoned.load() || (prepared->ready && !prepared->readerQuery && prepared->epoch!=intentEpoch)))return Failure("preparation_cancelled","Playback intent changed while preparing the source");
        Document d;d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(raw.data(),raw.size());
        if(d.HasParseError() || !d.IsObject())return Failure("invalid_request","Invalid JSON");
        foxicmp::progressive::InlineKeys(d);
        if(harvester::Number(d,"version")!=1)return Failure("invalid_request","Expected version 1");
        const auto action=harvester::String(d,"action");
        if(foxicmp::progressive::ContinuousRemuxAction(action)) {
            // Preparation only: no graph mutation and no media-opening slot.
            if(!continuousRemux) {
                const auto registry=ReaderRegistry();
                continuousRemux=std::make_unique<foxicmp::progressive::ContinuousRemuxController>(
                    std::filesystem::path(registry),std::filesystem::path(registry+L".continuous-cache"),
                    std::filesystem::path(ProfileDirectory())/L"foxicmp-remux-stream-worker.exe");
            }
            auto reply=continuousRemux->request(raw);Document answer;answer.Parse(reply.c_str());
            if(answer["ok"].GetBool() && (action=="continuousRemuxCancel" || action=="continuousRemuxForget") && continuousPlayback && harvester::String(answer,"jobId")==continuousPlayback->command.jobId){
                continuousPlayback->closed=true;if(continuousOpening)continuousOpening->abandoned.store(true);
                if(frame.OwnsHarvester(continuousNative.lock()))frame.SendMessageW(WM_COMMAND,ID_FILE_CLOSEMEDIA);
            }
            return reply;
        }
        if(foxicmp::progressive::ContinuousPlaybackAction(action)) {
            try {
                const auto c=foxicmp::progressive::ParseContinuousPlayback(raw);
                if(c.action=="continuousRemuxPlay"){
                    if(continuousPlayback && continuousPlayback->command.requestId==c.requestId){
                        if(continuousPlayback->command.fingerprint!=c.fingerprint)return Failure("continuous-playback-conflict","Playback request identity conflict");return ContinuousStatus();
                    }
                    if(continuousPlayback && continuousPlayback->command.jobId==c.jobId)return Failure("continuous-playback-conflict","This job already has a playback attempt");
                    if(continuousOpening || (continuousPlayback && continuousPlayback->committing))return Failure("continuous-playback-busy","Previous playback is still preparing/closing");
                    if(!continuousRemux)return Failure("unknown-continuous-job","Unknown remux job");
                    auto next=std::make_shared<foxicmp::progressive::ContinuousPlayback>();next->command=c;
                    next->source=continuousRemux->playbackSource(c.jobId,c.sessionId,c.generation,c.writer,next->duration);
                    next->output=continuousRemux->outputObserver(c.jobId);
                    next->producerError=continuousRemux->errorObserver(c.jobId);
                    if((frame.GetLoadState()!=MLS_CLOSED || frame.HasHarvesterPlayback()) && !c.replace)return Failure("session_busy","Playback replacement must be explicit");
                    PlaybackIntentChanged();continuousEpoch=intentEpoch;continuousPlayback=next;continuousFinished=false;continuousNative.reset();
                    continuousOpening=foxicmp::progressive::SourceOpening<CHarvesterSession>::launch([next]{
                        CStringW error;auto source=CHarvesterSession::OpenContinuous(next->source,next->duration,error);
                        if(!source)throw std::runtime_error(Utf8(error.GetString()));return source;
                    });
                    if(!frame.SetTimer(TIMER_CLASSIC_PROGRESSIVE_OPEN,20,nullptr)){continuousOpening->abandoned.store(true);next->closed=true;next->error="continuous-start-failed";continuousRemux->cancelPlaybackJob(c.jobId);}
                    return ContinuousStatus();
                }
                if(!continuousPlayback || !continuousPlayback->matches(c))return Failure("unknown-continuous-playback","Unknown playback identity");
                if(c.action=="continuousPlaybackControl"){
                    const auto native=continuousNative.lock();
                    if(c.command=="stop"){
                        continuousPlayback->closed=true;if(continuousOpening)continuousOpening->abandoned.store(true);
                        continuousRemux->cancelPlaybackJob(continuousPlayback->command.jobId);
                        if(frame.OwnsHarvester(native))frame.SendMessageW(WM_COMMAND,ID_FILE_CLOSEMEDIA);
                    }else{
                        if(continuousPlayback->closed || continuousFinished || !continuousPlayback->error.empty() || !continuousPlayback->output().failure.empty() || continuousPlayback->source->stopped())return Failure("continuous-playback-closed","Playback was stopped or replaced");
                        if(!frame.OwnsHarvester(native) || frame.GetLoadState()!=MLS_LOADED)return Failure("continuous-playback-not-ready","Wait for loaded playback");
                        if(c.command=="seek"){continuousPlayback->checkSeek(c.seconds);frame.SeekTo(REFERENCE_TIME(c.seconds*10000000.),false);}
                        if(c.command=="pause")frame.SendMessageW(WM_COMMAND,ID_PLAY_PAUSE);
                        if(c.command=="play")frame.SendMessageW(WM_COMMAND,ID_PLAY_PLAY);
                    }
                }return ContinuousStatus();
            }catch(const foxicmp::progressive::Error& e){return Failure(e.code.c_str(),e.what());}
        }
        if(action=="previewRemux" || action=="previewStatus" || action=="previewCancel" || action=="previewControl") {
            try { const auto command=foxicmp::progressive::ParsePreviewCommand(raw);
            if(command.action=="previewRemux") {
                if(preview && previewRequest==command.requestId){if(previewFingerprint!=command.fingerprint)return Failure("preview_conflict","Preview request token was reused");return PreviewStatus();}
                if(frame.GetLoadState()!=MLS_CLOSED && !command.replace)return Failure("session_busy","Playback replacement must be explicit");
                auto bytes=resourceProbes?resourceProbes->stagedRemux(command.id):nullptr;if(!bytes)return Failure("prepared_clip_not_ready","A retained prepared clip is required");
                Document metadata;metadata.Parse(resourceProbes->request(std::filesystem::path(ReaderRegistry()),"{\"version\":1,\"action\":\"resourceProbeStatus\",\"probeId\":\""+command.id+"\"}").c_str());
                if(std::string(metadata["state"].GetString())!="finished")return Failure("prepared_clip_not_ready","Prepared clip is closing");
                auto next=foxicmp::progressive::PreparedClips::create(bytes);auto data=std::make_unique<OpenFileData>();data->fi=Wide(next->uri());data->fi.SetTitle(Wide(command.title));data->title=Wide(command.title).GetString();data->bAddRecent=FALSE;
                frame.OpenMedia(std::move(data));preview=next;previewRequest=command.requestId;previewFingerprint=command.fingerprint;previewProbe=command.id;previewSession=metadata["sessionId"].GetString();previewSourceEof=metadata["media"]["remux"]["sourceEof"].GetBool();
                if(command.activate){frame.ShowWindow(SW_RESTORE);frame.SetForegroundWindow();}return PreviewStatus();
            }
            if(!preview || (command.byRequest?previewRequest!=command.id:preview->id!=command.id))return Failure("unknown_preview","Preview identifier does not match");
            const std::wstring uri=Wide(preview->uri()).GetString();
            const bool owned=!preview->cancelled.load() && (frame.GetLoadState()==MLS_LOADING || std::wstring(frame.GetCurFileName().GetString())==uri);
            if(command.action=="previewCancel"){if(owned)frame.SendMessageW(WM_COMMAND,ID_FILE_CLOSEMEDIA);preview->cancelled=true;}
            if(command.action=="previewControl"){
                if(!owned || frame.GetLoadState()!=MLS_LOADED)return Failure("preview_closed","Preview no longer owns loaded playback");
                if(command.command=="pause" && frame.GetMediaState()!=State_Paused)frame.SendMessageW(WM_COMMAND,ID_PLAY_PAUSE);
                if(command.command=="play" && frame.GetMediaState()!=State_Running)frame.SendMessageW(WM_COMMAND,ID_PLAY_PLAY);
                if(command.command=="seek"){if(command.seconds>double(frame.GetDur())/10000000.)return Failure("invalid_request","Seek exceeds prepared clip");frame.SeekTo(REFERENCE_TIME(command.seconds*10000000.),false);}
            }return PreviewStatus();
            } catch(const foxicmp::progressive::Error& e) { return Failure(e.code.c_str(),e.what()); }
        }
        if(action=="resourceProbe" || action=="resourceProbeUpdate" || action=="resourceProbeStatus" || action=="resourceProbeCancel" || action=="resourceProbeForget") {
            if(!resourceProbes)resourceProbes=std::make_unique<foxicmp::progressive::ResourceProbeManager>();
            return resourceProbes->request(std::filesystem::path(ReaderRegistry()),raw);
        }
        if(action=="readerStatus" || action=="readerRelease" || action=="readerForget" || action=="readers") {
            if(prepared && !prepared->ready) {
                try{foxicmp::progressive::ReaderCommand(raw);}catch(const foxicmp::progressive::Error& e){return Failure(e.code.c_str(),e.what());}
                if(registryPreparation && !registryPreparation->finished.load(std::memory_order_acquire))return Failure("reader_registry_busy","Another registry request is active");
                registryPreparation=prepared->shared_from_this();prepared->requested=true;prepared->readerQuery=true;prepared->descriptor=raw;prepared->registry=ReaderRegistry();
                if(action=="readerRelease")prepared->readerPlayback=continuousPlayback;
                if(action=="readerRelease" && harvester::String(d,"readerId")==readerId){auto active=source.lock();if(active && frame.OwnsHarvester(active))prepared->candidate=active;}
                return {};
            }
            auto reply=prepared?prepared->preparedReply:foxicmp::progressive::ReaderRequest(std::filesystem::path(ReaderRegistry()),raw);
            if(action=="readerRelease" && continuousPlayback && !continuousPlayback->closed && (!prepared || (!prepared->readerPass && prepared->readerPlayback==continuousPlayback))
                && continuousPlayback->holdsOutputReader(reply)){
                continuousPlayback->closed=true;if(continuousOpening)continuousOpening->abandoned.store(true);
                continuousRemux->cancelPlaybackJob(continuousPlayback->command.jobId);
                if(frame.OwnsHarvester(continuousNative.lock()))frame.SendMessageW(WM_COMMAND,ID_FILE_CLOSEMEDIA);
                // Keep the valid pre-close receipt; a fresh readerStatus must
                // establish release after concurrent native/registry cleanup.
            }
            Document result;result.Parse(reply.c_str());
            if(action=="readerRelease" && result["ok"].GetBool() && !readerId.empty() && harvester::String(d,"readerId")==readerId && (!prepared || !prepared->readerPass)) {
                auto active=source.lock();
                if(active && frame.OwnsHarvester(active) && (!prepared || prepared->candidate==active)) {
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
            try {foxicmp::progressive::Fmp4V2Bridge::CheckProfile(Encode(d["session"]),true);}
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
            CStringW probeError;std::string reason;auto candidate=CHarvesterSession::OpenPublished(Encode(descriptor),probeError,v2?ReaderRegistry():std::wstring(),&reason,false,true);
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
            if(source.lock() && frame.OwnsHarvester(source.lock()) && id==sessionId && sessionSchema==(v2?2:1) && requestedGeneration==sessionGeneration) {
                if(v2){Document details;details.Parse(source.lock()->PlaybackDetails().c_str());if(harvester::String(details,"writerInstanceId")!=harvester::String(d["session"]["writer"],"instanceId"))return Failure("generation_changed","Writer incarnation changed; use a new generation");}
                if(d.HasMember("trackReader") && d["trackReader"].GetBool() && readerId.empty())return Failure("reader_not_tracked","Existing session was opened without a reader lease");
                return Status();
            }
            if(frame.HasHarvesterPlayback() && !(d.HasMember("replace") && d["replace"].GetBool()))return Failure("session_busy","Explicit replacement is required");
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
                PlaybackIntentChanged(CStringW(),true);
                preparation=prepared->shared_from_this();
                prepared->requested=true;prepared->descriptor=Encode(descriptor);prepared->registry=v2 || (d.HasMember("trackReader") && d["trackReader"].GetBool())?ReaderRegistry():std::wstring();prepared->epoch=intentEpoch;return {};
            }
            if(prepared && !prepared->preparedReply.empty())return prepared->preparedReply;
            if(!prepared)PlaybackIntentChanged(CStringW(),true);
            CStringW error;std::string reason;auto next=prepared?prepared->candidate:CHarvesterSession::OpenPublished(Encode(descriptor),error,
                v2 || (d.HasMember("trackReader") && d["trackReader"].GetBool())?ReaderRegistry():std::wstring(),&reason,true,true);
            if(!next)return Failure(v2?reason.c_str():"source_not_ready",Utf8(error.GetString()));
            auto data=std::make_unique<OpenFileData>();data->harvesterManifest=L"published-api";data->fi=CStringW(next->Path(0).c_str());for(size_t i=1;i<next->TrackCount();++i)if(next->Snapshot().tracks[i].initializationReady)data->auds.emplace_back(next->Path(int(i)).c_str());data->bAddRecent=FALSE;
            if(!title.empty())data->fi.SetTitle(title.c_str());
            data->preparedHarvester=next;source=next;releaseReceipt=next->ReleaseReceipt();sessionId=id;sessionGeneration=next->Snapshot().generation;sessionSchema=v2?2:1;readerId=next->RegisteredReaderId();lastOutcome.clear();frame.OpenMedia(std::move(data));
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
        if(!active || !frame.OwnsHarvester(active))return (action=="status" || action=="cancel" || action=="release")?Status():Failure("session_closed","Playback was stopped or replaced");
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
            }else{CStringW error;std::string reason;if(!active->Publish(Encode(descriptor),error,&reason))return Failure(reason.empty()?"invalid_update":reason.c_str(),Utf8(error.GetString()));}
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

void ClassicProgressiveApi::PlaybackIntentChanged(const CStringW& incoming,bool retainCurrentPlayback){
    ++intentEpoch;if(preparation)preparation->abandoned.store(true);if(legacyOpening)legacyOpening->abandoned.store(true);if(preview && incoming!=Wide(preview->uri()))preview->cancelled=true;
    if(continuousPlayback && !continuousPlayback->committing && !(retainCurrentPlayback && !continuousOpening && frame.OwnsHarvester(continuousNative.lock()))){
        continuousPlayback->closed=true;if(continuousOpening)continuousOpening->abandoned.store(true);
        const auto failure=continuousPlayback->output().failure;if(!failure.empty() && failure!="cancelled")continuousPlayback->error=failure;
        const auto producerError=continuousPlayback->producerError();if(!producerError.empty() && producerError!="cancelled")continuousPlayback->error=producerError;
        if(continuousRemux)continuousRemux->cancelPlaybackJob(continuousPlayback->command.jobId);
    }
}
std::string ClassicProgressiveApi::PreviewStatus(){
    if(!preview)return Failure("unknown_preview","No prepared preview");
    const bool loaded=frame.GetLoadState()==MLS_LOADED;
    const bool owned=!preview->cancelled.load() && (!loaded || std::wstring(frame.GetCurFileName().GetString())==std::wstring(Wide(preview->uri()).GetString()));
    const char* state=!owned?"closed":!loaded?"buffering":frame.m_bGraphEventComplete?"finished":frame.GetMediaState()==State_Paused?"paused":"playing";
    Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("version",1,a);d.AddMember("ok",true,a);d.AddMember("mode","prepared-remux-clip",a);d.AddMember("previewId",Value(preview->id.c_str(),a),a);d.AddMember("requestId",Value(previewRequest.c_str(),a),a);d.AddMember("probeId",Value(previewProbe.c_str(),a),a);d.AddMember("sourceSessionId",Value(previewSession.c_str(),a),a);d.AddMember("sourceComplete",previewSourceEof,a);d.AddMember("boundedClip",true,a);d.AddMember("continuousPlayback",false,a);d.AddMember("state",Value(state,a),a);d.AddMember("active",owned && std::string(state)!="finished",a);d.AddMember("positionSeconds",loaded && owned?double(frame.GetPos())/10000000.:0.,a);d.AddMember("durationSeconds",loaded && owned?double(frame.GetDur())/10000000.:0.,a);d.AddMember("decoderBytesRead",Value(std::to_string(preview->bytesRead.load()).c_str(),a),a);d.AddMember("readerCount",preview->readers.load(),a);d.AddMember("released",preview->readers.load()==0 && (preview->cancelled.load() || std::string(state)=="finished"),a);return Encode(d);
}

bool ClassicProgressiveApi::PreparedCompleted(){
    if(continuousPlayback && frame.OwnsHarvester(continuousNative.lock())){
        if(continuousPlayback->output().complete){continuousFinished=true;frame.m_bGraphEventComplete=true;return true;}
        continuousPlayback->error="continuous-unconfirmed-eof";continuousRemux->cancelPlaybackJob(continuousPlayback->command.jobId);return false;
    }
    if(!preview || preview->cancelled.load() || std::wstring(frame.GetCurFileName().GetString())!=std::wstring(Wide(preview->uri()).GetString()))return false;
    frame.m_bGraphEventComplete=true;return true;
}

void ClassicProgressiveApi::PollContinuous(){
    const auto job=continuousOpening;if(!job)return;
    const auto ticket=continuousPlayback;
    if(job->expired() && !job->abandoned.exchange(true)){ticket->error="continuous-preparation-timeout";ticket->closed=true;continuousRemux->cancelPlaybackJob(ticket->command.jobId);}
    if(!job->finished.load(std::memory_order_acquire))return;
    continuousOpening.reset();
    if(job->abandoned.load() || ticket->closed || continuousEpoch!=intentEpoch)return;
    if(!job->candidate){ticket->error=job->error.empty()?"continuous-start-failed":job->error;ticket->closed=true;continuousRemux->cancelPlaybackJob(ticket->command.jobId);return;}
    // OpenMedia already fences nested Stop/Close with m_graphOpenEpoch. During
    // this synchronous commit its own intent notifications must not cancel it.
    // New play attempts remain rejected; explicit Stop still closes this ticket.
    ticket->committing=true;continuousNative=job->candidate;
    auto data=std::make_unique<OpenFileData>();data->harvesterManifest=L"continuous-api";data->fi=CStringW(job->candidate->Path(0).c_str());data->fi.SetTitle(Wide(ticket->command.title));data->title=Wide(ticket->command.title).GetString();data->bAddRecent=FALSE;data->preparedHarvester=job->candidate;
    frame.OpenMedia(std::move(data));ticket->committing=false;
    if(ticket->closed || !frame.OwnsHarvester(job->candidate)){
        ticket->closed=true;continuousRemux->cancelPlaybackJob(ticket->command.jobId);
        if(frame.OwnsHarvester(job->candidate))frame.SendMessageW(WM_COMMAND,ID_FILE_CLOSEMEDIA);
    }else if(ticket->command.activate){frame.ShowWindow(SW_RESTORE);frame.SetForegroundWindow();}
}
std::string ClassicProgressiveApi::ContinuousStatus(){
    if(!continuousPlayback)return Failure("unknown-continuous-playback","No continuous playback");
    const auto& p=*continuousPlayback;const auto native=continuousNative.lock();const bool owned=frame.OwnsHarvester(native),loaded=owned && frame.GetLoadState()==MLS_LOADED;const auto out=p.output();
    const auto producerError=p.producerError();
    const auto error=!p.error.empty()?p.error:!producerError.empty() && producerError!="cancelled"?producerError:!p.closed && !out.failure.empty()?out.failure:std::string();
    const char* state=!error.empty()?"error":p.closed?"closed":continuousOpening || p.committing?"opening":!owned?"closed":!loaded?"buffering":continuousFinished?"finished":!frame.m_harvesterWantPlay?"paused":frame.m_harvesterBuffering?"buffering":frame.GetMediaState()==State_Paused?"paused":"playing";
    Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("version",1,a);d.AddMember("ok",true,a);d.AddMember("mode","continuous-h264-video-v1",a);
    auto text=[&](const char* key,const std::string& value){d.AddMember(Value(key,a),Value(value.c_str(),a),a);};
    text("playbackId",p.id);text("requestId",p.command.requestId);text("jobId",p.command.jobId);text("sessionId",p.command.sessionId);text("generation",std::to_string(p.command.generation));text("writerInstanceId",p.command.writer);text("state",state);text("error",error);
    d.AddMember("active",std::string(state)=="opening" || std::string(state)=="buffering" || std::string(state)=="playing" || std::string(state)=="paused",a);
    d.AddMember("positionSeconds",loaded?double(frame.GetPos())/10000000.:0.,a);d.AddMember("durationSeconds",double(p.duration)/10000000.,a);d.AddMember("outputComplete",out.complete,a);d.AddMember("decoderReaders",uint64_t(out.activeReaders),a);
    Value ids(rapidjson::kArrayType);for(const auto& id:out.readerIds)ids.PushBack(Value(id.c_str(),a),a);d.AddMember("readerIds",ids,a);return Encode(d);
}
