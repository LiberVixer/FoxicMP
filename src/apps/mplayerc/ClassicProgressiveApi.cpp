#include "stdafx.h"
#include "ClassicProgressiveApi.h"
#include "MainFrm.h"
#include "mplayerc.h"
#include "ExtLib/rapidjson/include/rapidjson/writer.h"
#include "ExtLib/rapidjson/include/rapidjson/stringbuffer.h"
#include <sddl.h>
#include <initializer_list>

namespace {
using rapidjson::Document;
using rapidjson::Value;
std::string Utf8(const std::wstring& text) {
    int size=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(size,'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),out.data(),size,nullptr,nullptr);return out;
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
    Document d;d.Parse(R"({"version":1,"transport":"windows-pipe-json-lines","edition":"classic","profileSelectable":false,"maxRequestBytes":65536,"heartbeatSeconds":5,"disconnectAfterSeconds":15,"maxTrackPathChars":240,"actions":["capabilities","start","update","complete","heartbeat","status","cancel","error"],"containers":["fast-start-mp4","fragmented-mp4"],"videoCodecs":["h264"],"audioCodecs":["aac-lc"],"separateTracksRequired":true,"initializedTracksRequired":true,"startBufferSeconds":10,"resumeBufferSeconds":5,"pauseBufferSeconds":1})");
    auto& a=d.GetAllocator();d.AddMember("endpoint",Value(Utf8(Endpoint()).c_str(),a),a);d.AddMember("profile",Value(Utf8(ProfileDirectory()).c_str(),a),a);return Encode(d);
}
ClassicProgressiveApi::ClassicProgressiveApi(CMainFrame& frame):frame(frame) {
    stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(stop)worker=std::thread([this]{Serve();});
}
ClassicProgressiveApi::~ClassicProgressiveApi() {if(stop){SetEvent(stop);if(worker.joinable())worker.join();CloseHandle(stop);}}
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
        CloseHandle(connected.hEvent);if(!ok)break;
        std::string input;char bytes[4096];DWORD count=0;
        const auto deadline=GetTickCount64()+5000;
        while(input.size()<=65536 && input.find('\n')==std::string::npos && GetTickCount64()<deadline &&
              Transfer(pipe,stop,bytes,sizeof(bytes),count,false,DWORD(deadline-GetTickCount64())) && count)input.append(bytes,count);
        std::string output;
        if(input.empty() || input.size()>65536 || input.find('\n')!=input.size()-1)output=Failure("invalid_request","One JSON line of at most 65536 bytes is required");
        else {
            auto request=std::make_shared<Request>();request->input=std::move(input);
            auto* message=new std::shared_ptr<Request>(request);
            if(!frame.PostMessageW(WM_CLASSIC_PROGRESSIVE_REQUEST,0,reinterpret_cast<LPARAM>(message)))delete message;
            else {HANDLE events[]={stop,request->done};if(WaitForMultipleObjects(2,events,FALSE,5000)==WAIT_OBJECT_0+1)output=request->output;}
        }
        if(!output.empty()) {
            output+='\n';DWORD written=0;
            Transfer(pipe,stop,output.data(),DWORD(output.size()),written,true);
        }
        // No FlushFileBuffers: an unresponsive client must never block shutdown.
        DisconnectNamedPipe(pipe);
    }
    CloseHandle(pipe);
}
void ClassicProgressiveApi::Ended(const std::shared_ptr<CHarvesterSession>& ended,bool natural) {
    if(ended!=source.lock())return;
    Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("version",1,a);d.AddMember("ok",true,a);d.AddMember("active",false,a);
    d.AddMember("sessionId",Value(sessionId.c_str(),a),a);const auto error=ended->Error();
    d.AddMember("state",Value(error.empty()?(natural?"finished":"stopped"):"error",a),a);d.AddMember("error",Value(error.c_str(),a),a);lastOutcome=Encode(d);
}
std::string ClassicProgressiveApi::Status() {
    auto session=source.lock();
    if(!session || frame.m_harvester!=session) {
        if(!lastOutcome.empty())return lastOutcome;
        return Failure("session_closed","Playback was stopped or replaced");
    }
    const auto error=session->Error();const auto snapshot=session->Snapshot();
    const char* state=!error.empty()?"error":frame.m_bGraphEventComplete?"finished":!session->Connected()?"waiting-downloader":
        !frame.m_harvesterStarted || frame.m_harvesterBuffering?"buffering":!frame.m_harvesterWantPlay?"paused":"playing";
    Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("version",1,a);d.AddMember("ok",true,a);d.AddMember("active",true,a);
    d.AddMember("sessionId",Value(sessionId.c_str(),a),a);d.AddMember("state",Value(state,a),a);d.AddMember("error",Value(error.c_str(),a),a);
    d.AddMember("connected",session->Connected(),a);d.AddMember("complete",session->Complete(),a);
    d.AddMember("positionSeconds",double(frame.GetPos())/10000000.,a);d.AddMember("availableSeconds",double(session->ReadyEnd())/10000000.,a);
    d.AddMember("durationSeconds",double(snapshot.duration)/10000000.,a);return Encode(d);
}
std::string ClassicProgressiveApi::Handle(const std::string& raw) {
    try {
        Document d;d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(raw.data(),raw.size());
        if(d.HasParseError() || !d.IsObject())return Failure("invalid_request","Invalid JSON");
        harvester::UniqueKeys(d);
        if(harvester::Number(d,"version")!=1)return Failure("invalid_request","Expected version 1");
        const auto action=harvester::String(d,"action");
        if(action=="capabilities") {
            if(!Keys(d,{"version","action"}))return Failure("invalid_request","Unexpected fields");
            Document info;info.Parse(Information().c_str());info.RemoveMember("endpoint");info.RemoveMember("profile");
            auto& a=info.GetAllocator();info.AddMember("ok",true,a);info.AddMember("pid",uint32_t(GetCurrentProcessId()),a);return Encode(info);
        }
        if(action=="start") {
            if(!Keys(d,{"version","action","session"},{"replace","activate","title"}) || !d["session"].IsObject() ||
                (d.HasMember("replace") && !d["replace"].IsBool()) || (d.HasMember("activate") && !d["activate"].IsBool()) ||
                (d.HasMember("title") && (!d["title"].IsString() || d["title"].GetStringLength()>4096)))return Failure("invalid_request","Invalid start request");
            const auto id=harvester::String(d["session"],"sessionId");
            if(source.lock() && source.lock()==frame.m_harvester && id==sessionId)return Status();
            if(frame.m_harvester && !(d.HasMember("replace") && d["replace"].GetBool()))return Failure("session_busy","Explicit replacement is required");
            CStringW error;auto next=CHarvesterSession::OpenPublished(Encode(d["session"]),error);
            if(!next)return Failure("source_not_ready",Utf8(error.GetString()));
            frame.SendMessageW(WM_COMMAND,ID_FILE_CLOSEMEDIA);
            auto data=std::make_unique<OpenFileData>();data->harvesterManifest=L"published-api";data->fi=CStringW(next->Path(0).c_str());data->auds.emplace_back(next->Path(1).c_str());data->bAddRecent=FALSE;
            if(d.HasMember("title")) {
                const auto text=harvester::String(d,"title");int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
                std::wstring title(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),title.data(),size);
                for(auto& c:title)if(c<32 || c==127)c=L' ';data->fi.SetTitle(title.c_str());
            }
            frame.m_harvester=next;frame.m_harvesterStarted=false;frame.m_harvesterBuffering=true;frame.m_harvesterWantPlay=true;source=next;sessionId=id;lastOutcome.clear();frame.OpenMedia(std::move(data));
            if(!d.HasMember("activate") || d["activate"].GetBool()){frame.ShowWindow(SW_RESTORE);frame.SetForegroundWindow();}
            return Status();
        }
        const bool update=action=="update" || action=="complete", failure=action=="error";
        if(action!="status" && action!="cancel" && action!="heartbeat" && !update && !failure)return Failure("invalid_request","Unknown action");
        if(!Keys(d,{"version","action","sessionId"},update?std::initializer_list<const char*>{"session"}:failure?std::initializer_list<const char*>{"message"}:std::initializer_list<const char*>{}) ||
            harvester::String(d,"sessionId")!=sessionId || sessionId.empty())return Failure("unknown_session","Unknown session identifier");
        auto active=source.lock();
        if(!active || frame.m_harvester!=active)return action=="status" || action=="cancel"?Status():Failure("session_closed","Playback was stopped or replaced");
        if(update) {
            if(!d.HasMember("session") || !d["session"].IsObject() || (action=="complete" && harvester::String(d["session"],"state")!="complete"))return Failure("invalid_request","Expected a session snapshot");
            CStringW error;if(!active->Publish(Encode(d["session"]),error))return Failure("invalid_update",Utf8(error.GetString()));
        }else if(action=="heartbeat")active->PublishedHeartbeat();
        else if(action=="cancel") {
            frame.SendMessageW(WM_COMMAND,ID_FILE_CLOSEMEDIA);Document result;result.Parse(lastOutcome.c_str());
            if(result.IsObject()){result["state"].SetString("cancelled",result.GetAllocator());lastOutcome=Encode(result);}return Status();
        }else if(failure) {
            const auto message=harvester::String(d,"message");if(message.empty() || message.size()>4096)return Failure("invalid_request","Expected a short error message");active->Fail(message.c_str());
        }
        return Status();
    }catch(const std::exception& e){return Failure("invalid_request",e.what());}
}
