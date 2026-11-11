// Real missing-range/sample/completion waits, controlled publication boundary.
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
 static unsigned completionWaiters(CHarvesterSession& s){return s.m_completionWaiters.load();}
};
static std::vector<char> load(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);require(bool(file),"fixture open");return {std::istreambuf_iterator<char>(file),{}};}
static size_t prefix(const std::vector<char>& raw){size_t at=0;unsigned fragments=0;while(at+8<=raw.size()){uint64_t size=0;for(int i=0;i<4;++i)size=(size<<8)|uint8_t(raw[at+i]);require(size>=8 && size<=raw.size()-at,"fixture box size");const bool media=std::string(raw.data()+at+4,4)=="mdat";at+=size;if(media && ++fragments==5)return at;}throw std::runtime_error("five fragments missing");}
static bool same(const harvester::Sample& a,const harvester::Sample& b){return a.offset==b.offset && a.size==b.size && a.start==b.start && a.end==b.end && a.decode==b.decode && a.sync==b.sync;}
struct Fixture {
 std::filesystem::path root;std::shared_ptr<CHarvesterSession> source,other;
 CHarvesterFileReader* object=nullptr;CComPtr<IAsyncReader> reader;
 HANDLE breaker=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 rapidjson::Document model;std::array<std::vector<char>,2> bytes;std::array<size_t,2> visible;bool completed=false;
 std::vector<std::string> ids;
 Fixture(const std::filesystem::path& directory,const std::filesystem::path& video,const std::filesystem::path& audio):root(directory){
  std::filesystem::create_directories(root);bytes={load(video),load(audio)};for(int i=0;i<2;++i){visible[i]=prefix(bytes[i]);std::ofstream file(root/(i?"audio.part":"video.part"),std::ios::binary);file.write(bytes[i].data(),visible[i]);require(bool(file),"write initial prefix");}
  auto& d=model;d.SetObject();auto& a=d.GetAllocator();d.AddMember("schemaVersion",1,a);d.AddMember("sessionId",p::Value(p::lease_detail::RandomId().c_str(),a),a);d.AddMember("generation",1,a);d.AddMember("revision",1,a);d.AddMember("state","downloading",a);d.AddMember("duration100ns",240000000,a);d.AddMember("sessionDirectory",p::Value(root.u8string().c_str(),a),a);
  p::Value tracks(rapidjson::kObjectType);for(const char* kind:{"video","audio"}){p::Value t(rapidjson::kObjectType);const bool v=std::string(kind)=="video";t.AddMember("path",p::Value(v?"video.part":"audio.part",a),a);t.AddMember("container",p::Value(v?"mp4":"m4a",a),a);t.AddMember("fragmented",true,a);t.AddMember("initializationReady",true,a);t.AddMember("availablePrefixBytes",uint64_t(std::filesystem::file_size(root/(v?"video.part":"audio.part"))),a);t.AddMember("expectedTotalBytes",p::Value(),a);t.AddMember("state","downloading",a);tracks.AddMember(p::Value(kind,a),t,a);}d.AddMember("tracks",tracks,a);
  const auto json=p::lease_detail::Encode(d);CStringW error;source=CHarvesterSession::OpenPublished(json,error,(root/"registry").wstring());require(bool(source),"OpenPublished raw source");ClassicReadBoundaryTest::freeze(*source);other=CHarvesterSession::OpenPublished(json,error,(root/"registry").wstring());require(bool(other),"independent raw source");ClassicReadBoundaryTest::freeze(*other);
  require(!source->HasVirtualReader(),"raw fixture used virtual reader");ids={source->RegisteredReaderId(),other->RegisteredReaderId()};HRESULT hr=S_OK;object=new CHarvesterFileReader(source,0,hr);reader=object;require(SUCCEEDED(hr),"raw reader init");object->StartStreaming();object->SetBreakEvent(breaker);
 }
 void publishAll(){
  if(completed)return;
  for(int i=0;i<2;++i){std::ofstream file(root/(i?"audio.part":"video.part"),std::ios::binary|std::ios::app);file.write(bytes[i].data()+visible[i],bytes[i].size()-visible[i]);require(bool(file),"append confirmed bytes");}
  auto& a=model.GetAllocator();model["revision"].SetUint(2);model["state"].SetString("complete",a);
  for(int i=0;i<2;++i){auto& t=model["tracks"][i?"audio":"video"];t["availablePrefixBytes"].SetUint64(bytes[i].size());t["expectedTotalBytes"].SetUint64(bytes[i].size());t["state"].SetString("complete",a);}
  CStringW error;require(source->Publish(p::lease_detail::Encode(model),error),"publish complete");completed=true;
 }
 void finish(const std::vector<char>& raw){
  p::classic_read_test::waiting={};p::classic_read_test::nativeIo={};HRESULT hr=S_OK;CComPtr<IAsyncReader> independent=new CHarvesterFileReader(other,0,hr);require(SUCCEEDED(hr),"independent reader init");char bytes[64];require(independent->SyncRead(0,64,reinterpret_cast<BYTE*>(bytes))==S_OK && hash(bytes,64)==hash(raw.data(),64),"independent reader lost");independent.Release();
  auto a=source->ReleaseReceipt(),b=other->ReleaseReceipt();source->Cancel();other->Cancel();reader.Release();object=nullptr;source.reset();other.reset();
  require(p::SourceRetirement::waitEmpty(std::chrono::seconds(5)) && a->load() && b->load(),"source retirement");
  for(const auto& id:ids){rapidjson::Document d;d.Parse(p::ReaderRequest(root/"registry","{\"version\":1,\"action\":\"readerStatus\",\"readerId\":\""+id+"\"}").c_str());require(d["ok"].GetBool() && d["reader"]["released"].GetBool() && d["reader"]["filesClosed"].GetBool(),"filesClosed proof");d.Parse(p::ReaderRequest(root/"registry","{\"version\":1,\"action\":\"readerForget\",\"readerId\":\""+id+"\"}").c_str());require(d["ok"].GetBool(),"forget proof");}
  require(std::filesystem::remove(root/"video.part") && std::filesystem::remove(root/"audio.part"),"native files remained open");
 }
 ~Fixture(){if(breaker)CloseHandle(breaker);}
};
int main(int argc,char** argv)try{
 require(argc==4,"video audio work-directory");require(AfxWinInit(GetModuleHandle(nullptr),nullptr,GetCommandLine(),0)!=FALSE,"MFC init");require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM init");
 const auto video=std::filesystem::u8path(argv[1]),audio=std::filesystem::u8path(argv[2]),root=std::filesystem::u8path(argv[3]);const auto raw=load(video);rapidjson::Document report;report.SetObject();auto& a=report.GetAllocator();p::Value rows(rapidjson::kArrayType);unsigned failures=0;
 const auto run=[&](const std::string& operation,const std::string& boundary,const std::string& signal){
  const auto name=operation+"-"+boundary+"-"+signal;Fixture f(root/std::filesystem::u8path(name),video,audio);
  size_t futureIndex=0;harvester::Sample indexed;while(f.source->SampleAt(0,futureIndex,indexed)==1){require(++futureIndex<100000,"sample index bound");}
  const auto offset=boundary=="ready"?0:f.visible[0]+32;
  if((operation=="completion" && boundary=="ready") || operation=="sample-eof")f.publishAll();
  const auto index=operation=="sample-eof"?size_t(-1):boundary=="ready"?0:futureIndex;
  harvester::Sample sample{123,456,-11,-22,-33,true},sentinel=sample;std::vector<BYTE> buffer(64,0x55);const auto original=buffer;
  unsigned callbacks=0,waitObservations=0,readyObservations=0,nativeReads=0;bool fired=false,completionCounterObserved=true;
  p::classic_read_test::nativeIo=[&](void*,const char* stage,void*,void*,unsigned long){if(std::string(stage)=="submitted")++nativeReads;};
  p::classic_read_test::waiting=[&](void* owner,const char* stage){
   const std::string point=stage;require(owner==(operation.rfind("sample",0)==0?static_cast<void*>(f.object):static_cast<void*>(f.source.get())),"wrong wait owner");
   if(point.find("wait")!=std::string::npos)++waitObservations;else ++readyObservations;
   const auto target=(operation.rfind("sample",0)==0?"sample-":"range-")+std::string(boundary=="ready"?"ready":"wait");if(fired || point!=target)return;fired=true;++callbacks;
   if(operation=="completion")completionCounterObserved=ClassicReadBoundaryTest::completionWaiters(*f.source)==1;
   if(signal=="cancel")f.source->Cancel();else if(signal=="breaker")SetEvent(f.breaker);else if(signal=="flush" || signal=="pulse" || signal=="pulse-publish"){f.reader->BeginFlush();if(signal!="flush")f.reader->EndFlush();}
   if((signal=="none" || signal=="pulse-publish") && boundary=="waiting")f.publishAll();
  };
  std::mutex mutex;std::condition_variable changed;bool done=false;std::exception_ptr error;HRESULT hr=E_UNEXPECTED;bool rescue=false;const auto start=GetTickCount64();
  const auto request=[&]{return operation=="syncread"?f.reader->SyncRead(LONGLONG(offset),64,buffer.data()):operation=="completion"?f.object->WaitForCompletion():f.object->WaitForSample(index,&sample);};
  std::thread worker([&]{try{hr=request();}catch(...){error=std::current_exception();}{std::lock_guard<std::mutex> lock(mutex);done=true;}changed.notify_one();});
  {std::unique_lock<std::mutex> lock(mutex);if(!changed.wait_for(lock,std::chrono::seconds(2),[&]{return done;})){rescue=true;lock.unlock();try{f.publishAll();}catch(...){f.source->Cancel();}}}
  worker.join();if(error)std::rethrow_exception(error);const auto elapsed=GetTickCount64()-start;
  const auto expected=signal!="none"?E_ABORT:operation=="sample-eof"?S_FALSE:S_OK;const bool unchangedSample=same(sample,sentinel),unchangedBuffer=buffer==original;
  bool ok=hr==expected && fired && callbacks==1 && !rescue && completionCounterObserved && ClassicReadBoundaryTest::completionWaiters(*f.source)==0 && f.source->Error().empty() && !f.object->HasErrors();
  if(expected==E_ABORT)ok=ok && unchangedSample && unchangedBuffer && nativeReads==0;
  else if(operation=="syncread")ok=ok && nativeReads==1 && hash(reinterpret_cast<const char*>(buffer.data()),64)==hash(raw.data()+offset,64);
  else if(operation=="sample-eof")ok=ok && unchangedSample;
  else if(operation=="sample"){harvester::Sample reference;ok=ok && f.source->SampleAt(0,index,reference)==1 && same(sample,reference) && !unchangedSample;}
  const auto firstReads=nativeReads;bool recovered=false;p::classic_read_test::waiting={};p::classic_read_test::nativeIo={};
  if(signal!="none" && signal!="cancel"){f.reader->EndFlush();ResetEvent(f.breaker);f.publishAll();const auto recovery=request();recovered=recovery==(operation=="sample-eof"?S_FALSE:S_OK);ok=ok && recovered;}
  f.finish(raw);p::Value row(rapidjson::kObjectType);row.AddMember("profile",p::Value(name.c_str(),a),a);row.AddMember("result",p::Value(ok?"PASS":"FAIL",a),a);row.AddMember("hresult",int64_t(hr),a);row.AddMember("expectedHresult",int64_t(expected),a);row.AddMember("callbacks",callbacks,a);row.AddMember("waitObservations",waitObservations,a);row.AddMember("readyObservations",readyObservations,a);row.AddMember("nativeReadSubmissions",firstReads,a);row.AddMember("sampleUnchanged",unchangedSample,a);row.AddMember("readBufferUnchanged",unchangedBuffer,a);row.AddMember("completionCounterBalanced",true,a);row.AddMember("rescuePublicationRequired",rescue,a);row.AddMember("wallMilliseconds",elapsed,a);row.AddMember("recoveredAfterReset",recovered,a);row.AddMember("closedAndForgottenReaders",2,a);rows.PushBack(row,a);if(!ok)++failures;
 };
 for(const char* operation:{"syncread","completion","sample"}){
  for(const char* boundary:{"waiting","ready"})for(const char* signal:{"none","cancel","flush","breaker","pulse"})run(operation,boundary,signal);
  run(operation,"waiting","pulse-publish");
 }
 for(const char* signal:{"none","cancel","flush","breaker","pulse"})run("sample-eof","ready",signal);
 report.AddMember("result",p::Value(failures?"FAIL":"PASS",a),a);report.AddMember("count",rows.Size(),a);report.AddMember("failures",failures,a);report.AddMember("rows",rows,a);std::cout<<p::lease_detail::Encode(report)<<std::endl;return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 2;}
