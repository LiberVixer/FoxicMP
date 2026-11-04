// Child transport failure and cancellation tests; never opens a user media file.
#include "../shared/progressiveprocess.h"
#include <cstdlib>
#include <iostream>
#include <sstream>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#else
#include <dirent.h>
#endif
using foxicmp::progressive::process_detail::Process;
using Clock=std::chrono::steady_clock;
static void environment(const char* mode){
#ifdef _WIN32
 SetEnvironmentVariableA("FOXICMP_TRANSPORT_TEST_CHILD",mode);
#else
 if(mode)setenv("FOXICMP_TRANSPORT_TEST_CHILD",mode,1);else unsetenv("FOXICMP_TRANSPORT_TEST_CHILD");
#endif
}
static unsigned handles(){
#ifdef _WIN32
 DWORD n=0;if(!GetProcessHandleCount(GetCurrentProcess(),&n))throw std::runtime_error("handle count failed");return n;
#else
 auto* d=opendir("/proc/self/fd");if(!d)throw std::runtime_error("fd count failed");unsigned n=0;while(readdir(d))++n;closedir(d);return n;
#endif
}
static void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int main(int argc,char** argv){
#ifdef _WIN32
 _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
 if(const auto* mode=std::getenv("FOXICMP_TRANSPORT_TEST_CHILD")){
  const std::string m=mode;
  if(m=="exit")return 0;
  if(m=="nonzero")return 17;
#ifdef _WIN32
  if(m=="job"){JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};const auto flags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_PROCESS_MEMORY;
   if(!QueryInformationJobObject(nullptr,JobObjectExtendedLimitInformation,&limits,sizeof(limits),nullptr) || (limits.BasicLimitInformation.LimitFlags&flags)!=flags || limits.ProcessMemoryLimit!=SIZE_T(1024)*1024*1024)return 18;
   std::cout<<"job-limited"<<std::endl;return 0;}
#endif
  if(m=="flood"){std::cout<<std::string(150000,'x')<<std::endl;return 0;}
  if(m=="reply"){std::string line;std::getline(std::cin,line);std::cout<<"ok:"<<line<<std::endl;return 0;}
  std::this_thread::sleep_for(std::chrono::seconds(30));return 0;
 }
 try{const auto executable=std::filesystem::absolute(argv[0]);unsigned checks=0;std::thread warmup([]{});warmup.join();
  std::cout<<"Warming up child transport"<<std::endl;
  {std::atomic<bool> cancel{false};environment("reply");Process p(executable,cancel);environment(nullptr);p.send("hello");require(p.receive()=="ok:hello","warmup reply mismatch");}
  {std::atomic<bool> cancel{false};try{Process p(executable.parent_path()/"missing-test-worker",cancel);}catch(const std::exception&){};}
  const auto before=handles();
  std::cout<<"Testing 20 request/reply cycles"<<std::endl;
  for(int i=0;i<20;++i){std::atomic<bool> cancel{false};environment("reply");{Process p(executable,cancel);environment(nullptr);p.send("hello");require(p.receive()=="ok:hello","normal reply mismatch");}}++checks;
  for(const auto* mode:{"receive-cancel","send-cancel","deadline","exit","flood"}){
   std::cout<<"Testing "<<mode<<std::endl;
   std::atomic<bool> cancel{false};environment(std::string(mode)=="flood"?"flood":std::string(mode)=="exit"?"exit":"stall");const auto start=Clock::now();std::string error;
   {Process p(executable,cancel);environment(nullptr);std::thread stopper;
    if(std::string(mode).find("cancel")!=std::string::npos)stopper=std::thread([&]{std::this_thread::sleep_for(std::chrono::milliseconds(100));cancel=true;});
    try{if(std::string(mode)=="send-cancel")p.send(std::string(139999,'x'));else p.receive();}catch(const std::exception& e){error=e.what();}
    if(stopper.joinable())stopper.join();
   }
   const auto elapsed=std::chrono::duration<double>(Clock::now()-start).count();
   if(std::string(mode).find("cancel")!=std::string::npos){require(error=="cancelled","cancellation not propagated");require(elapsed<2,"cancellation too slow");}
   else if(std::string(mode)=="deadline"){require(error=="backend-time-limit","deadline missing");require(elapsed>=3.5 && elapsed<6,"deadline out of bounds");}
   else if(std::string(mode)=="flood")require(error=="backend-frame-limit","oversized child frame accepted");
   else require(error=="backend-worker-exited" || error=="backend-pipe-error","child exit not propagated");
   std::cout<<mode<<": "<<error<<" "<<elapsed<<" seconds, handles="<<handles()<<" baseline="<<before<<std::endl;++checks;
  }
  {std::atomic<bool> cancel{false};environment("nonzero");Process p(executable,cancel);environment(nullptr);require(p.finish()==17,"child exit code was lost");++checks;}
  {std::atomic<bool> cancel{false};environment("stall");std::string error;const auto began=Clock::now();
   {Process p(executable,cancel,{std::chrono::seconds(10),std::chrono::milliseconds(100)});environment(nullptr);try{p.receive();}catch(const std::exception& e){error=e.what();}}
   require(error=="backend-operation-time-limit" && std::chrono::duration<double>(Clock::now()-began).count()<2,"operation deadline failed");++checks;}
  {std::atomic<bool> cancel{false};environment("reply");Process p(executable,cancel,{std::chrono::seconds(10),std::chrono::seconds(1)});environment(nullptr);
   std::this_thread::sleep_for(std::chrono::milliseconds(4200));p.send("after snapshot deadline");require(p.receive()=="ok:after snapshot deadline" && p.finish()==0,"stream lifetime used snapshot deadline");++checks;}
  {std::atomic<bool> cancel{false};std::string error;try{Process p(executable,cancel,{std::chrono::hours(2),std::chrono::seconds(1)});}catch(const std::exception& e){error=e.what();}
   require(error=="backend-invalid-limits","unbounded worker lifetime accepted");++checks;}
#ifdef _WIN32
  {std::atomic<bool> cancel{false};environment("job");Process p(executable,cancel);environment(nullptr);require(p.receive()=="job-limited" && p.finish()==0,"worker Job limits missing");++checks;}
#endif
  for(int i=0;i<20;++i){std::atomic<bool> cancel{false};try{Process p(executable.parent_path()/"missing-test-worker",cancel);require(false,"missing worker accepted");}catch(const std::exception& e){require(std::string(e.what())=="backend-worker-unavailable","wrong missing worker error");}}++checks;
  std::cout<<"After 20 failed spawns: handles="<<handles()<<" baseline="<<before<<std::endl;
  require(handles()==before,"child transport leaked handles");++checks;
  std::cout<<"PASS "<<checks<<" transport checks\n";return 0;
 }catch(const std::exception& e){environment(nullptr);std::cerr<<e.what()<<std::endl;return 1;}
}
