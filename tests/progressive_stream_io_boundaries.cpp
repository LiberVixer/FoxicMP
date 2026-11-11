// Standalone header-only fixture. Never link its instrumented definitions with
// normal player/decoder objects: FOXICMP_PRIVATE_STREAM_IO_TEST is target-local.
#ifndef FOXICMP_PRIVATE_STREAM_IO_TEST
#error The private stream I/O fixture requires its target-local definition
#endif
#include "../shared/progressivestreamdecoder.h"
#include <future>
#include <fstream>
#include <iostream>
namespace p=foxicmp::progressive;
using namespace std::chrono_literals;
static unsigned checks=0;
static void check(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
static std::string digest(const char* data,size_t n){p::Sha256 s;s.update(data,n);return s.hex();}
struct Gate {
 std::mutex mutex;std::condition_variable cv;unsigned phase;bool entered=false,opened=false,timedOut=false;
 explicit Gate(unsigned at):phase(at){}
 void hit(unsigned at){
  if(at!=phase)return;std::unique_lock<std::mutex> lock(mutex);if(entered)return;
  entered=true;cv.notify_all();
  if(!cv.wait_for(lock,5s,[&]{return opened;})){timedOut=true;throw std::runtime_error("private I/O gate deadline");}
 }
 void wait(){std::unique_lock<std::mutex> lock(mutex);check(cv.wait_for(lock,5s,[&]{return entered;}),"gate not reached");}
 void open(){std::lock_guard<std::mutex> lock(mutex);opened=true;cv.notify_all();}
 ~Gate(){open();}
};
template<class T> static T get(std::future<T>& f){check(f.wait_for(5s)==std::future_status::ready,"operation deadline");return f.get();}
static bool receipt(const std::filesystem::path& registry,const std::string& id){
 const auto reply=p::ReaderRequest(registry,"{\"version\":1,\"action\":\"readerStatus\",\"readerId\":\""+id+"\"}");
 rapidjson::Document d;d.Parse(reply.c_str());check(d.IsObject() && d.HasMember("ok") && d["ok"].GetBool(),"receipt unavailable");
 const auto& r=d["reader"];check(r["released"].GetBool()==r["filesClosed"].GetBool(),"release without close proof");return r["released"].GetBool();
}
struct Fixture {
 std::shared_ptr<p::StreamCacheBudget> budget=std::make_shared<p::StreamCacheBudget>();
 p::StreamCacheOwner owner{budget};std::shared_ptr<p::StreamOutputStore> store;
 std::shared_ptr<p::StreamDecoderSource> source;std::shared_ptr<p::StreamDecoderCursor> cursor;
 std::shared_ptr<p::StreamOutputStore::Reader> independent;
 std::filesystem::path registry;std::vector<std::string> ids;std::vector<int64_t> readReturns;const std::vector<char>& raw;
 Fixture(const std::filesystem::path& root,const std::vector<char>& bytes,size_t prefix):registry(root/"registry"),raw(bytes){
  std::filesystem::create_directories(root);
  store=owner.create(root/"cache",registry,p::lease_detail::RandomId(),1);append(0,prefix);
  check(store->status().fragments>0 && store->status().visible>0,"fixture has no published fragment");
  source=std::make_shared<p::StreamDecoderSource>(store);cursor=std::make_shared<p::StreamDecoderCursor>(source);ids.push_back(cursor->id());
 }
 void append(size_t at,size_t end){while(at<end){const auto n=(std::min)(size_t(65536),end-at);store->append(at,std::vector<char>(raw.begin()+at,raw.begin()+at+n));at+=n;}}
 int64_t read(char* out,uint64_t count){const auto result=cursor->read(out,count);readReturns.push_back(result);return result;}
 void complete(){store->producerClosed({},"",digest(raw.data(),raw.size()));check(store->status().complete,"complete fixture rejected");}
 void extraReader(){independent=source->reader();ids.push_back(independent->id());}
 void finish(){
  p::stream_cache_detail::privateIoBoundaryHook={};cursor->close();if(independent)independent->close();source->cancel();
  store->producerClosed({},"cancelled");check(store->retire(),"cache still owned after close");owner.collect();
  check(store->status().removed && budget->bytes()==0 && !std::filesystem::exists(store->privatePath()),"cache not removed/budget leaked");
  for(const auto& id:ids){check(receipt(registry,id),"reader still owns files");
   const auto reply=p::ReaderRequest(registry,"{\"version\":1,\"action\":\"readerForget\",\"readerId\":\""+id+"\"}");
   rapidjson::Document d;d.Parse(reply.c_str());check(d["ok"].GetBool(),"forget failed");}
 }
};
int main(int argc,char** argv)try{
 check(argc==4,"fixture work-directory first-fragment-end");
 std::ifstream input(std::filesystem::u8path(argv[1]),std::ios::binary);check(bool(input),"fixture open");
 const std::vector<char> raw{std::istreambuf_iterator<char>(input),{}};const auto prefix=size_t(std::stoull(argv[3]));
 check(prefix>256 && prefix<raw.size() && prefix<65536,"fixture prefix geometry");const auto root=std::filesystem::u8path(argv[2]);
 rapidjson::Document report;report.SetObject();auto& a=report.GetAllocator();p::Value rows(rapidjson::kArrayType);
 const auto run=[&](const std::string& name,unsigned phase,auto body){
  const auto before=checks;Fixture f(root/std::filesystem::u8path(name),raw,name=="publication-after-read"?prefix:raw.size());
  Gate gate(phase);p::stream_cache_detail::privateIoBoundaryHook=[&](unsigned at){gate.hit(at);};
  body(f,gate);check(!gate.timedOut,"gate timed out");
  p::Value row(rapidjson::kObjectType);p::Value returns(rapidjson::kArrayType);for(auto result:f.readReturns)returns.PushBack(result,a);
  row.AddMember("readReturns",returns,a);row.AddMember("deliveredBytes",f.cursor->bytes(),a);row.AddMember("eofCount",f.cursor->eofCount(),a);
  row.AddMember("cursorError",p::Value(f.cursor->error().c_str(),a),a);const auto status=f.store->status();
  row.AddMember("cacheFailure",p::Value(status.failure.c_str(),a),a);row.AddMember("activeReadersBeforeCleanup",uint64_t(status.activeReaders),a);
  row.AddMember("completeBeforeCleanup",status.complete,a);f.finish();row.AddMember("closedAndForgottenReaders",uint64_t(f.ids.size()),a);
  row.AddMember("profile",p::Value(name.c_str(),a),a);row.AddMember("phase",phase,a);row.AddMember("checks",checks-before,a);row.AddMember("result","PASS",a);rows.PushBack(row,a);
 };
 for(unsigned phase=1;phase<=4;++phase)for(unsigned action=0;action<4;++action){
  const std::string name=std::string(action==0?"interrupt":action==1?"cursor-close":action==2?"source-close-readers":"seek-serialization")+"-"+std::to_string(phase);
  run(name,phase,[&](Fixture& f,Gate& gate){
   if(action==2)f.extraReader();std::vector<char> bytes(65536);auto read=std::async(std::launch::async,[&]{return f.read(bytes.data(),64);});gate.wait();
   check(f.cursor->bytes()==0 && f.cursor->eofCount()==0 && f.cursor->error().empty(),"status blocked/stale before return");
   check(!receipt(f.registry,f.ids[0]),"in-flight file already released");std::future<int64_t> operation;
   if(action==0)f.cursor->interrupt();
   else if(action==1){
    operation=std::async(std::launch::async,[&]{f.cursor->close();return int64_t(1);});
    const auto token=f.cursor->interruptToken().lock();const auto deadline=std::chrono::steady_clock::now()+5s;
    while(!token->load() && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();check(token->load(),"close did not interrupt");
   }else if(action==2){f.source->cancel();operation=std::async(std::launch::async,[&]{f.source->closeCancelledDecoderReaders();return int64_t(1);});}
   else operation=std::async(std::launch::async,[&]{return f.cursor->seek(128);});
   if(action==2 && phase==4){check(get(operation)==1,"close after physical I/O failed");check(receipt(f.registry,f.ids[0]),"closed file has no proof");}
   else if(operation.valid()){check(operation.wait_for(0s)!=std::future_status::ready,"operation overtook in-flight read");check(!receipt(f.registry,f.ids[0]),"proof before native read completed");}
   gate.open();const auto n=get(read);check(n==(action==3?64:-1),"stale read returned data/false EOF");check(f.cursor->eofCount()==0,"interruption became EOF");
   if(operation.valid())check(get(operation)==(action==3?128:1),"serialized operation failed");
   if(action==3){check(f.read(bytes.data(),64)==64,"read after seek failed");check(digest(bytes.data(),64)==digest(raw.data()+128,64) && f.cursor->bytes()==128,"old read reset new position");}
   else check(f.cursor->bytes()==0,"aborted bytes counted as delivered");
   if(action==2){check(!receipt(f.registry,f.ids[1]),"independent reader was closed");check(f.independent->read(0,bytes.data(),64).bytes==64 && digest(bytes.data(),64)==digest(raw.data(),64),"independent reader unusable");}
  });
 }
 for(bool cancel:{false,true})run(cancel?"cancel-confirmed-eof":"confirmed-eof-control",4,[&](Fixture& f,Gate& gate){
  p::stream_cache_detail::privateIoBoundaryHook={};f.complete();check(f.cursor->seek(int64_t(raw.size()))==int64_t(raw.size()),"seek EOF");
  p::stream_cache_detail::privateIoBoundaryHook=[&](unsigned at){gate.hit(at);};char byte;auto read=std::async(std::launch::async,[&]{return f.read(&byte,1);});gate.wait();if(cancel)f.cursor->interrupt();gate.open();check(get(read)==(cancel?-1:0),"EOF/cancel ordering");check(f.cursor->eofCount()==(cancel?0u:1u),"false/missing EOF count");
 });
 run("cancel-unpublished-tail",4,[&](Fixture& f,Gate& gate){
  check(f.cursor->seek(int64_t(f.store->status().visible))>=0,"tail seek");char byte;auto read=std::async(std::launch::async,[&]{return f.read(&byte,1);});gate.wait();f.cursor->interrupt();gate.open();check(get(read)==-1 && f.cursor->eofCount()==0,"waiting became EOF");
 });
 for(unsigned phase=1;phase<=3;++phase)run("corruption-"+std::to_string(phase),phase,[&](Fixture& f,Gate& gate){
  std::vector<char> bytes(64);auto read=std::async(std::launch::async,[&]{return f.read(bytes.data(),64);});gate.wait();
  {std::fstream file(f.store->privatePath(),std::ios::binary|std::ios::in|std::ios::out);check(bool(file),"tamper open");const char changed=char(raw[0]^0x55);file.write(&changed,1);file.flush();check(bool(file),"tamper write");}
  gate.open();const auto n=get(read);
  if(phase==1)check(n==-1 && f.cursor->bytes()==0,"corrupt bytes delivered");
  else{check(n==64 && digest(bytes.data(),64)==digest(raw.data(),64),"verified snapshot changed after read");check(f.cursor->seek(0)==0 && f.read(bytes.data(),64)==-1,"later corruption not detected");}
  check(f.cursor->error()=="cache-integrity-mismatch" && f.cursor->eofCount()==0,"integrity failure became EOF");
 });
 run("truncate-before-native-read",1,[&](Fixture& f,Gate& gate){
  char byte;auto read=std::async(std::launch::async,[&]{return f.read(&byte,1);});gate.wait();std::filesystem::resize_file(f.store->privatePath(),0);gate.open();check(get(read)==-1 && f.cursor->eofCount()==0,"short read became EOF");
#ifdef _WIN32
  check(f.cursor->error()=="resource-io-error","unexpected Windows short-read error");
#else
  check(f.cursor->error()=="cache-io-error","unexpected POSIX short-read error");
#endif
 });
 run("publication-after-read",4,[&](Fixture& f,Gate& gate){
  const auto visible=f.store->status().visible;std::vector<char> bytes(65536);auto read=std::async(std::launch::async,[&]{return f.read(bytes.data(),bytes.size());});gate.wait();
  f.append(prefix,raw.size());const auto grown=f.store->status().visible;check(grown>visible && !f.store->status().complete,"no growing publication");gate.open();const auto n=get(read);
  check(n==int64_t(visible) && digest(bytes.data(),size_t(n))==digest(raw.data(),size_t(n)),"old read used future publication");
  const auto next=f.read(bytes.data(),bytes.size());check(next==int64_t(grown-visible) && digest(bytes.data(),size_t(next))==digest(raw.data()+visible,size_t(next)),"new prefix/position mismatch");
  check(f.cursor->eofCount()==0,"publication became EOF");
 });
 run("zero-length-probe",4,[&](Fixture& f,Gate&){char byte;check(f.read(&byte,0)==0 && f.cursor->bytes()==0 && f.cursor->eofCount()==0,"empty probe counted as EOF");});
 report.AddMember("result","PASS",a);report.AddMember("count",rows.Size(),a);report.AddMember("checks",checks,a);report.AddMember("rows",rows,a);report.AddMember("fixtureSha256",p::Value(digest(raw.data(),raw.size()).c_str(),a),a);
 std::cout<<p::lease_detail::Encode(report)<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
