// Actual Classic CHarvesterFileReader/ReadV2 fixture, separate portable profile.
// GPL-3.0-or-later. Never link the normal HarvesterSession/AsyncReader objects.
#ifndef FOXICMP_PRIVATE_CLASSIC_READ_TEST
#error Private Classic read instrumentation required
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
class ClassicReadBoundaryTest {
public:
 static void freezeWatcher(CHarvesterSession& s){SetEvent(s.m_stop);if(s.m_thread.joinable())s.m_thread.join();}
};
static void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static std::string hash(const char* data,size_t n){p::Sha256 d;d.update(data,n);return d.hex();}
static std::string readerProof(const std::filesystem::path& registry,const std::string& id){return p::ReaderRequest(registry,"{\"version\":1,\"action\":\"readerStatus\",\"readerId\":\""+id+"\"}");}
struct Fixture {
 std::shared_ptr<p::StreamCacheBudget> budget=std::make_shared<p::StreamCacheBudget>();p::StreamCacheOwner owner{budget};
 std::shared_ptr<p::StreamOutputStore> store;std::shared_ptr<p::StreamDecoderSource> decoded;
 std::shared_ptr<CHarvesterSession> source;CHarvesterFileReader* native=nullptr;CComPtr<IAsyncReader> reader;
 std::shared_ptr<p::StreamOutputStore::Reader> independent;std::filesystem::path registry;std::vector<std::string> ids;
 HANDLE flush=CreateEventW(nullptr,TRUE,FALSE,nullptr),breaker=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 Fixture(const std::filesystem::path& root,const std::vector<char>& raw):registry(root/"registry"){
  std::filesystem::create_directories(root);store=owner.create(root/"cache",registry,p::lease_detail::RandomId(),1);
  for(size_t at=0;at<raw.size();){const auto n=(std::min)(size_t(65536),raw.size()-at);store->append(at,{raw.begin()+at,raw.begin()+at+n});at+=n;}
  decoded=std::make_shared<p::StreamDecoderSource>(store);CStringW error;source=CHarvesterSession::OpenContinuous(decoded,240000000,error);require(bool(source),"OpenContinuous failed");
  ClassicReadBoundaryTest::freezeWatcher(*source);ids.push_back(source->RegisteredReaderId());independent=decoded->reader();ids.push_back(independent->id());
  HRESULT hr=S_OK;native=new CHarvesterFileReader(source,0,hr);reader=native;require(SUCCEEDED(hr),"reader construction");native->StartStreaming();native->SetBreakEvent(breaker);
 }
 void finish(bool corrupt,const std::vector<char>& raw){
  p::classic_read_test::payload={};p::classic_read_test::returned={};auto released=source->ReleaseReceipt();source->Cancel();reader.Release();native=nullptr;source.reset();
  require(p::SourceRetirement::waitEmpty(std::chrono::seconds(5)) && released->load(),"Classic context not released");require(!decoded->stopped(),"preview close cancelled shared source");
  require(!store->status().removed && store->status().activeReaders==1,"independent lease lost");
  if(!corrupt){char bytes[64];require(independent->read(0,bytes,64).bytes==64 && hash(bytes,64)==hash(raw.data(),64),"independent reader data lost");}
  independent->close();store->producerClosed({},"cancelled");require(store->retire(),"cache did not retire");owner.collect();require(budget->bytes()==0 && store->status().removed,"cache budget leaked");
  for(const auto& id:ids){rapidjson::Document d;d.Parse(readerProof(registry,id).c_str());require(d["ok"].GetBool() && d["reader"]["released"].GetBool() && d["reader"]["filesClosed"].GetBool(),"missing filesClosed proof");
   const auto reply=p::ReaderRequest(registry,"{\"version\":1,\"action\":\"readerForget\",\"readerId\":\""+id+"\"}");d.Parse(reply.c_str());require(d["ok"].GetBool(),"forget failed");}
 }
 ~Fixture(){if(flush)CloseHandle(flush);if(breaker)CloseHandle(breaker);}
};
int main(int argc,char** argv)try{
 require(argc==3,"input.fmp4 work-directory");require(AfxWinInit(GetModuleHandle(nullptr),nullptr,GetCommandLine(),0)!=FALSE,"MFC init");require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM init");
 std::ifstream file(std::filesystem::u8path(argv[1]),std::ios::binary);require(bool(file),"input open");const std::vector<char> raw{std::istreambuf_iterator<char>(file),{}};require(raw.size()>131072,"fixture too short");
 const auto root=std::filesystem::u8path(argv[2]);rapidjson::Document report;report.SetObject();auto& a=report.GetAllocator();p::Value rows(rapidjson::kArrayType);unsigned failures=0;
 const auto run=[&](bool direct,const std::string& signal,size_t length,bool first,bool fault){
  const std::string name=std::string(direct?"readv2-":"syncread-")+signal+"-"+std::to_string(length)+(first?"-first":"-last")+(fault?"-fault":"");Fixture f(root/std::filesystem::u8path(name),raw);
  unsigned reads=0,callbacks=0,returns=0;bool fired=false;HRESULT observed=E_UNEXPECTED;std::vector<char> bytes(length);
  const auto interrupt=[&]{
   if(signal=="cancel")f.source->Cancel();
   else if(signal=="breaker")SetEvent(f.breaker);
   else if(direct)SetEvent(f.flush);
   else {require(SUCCEEDED(f.reader->BeginFlush()),"BeginFlush failed");if(signal=="pulse")require(SUCCEEDED(f.reader->EndFlush()),"EndFlush failed");}
  };
  p::classic_read_test::payload=[&](void* source,bool after,int track,uint64_t offset,int64_t count){
   if(source!=f.source.get())return;require(track==0,"wrong track");if(after)++reads;
   if(signal=="none" || fired || after==fault)return;
   if(!first && !fault && offset+uint64_t((std::max)(int64_t(0),count))<length)return;
   fired=true;++callbacks;
   if(fault){std::fstream cache(f.store->privatePath(),std::ios::binary|std::ios::in|std::ios::out);const char bad=char(raw[0]^0x55);cache.write(&bad,1);cache.flush();require(bool(cache),"cache fault injection");}
   interrupt();
  };
  p::classic_read_test::returned=[&](void* reader,long result){require(reader==f.native,"wrong reader callback");observed=result;++returns;};
  const auto hr=direct?f.source->ReadV2(0,0,length,bytes.data(),f.flush,f.breaker):f.reader->SyncRead(0,LONG(length),reinterpret_cast<BYTE*>(bytes.data()));
  const auto error=f.source->Error();const auto cacheError=f.store->status().failure;const bool expectedAbort=signal!="none";
  const bool rightCallback=direct?returns==0:returns==1 && observed==hr;
  bool ok=rightCallback && (expectedAbort?hr==E_ABORT:hr==S_OK) && error.empty() && (!expectedAbort || fired) && (!fault || cacheError=="cache-integrity-mismatch");
  if(!expectedAbort)ok=ok && hash(bytes.data(),bytes.size())==hash(raw.data(),bytes.size());
  const auto readCalls=reads;p::classic_read_test::payload={};p::classic_read_test::returned={};bool recovered=false;
  if(expectedAbort && signal!="cancel" && !fault){if(direct)ResetEvent(f.flush);else require(SUCCEEDED(f.reader->EndFlush()),"end flush recovery");ResetEvent(f.breaker);char next[64];
   const auto recoveredHr=direct?f.source->ReadV2(0,0,64,next,f.flush,f.breaker):f.reader->SyncRead(0,64,reinterpret_cast<BYTE*>(next));recovered=recoveredHr==S_OK && hash(next,64)==hash(raw.data(),64);ok=ok && recovered;}
  if(expectedAbort && first && !fault)ok=ok && readCalls==1;
  f.finish(fault,raw);p::Value row(rapidjson::kObjectType);row.AddMember("profile",p::Value(name.c_str(),a),a);row.AddMember("result",p::Value(ok?"PASS":"FAIL",a),a);
  row.AddMember("hresult",int64_t(hr),a);row.AddMember("expectedHresult",int64_t(expectedAbort?E_ABORT:S_OK),a);row.AddMember("readCalls",readCalls,a);row.AddMember("signalCallbacks",callbacks,a);row.AddMember("readerReturnCallbacks",returns,a);
  row.AddMember("sourceError",p::Value(error.c_str(),a),a);row.AddMember("cacheError",p::Value(cacheError.c_str(),a),a);row.AddMember("recoveredAfterReset",recovered,a);row.AddMember("closedAndForgottenReaders",2,a);rows.PushBack(row,a);if(!ok)++failures;
 };
 for(unsigned shape=0;shape<3;++shape)for(bool direct:{false,true})for(const auto* signal:{"none","cancel","flush","breaker","pulse"}){
  if(direct && std::string(signal)=="pulse")continue;run(direct,signal,shape?131072:64,shape==2,false);
 }
 for(bool direct:{false,true})for(const auto* signal:{"flush","breaker"})run(direct,signal,64,true,true);
 report.AddMember("result",p::Value(failures?"FAIL":"PASS",a),a);report.AddMember("count",rows.Size(),a);report.AddMember("failures",failures,a);report.AddMember("rows",rows,a);report.AddMember("fixtureSha256",p::Value(hash(raw.data(),raw.size()).c_str(),a),a);std::cout<<p::lease_detail::Encode(report)<<std::endl;return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 2;}
