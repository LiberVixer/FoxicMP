// Real Classic raw-file adapter + real pending Windows pipe I/O.
// Private pipe handle substitution is NOT a disk-driver or public API test.
// GPL-3.0-or-later.
#ifndef FOXICMP_PRIVATE_CLASSIC_READ_TEST
#error Private Classic instrumentation required
#endif
#include "filters/transform/MPCVideoDec/stdafx.h"
#include "DSUtil/Profile.h"
#include "DSUtil/HarvesterSession.h"
#include "filters/parser/BaseSplitter/AsyncReader.h"
#include "../../shared/progressivereaderlease.h"
#include "../../shared/progressivesha256.h"
#include "../../shared/progressiveretirement.h"
#include "../../shared/progressiveclassicreadtest.h"
#include <fstream>
#include <iostream>
#include <condition_variable>
CModApp app;
namespace p=foxicmp::progressive;
static void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static std::string hash(const char* data,size_t n){p::Sha256 d;d.update(data,n);return d.hex();}
class ClassicReadBoundaryTest {
public:
 static void freeze(CHarvesterSession& s){SetEvent(s.m_stop);if(s.m_thread.joinable())s.m_thread.join();}
 static void pipe(CHarvesterSession& s,HANDLE h){require(!s.HasVirtualReader(),"not raw file branch");CloseHandle(s.m_files[0]);s.m_files[0]=h;}
};
struct Fixture {
 std::filesystem::path root;std::shared_ptr<CHarvesterSession> source,other;
 CHarvesterFileReader* object=nullptr;CComPtr<IAsyncReader> reader;
 HANDLE breaker=CreateEventW(nullptr,TRUE,FALSE,nullptr),writer=INVALID_HANDLE_VALUE;
 std::vector<std::string> ids;
 Fixture(const std::filesystem::path& directory,const std::filesystem::path& video,const std::filesystem::path& audio):root(directory){
  std::filesystem::create_directories(root);std::filesystem::copy_file(video,root/"video.part");std::filesystem::copy_file(audio,root/"audio.part");
  rapidjson::Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("schemaVersion",1,a);d.AddMember("sessionId",p::Value(p::lease_detail::RandomId().c_str(),a),a);d.AddMember("generation",1,a);d.AddMember("revision",1,a);d.AddMember("state","downloading",a);d.AddMember("duration100ns",240000000,a);d.AddMember("sessionDirectory",p::Value(root.u8string().c_str(),a),a);
  p::Value tracks(rapidjson::kObjectType);for(const char* kind:{"video","audio"}){p::Value t(rapidjson::kObjectType);const bool v=std::string(kind)=="video";t.AddMember("path",p::Value(v?"video.part":"audio.part",a),a);t.AddMember("container",p::Value(v?"mp4":"m4a",a),a);t.AddMember("fragmented",true,a);t.AddMember("initializationReady",true,a);t.AddMember("availablePrefixBytes",uint64_t(std::filesystem::file_size(root/(v?"video.part":"audio.part"))),a);t.AddMember("expectedTotalBytes",p::Value(),a);t.AddMember("state","downloading",a);tracks.AddMember(p::Value(kind,a),t,a);}d.AddMember("tracks",tracks,a);
  const auto json=p::lease_detail::Encode(d);CStringW error;source=CHarvesterSession::OpenPublished(json,error,(root/"registry").wstring());require(bool(source),"OpenPublished raw source");ClassicReadBoundaryTest::freeze(*source);other=CHarvesterSession::OpenPublished(json,error,(root/"registry").wstring());require(bool(other),"independent raw source");ClassicReadBoundaryTest::freeze(*other);
  require(!source->HasVirtualReader(),"raw fixture used virtual reader");ids={source->RegisteredReaderId(),other->RegisteredReaderId()};HRESULT hr=S_OK;object=new CHarvesterFileReader(source,0,hr);reader=object;require(SUCCEEDED(hr),"raw reader init");object->StartStreaming();object->SetBreakEvent(breaker);
 }
 void pendingPipe(){
  const auto name=L"\\\\.\\pipe\\FoxicMP-private-native-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
  HANDLE server=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_INBOUND|FILE_FLAG_OVERLAPPED,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,0,nullptr);require(server!=INVALID_HANDLE_VALUE,"pipe server");
  OVERLAPPED connection{};connection.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);require(connection.hEvent!=nullptr,"connect event");BOOL connected=ConnectNamedPipe(server,&connection);require(connected || GetLastError()==ERROR_IO_PENDING,"connect pending");
  writer=CreateFileW(name.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);require(writer!=INVALID_HANDLE_VALUE,"pipe writer");DWORD bytes=0;require(GetOverlappedResult(server,&connection,&bytes,TRUE)!=FALSE,"connect completion");CloseHandle(connection.hEvent);ClassicReadBoundaryTest::pipe(*source,server);
 }
 void write(const std::vector<char>& raw,size_t n){DWORD count=0;require(WriteFile(writer,raw.data(),DWORD(n),&count,nullptr)!=FALSE && count==n,"pipe write");}
 void finish(const std::vector<char>& raw){
  p::classic_read_test::nativeIo={};HRESULT hr=S_OK;CComPtr<IAsyncReader> independent=new CHarvesterFileReader(other,0,hr);require(SUCCEEDED(hr),"independent reader init");char bytes[64];require(independent->SyncRead(0,64,reinterpret_cast<BYTE*>(bytes))==S_OK && hash(bytes,64)==hash(raw.data(),64),"independent reader lost");independent.Release();
  auto a=source->ReleaseReceipt(),b=other->ReleaseReceipt();source->Cancel();other->Cancel();reader.Release();object=nullptr;source.reset();other.reset();if(writer!=INVALID_HANDLE_VALUE){CloseHandle(writer);writer=INVALID_HANDLE_VALUE;}
  require(p::SourceRetirement::waitEmpty(std::chrono::seconds(5)) && a->load() && b->load(),"source retirement");
  for(const auto& id:ids){rapidjson::Document d;d.Parse(p::ReaderRequest(root/"registry","{\"version\":1,\"action\":\"readerStatus\",\"readerId\":\""+id+"\"}").c_str());require(d["ok"].GetBool() && d["reader"]["released"].GetBool() && d["reader"]["filesClosed"].GetBool(),"filesClosed proof");d.Parse(p::ReaderRequest(root/"registry","{\"version\":1,\"action\":\"readerForget\",\"readerId\":\""+id+"\"}").c_str());require(d["ok"].GetBool(),"forget proof");}
  require(std::filesystem::remove(root/"video.part") && std::filesystem::remove(root/"audio.part"),"native files remained open");
 }
 ~Fixture(){if(writer!=INVALID_HANDLE_VALUE)CloseHandle(writer);if(breaker)CloseHandle(breaker);}
};
int main(int argc,char** argv)try{
 require(argc==4,"video audio work-directory");require(AfxWinInit(GetModuleHandle(nullptr),nullptr,GetCommandLine(),0)!=FALSE,"MFC init");require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM init");
 const auto video=std::filesystem::u8path(argv[1]),audio=std::filesystem::u8path(argv[2]),root=std::filesystem::u8path(argv[3]);std::ifstream input(video,std::ios::binary);const std::vector<char> raw{std::istreambuf_iterator<char>(input),{}};require(raw.size()>131072,"video fixture length");rapidjson::Document result;result.SetObject();auto& a=result.GetAllocator();p::Value rows(rapidjson::kArrayType);unsigned failures=0;
 const auto run=[&](bool pipe,const std::string& signal,const std::string& point,uint64_t offset,LONG size,bool truncated){
  const auto name=std::string(pipe?"pipe-":"file-")+signal+"-"+point+"-"+std::to_string(offset)+"-"+std::to_string(size)+(truncated?"-truncated":"");Fixture f(root/std::filesystem::u8path(name),video,audio);if(pipe)f.pendingPipe();if(truncated)std::filesystem::resize_file(f.root/"video.part",64);
  std::vector<char> bytes(size);unsigned pending=0,waits=0,settled=0,cancelled=0,callbacks=0;DWORD terminalError=ERROR_SUCCESS;bool operationCompleted=false,fired=false,recovered=false;
  p::classic_read_test::nativeIo=[&](void* reader,const char* stage,void* file,void* ptr,unsigned long error){
   require(reader==f.object,"wrong native reader");const auto step=std::string(stage);auto* ov=static_cast<OVERLAPPED*>(ptr);if(step=="submitted" && error==ERROR_IO_PENDING)++pending;if(step=="waiting")++waits;
   if(step=="settled" || step=="cancelled"){++settled;if(step=="cancelled")++cancelled;terminalError=error;DWORD n=0;const BOOL done=GetOverlappedResult(file,ov,&n,FALSE);operationCompleted=done || GetLastError()!=ERROR_IO_INCOMPLETE;}
   if(step!=point || fired)return;fired=true;++callbacks;
   if(signal=="cancel" || signal=="complete-cancel")f.source->Cancel();
   else if(signal=="flush" || signal=="broken-flush" || signal=="pulse"){f.reader->BeginFlush();if(signal=="pulse")f.reader->EndFlush();}
   else if(signal=="breaker")SetEvent(f.breaker);
   if(pipe){if(signal=="none" || signal=="complete-cancel" || signal=="short")f.write(raw,signal=="short"?32:64);else if(signal=="broken" || signal=="broken-flush"){CloseHandle(f.writer);f.writer=INVALID_HANDLE_VALUE;}}
  };
  std::mutex mutex;std::condition_variable changed;bool done=false;std::exception_ptr failure;HRESULT hr=E_UNEXPECTED;std::atomic<bool> rescue{false};const auto start=GetTickCount64();
  std::thread worker([&]{try{hr=f.reader->SyncRead(LONGLONG(offset),size,reinterpret_cast<BYTE*>(bytes.data()));}catch(...){failure=std::current_exception();}{std::lock_guard<std::mutex> lock(mutex);done=true;}changed.notify_one();});
  {std::unique_lock<std::mutex> lock(mutex);if(!changed.wait_for(lock,std::chrono::seconds(2),[&]{return done;})){rescue=true;lock.unlock();if(pipe)f.write(raw,64);else CancelIoEx(f.source->File(0),nullptr);}}
  worker.join();if(failure)std::rethrow_exception(failure);const auto elapsed=GetTickCount64()-start;
  const bool aborted=signal=="cancel" || signal=="flush" || signal=="breaker" || signal=="pulse" || signal=="complete-cancel" || signal=="broken-flush";
  const bool bad=truncated || signal=="short" || signal=="broken";const auto expected=aborted?E_ABORT:bad?E_FAIL:S_OK;const auto sourceError=f.source->Error();const auto readerError=f.object->HasErrors();
  bool ok=hr==expected && fired && callbacks==1 && operationCompleted && !rescue.load() && (pipe?pending==1:true) && (aborted || !bad?sourceError.empty() && !readerError:!sourceError.empty() && readerError);
  if(expected==S_OK)ok=ok && hash(bytes.data(),bytes.size())==hash(raw.data()+offset,bytes.size());
  p::classic_read_test::nativeIo={};if(aborted && signal!="cancel" && signal!="complete-cancel" && signal!="broken-flush" && !truncated){f.reader->EndFlush();ResetEvent(f.breaker);if(pipe)f.write(raw,64);char again[64];recovered=f.reader->SyncRead(0,64,reinterpret_cast<BYTE*>(again))==S_OK && hash(again,64)==hash(raw.data(),64);ok=ok && recovered;}
  f.finish(raw);p::Value row(rapidjson::kObjectType);row.AddMember("profile",p::Value(name.c_str(),a),a);row.AddMember("result",p::Value(ok?"PASS":"FAIL",a),a);row.AddMember("hresult",int64_t(hr),a);row.AddMember("expectedHresult",int64_t(expected),a);row.AddMember("pendingReads",pending,a);row.AddMember("waitCalls",waits,a);row.AddMember("completionObservations",settled,a);row.AddMember("cancelledPaths",cancelled,a);row.AddMember("terminalOsError",unsigned(terminalError),a);row.AddMember("operationCompletedBeforeReturn",operationCompleted,a);row.AddMember("rescueWriteRequired",rescue.load(),a);row.AddMember("wallMilliseconds",elapsed,a);row.AddMember("sourceError",p::Value(sourceError.c_str(),a),a);row.AddMember("readerError",readerError,a);row.AddMember("recoveredAfterReset",recovered,a);row.AddMember("closedAndForgottenReaders",2,a);rows.PushBack(row,a);if(!ok)++failures;
 };
 for(auto shape:{std::pair<uint64_t,LONG>{0,64},{65536,64},{0,131072}})for(const char* signal:{"none","cancel","flush","breaker","pulse"})run(false,signal,"settled",shape.first,shape.second,false);
 for(const char* signal:{"none","cancel","flush","breaker","pulse"})run(false,signal,"settled",0,131072,true);
 for(const char* point:{"submitted","waiting"})for(const char* signal:{"none","cancel","flush","breaker","pulse","complete-cancel","short","broken","broken-flush"})run(true,signal,point,0,64,false);
 result.AddMember("result",p::Value(failures?"FAIL":"PASS",a),a);result.AddMember("count",rows.Size(),a);result.AddMember("failures",failures,a);result.AddMember("rows",rows,a);std::cout<<p::lease_detail::Encode(result)<<std::endl;return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 2;}
