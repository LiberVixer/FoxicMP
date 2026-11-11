// Actual Classic v2 resource and continuous AAC projection read boundaries.
// GPL-3.0-or-later. Private fixture only; no graph/device or installed profile.
#ifndef FOXICMP_PRIVATE_CLASSIC_READ_TEST
#error Private Classic instrumentation required
#endif
#include "filters/transform/MPCVideoDec/stdafx.h"
#include "DSUtil/Profile.h"
#include "DSUtil/HarvesterSession.h"
#include "filters/parser/BaseSplitter/AsyncReader.h"
#include "../../shared/progressivestreamdecoder.h"
#include "../../shared/progressiveretirement.h"
#include "../../shared/progressiveclassicreadtest.h"
#include <fstream>
#include <iostream>
CModApp app;
namespace p=foxicmp::progressive;
static void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static std::vector<char> load(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);require(bool(f),"input open");return {std::istreambuf_iterator<char>(f),{}};}
static std::string hash(const char* data,size_t n){p::Sha256 d;d.update(data,n);return d.hex();}
class ClassicReadBoundaryTest {
public:
 static void freeze(CHarvesterSession& s){SetEvent(s.m_stop);if(s.m_thread.joinable())s.m_thread.join();}
};
struct Inputs {
 rapidjson::Document config;std::vector<char> cache;std::array<std::vector<char>,2> v2;
 explicit Inputs(const std::filesystem::path& path){auto raw=load(path);config.Parse(raw.data(),raw.size());require(!config.HasParseError(),"input JSON");cache=load(std::filesystem::u8path(config["cache"].GetString()));for(unsigned i=0;i<2;++i)v2[i]=load(std::filesystem::u8path(config["tracks"][i].GetString()));}
};
struct Fixture {
 const Inputs& input;const bool continuous;std::filesystem::path root,registry;
 std::shared_ptr<p::StreamCacheBudget> budget=std::make_shared<p::StreamCacheBudget>();p::StreamCacheOwner owner{budget};
 std::shared_ptr<p::StreamOutputStore> store;std::shared_ptr<p::StreamDecoderSource> decoded;std::shared_ptr<p::StreamOutputStore::Reader> independent;
 std::shared_ptr<CHarvesterSession> source,other;std::array<CComPtr<IAsyncReader>,2> readers;std::array<CHarvesterFileReader*,2> objects{};std::vector<std::string> ids;
 HANDLE flush=CreateEventW(nullptr,TRUE,FALSE,nullptr),breaker=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 Fixture(const Inputs& value,const std::filesystem::path& folder,bool av):input(value),continuous(av),root(folder),registry(root/"registry"){
  std::filesystem::create_directories(root);CStringW error;
  if(continuous){store=owner.create(root/"cache",registry,p::lease_detail::RandomId(),1);store->acceptAudioWindow(p::StreamAudioWindow::Parse(input.config["window"]));for(size_t at=0;at<input.cache.size();){const auto n=(std::min)(size_t(65536),input.cache.size()-at);store->append(at,{input.cache.begin()+at,input.cache.begin()+at+n});at+=n;}
   decoded=std::make_shared<p::StreamDecoderSource>(store,"continuous-h264-aac-window-v1",true);source=CHarvesterSession::OpenContinuous(decoded,241370000,error);require(bool(source),"OpenContinuous AAC projection");ClassicReadBoundaryTest::freeze(*source);independent=decoded->reader();ids.push_back(independent->id());require(source->AllowsAacPcmWindow(1) && !source->HasV2Bridge(),"not real AAC projection");require(!store->status().complete && harvester::EndReady(source->Snapshot().tracks[1]),"audio endpoint lost in incomplete video cache");
  }else{
   for(unsigned i=0;i<2;++i){std::ofstream f(root/(i?"audio.part":"video.part"),std::ios::binary);f.write(input.v2[i].data(),input.v2[i].size());require(bool(f),"v2 track write");}
   rapidjson::Document d;d.CopyFrom(input.config["snapshot"],d.GetAllocator());d.AddMember("sessionDirectory",p::Value(root.u8string().c_str(),d.GetAllocator()),d.GetAllocator());const auto json=p::lease_detail::Encode(d);
   source=CHarvesterSession::OpenPublished(json,error,registry.wstring());require(bool(source),"OpenPublished v2");ClassicReadBoundaryTest::freeze(*source);other=CHarvesterSession::OpenPublished(json,error,registry.wstring());require(bool(other),"independent v2");ClassicReadBoundaryTest::freeze(*other);ids.push_back(other->RegisteredReaderId());require(source->HasV2Bridge() && !source->IsContinuous(),"not real v2 bridge");
  }
  ids.push_back(source->RegisteredReaderId());require(source->TrackCount()==2,"missing required track");for(unsigned i=0;i<2;++i){HRESULT hr=S_OK;objects[i]=new CHarvesterFileReader(source,int(i),hr);readers[i]=objects[i];require(SUCCEEDED(hr),"reader creation");objects[i]->StartStreaming();objects[i]->SetBreakEvent(breaker);}
 }
 void corrupt(){const auto path=continuous?store->privatePath():root/"audio.part";std::fstream f(path,std::ios::binary|std::ios::in|std::ios::out);const char bad=char((continuous?input.cache[0]:input.v2[1][0])^0x55);f.write(&bad,1);f.flush();require(bool(f),"physical corruption");}
 void finish(bool fault){
  p::classic_read_test::payload={};p::classic_read_test::returned={};auto released=source->ReleaseReceipt();source->Cancel();for(auto& r:readers)r.Release();source.reset();const auto began=GetTickCount64();while(!released->load() && GetTickCount64()-began<5000)Sleep(1);require(released->load(),"main context release");
  if(continuous){require(p::SourceRetirement::waitEmpty(std::chrono::seconds(5)),"continuous retirement pool");require(!decoded->stopped() && store->status().activeReaders==1,"independent cache lease lost");if(!fault){char data[64];require(independent->read(0,data,64).bytes==64 && hash(data,64)==hash(input.cache.data(),64),"independent cache bytes");}independent->close();store->producerClosed({},"cancelled");require(store->retire(),"cache retirement");owner.collect();require(store->status().removed && budget->bytes()==0,"cache budget leak");}
  else{char data[64];require(other->ReadV2(0,0,64,data,nullptr,nullptr)==S_OK && hash(data,64)==hash(input.v2[0].data(),64),"independent v2 video bytes");auto second=other->ReleaseReceipt();other->Cancel();other.reset();require(p::SourceRetirement::waitEmpty(std::chrono::seconds(5)) && second->load(),"independent context release");}
  for(const auto& id:ids){rapidjson::Document d;d.Parse(p::ReaderRequest(registry,"{\"version\":1,\"action\":\"readerStatus\",\"readerId\":\""+id+"\"}").c_str());require(d["ok"].GetBool() && d["reader"]["released"].GetBool() && d["reader"]["filesClosed"].GetBool(),"filesClosed proof");d.Parse(p::ReaderRequest(registry,"{\"version\":1,\"action\":\"readerForget\",\"readerId\":\""+id+"\"}").c_str());require(d["ok"].GetBool(),"forget proof");}
  for(const auto& entry:std::filesystem::directory_iterator(registry))require(entry.path().filename()==".guard","remaining reader receipts");if(!continuous)require(std::filesystem::remove(root/"video.part") && std::filesystem::remove(root/"audio.part"),"v2 file handles remained open");
 }
 ~Fixture(){if(flush)CloseHandle(flush);if(breaker)CloseHandle(breaker);}
};
int main(int argc,char** argv)try{
 require(argc==3,"config work-directory");require(AfxWinInit(GetModuleHandle(nullptr),nullptr,GetCommandLine(),0)!=FALSE,"MFC init");require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM init");Inputs input(std::filesystem::u8path(argv[1]));const auto root=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(root);rapidjson::Document report;report.SetObject();auto& a=report.GetAllocator();p::Value rows(rapidjson::kArrayType),dumps(rapidjson::kArrayType);unsigned failures=0;
 for(bool av:{false,true}){
  const std::string backend=av?"aac-projection":"v2-resources";std::array<std::vector<char>,2> expected;
  {Fixture f(input,root/(backend+"-reference"),av);for(unsigned track=0;track<2;++track){const auto length=f.source->Snapshot().tracks[track].available;require(length>131072 && length<16*1024*1024,"view byte bound");expected[track].resize(size_t(length));for(size_t at=0;at<length;){const LONG n=LONG((std::min)(size_t(65536),size_t(length)-at));require(f.readers[track]->SyncRead(at,n,reinterpret_cast<BYTE*>(expected[track].data()+at))==S_OK,"full reference read");at+=n;}const auto path=root/(backend+"-"+std::to_string(track)+".mp4");std::ofstream file(path,std::ios::binary);file.write(expected[track].data(),expected[track].size());require(bool(file),"dump write");p::Value item(rapidjson::kObjectType);item.AddMember("backend",p::Value(backend.c_str(),a),a);item.AddMember("track",track,a);item.AddMember("path",p::Value(path.u8string().c_str(),a),a);item.AddMember("sha256",p::Value(hash(expected[track].data(),expected[track].size()).c_str(),a),a);item.AddMember("bytes",uint64_t(length),a);dumps.PushBack(item,a);}f.finish(false);}
  const auto run=[&](unsigned track,bool direct,const std::string& signal,size_t length,bool first,bool fault){
   const auto name=backend+"-"+std::to_string(track)+(direct?"-readv2-":"-syncread-")+signal+"-"+std::to_string(length)+(first?"-first":"-last")+(fault?"-fault":"");Fixture f(input,root/std::filesystem::u8path(name),av);uint64_t start=0;if(fault){harvester::Sample sample;require(f.source->SampleAt(int(track),0,sample)==1 && sample.size>0,"first AAC sample");start=sample.offset;}
   unsigned readCalls=0,signals=0,failReads=0,returns=0;bool fired=false;HRESULT observed=E_UNEXPECTED;std::vector<char> data(length);
   p::classic_read_test::payload=[&](void* source,bool after,int kind,uint64_t offset,int64_t count){require(source==f.source.get() && kind==int(track),"wrong source/track callback");if(after){++readCalls;if(count<=0)++failReads;}if(fired || signal=="none" && !fault || after==fault)return;if(!first && !fault && offset+uint64_t((std::max)(int64_t(0),count))<start+length)return;fired=true;++signals;if(fault)f.corrupt();if(signal=="cancel")f.source->Cancel();else if(signal=="breaker")SetEvent(f.breaker);else if(signal!="none"){if(direct)SetEvent(f.flush);else{f.readers[track]->BeginFlush();if(signal=="pulse")f.readers[track]->EndFlush();}}};
   p::classic_read_test::returned=[&](void* reader,long result){require(reader==f.objects[track],"wrong returning reader");++returns;observed=result;};
   const auto hr=direct?f.source->ReadV2(int(track),start,length,data.data(),f.flush,f.breaker):f.readers[track]->SyncRead(start,LONG(length),reinterpret_cast<BYTE*>(data.data()));const auto error=f.source->Error();const auto expectedHr=signal!="none"?E_ABORT:fault?E_FAIL:S_OK;const auto calls=readCalls;
   bool ok=hr==expectedHr && (direct?returns==0:returns==1 && observed==hr) && (signal=="none" && !fault || fired) && (expectedHr==E_FAIL?!error.empty():error.empty());if(fault)ok=ok && failReads==1;if(!fault && first && signal!="none")ok=ok && calls==1;if(expectedHr==S_OK)ok=ok && data==std::vector<char>(expected[track].begin()+start,expected[track].begin()+start+length);
   p::classic_read_test::payload={};p::classic_read_test::returned={};bool recovered=false;if(signal!="none" && signal!="cancel" && !fault){if(direct)ResetEvent(f.flush);else f.readers[track]->EndFlush();ResetEvent(f.breaker);char next[64];recovered=(direct?f.source->ReadV2(int(track),0,64,next,f.flush,f.breaker):f.readers[track]->SyncRead(0,64,reinterpret_cast<BYTE*>(next)))==S_OK && hash(next,64)==hash(expected[track].data(),64);ok=ok && recovered;}
   f.finish(fault);p::Value row(rapidjson::kObjectType);row.AddMember("profile",p::Value(name.c_str(),a),a);row.AddMember("result",p::Value(ok?"PASS":"FAIL",a),a);row.AddMember("hresult",int64_t(hr),a);row.AddMember("expectedHresult",int64_t(expectedHr),a);row.AddMember("readCalls",calls,a);row.AddMember("failedPayloadReads",failReads,a);row.AddMember("signalCallbacks",signals,a);row.AddMember("sourceError",p::Value(error.c_str(),a),a);row.AddMember("recoveredAfterReset",recovered,a);row.AddMember("closedAndForgottenGroupReaders",2,a);row.AddMember("registryLeasesEmpty",true,a);rows.PushBack(row,a);if(!ok)++failures;
  };
  for(unsigned track=0;track<2;++track)for(unsigned shape=0;shape<3;++shape)for(bool direct:{false,true})for(const char* signal:{"none","cancel","flush","breaker","pulse"}){if(direct && std::string(signal)=="pulse")continue;run(track,direct,signal,shape?131072:64,shape==2,false);}
  for(bool direct:{false,true})for(const char* signal:{"none","flush","breaker"})run(1,direct,signal,64,true,true);
 }
 report.AddMember("result",p::Value(failures?"FAIL":"PASS",a),a);report.AddMember("count",rows.Size(),a);report.AddMember("failures",failures,a);report.AddMember("rows",rows,a);report.AddMember("dumps",dumps,a);std::cout<<p::lease_detail::Encode(report)<<std::endl;return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 2;}
