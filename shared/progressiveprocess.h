// Bounded private worker transport. No shell, inherited user file handles or source paths.
// GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif
namespace foxicmp { namespace progressive { namespace process_detail {
inline std::filesystem::path ExecutableDirectory(){
#ifdef _WIN32
 std::vector<wchar_t> b(32768);const auto n=GetModuleFileNameW(nullptr,b.data(),DWORD(b.size()));if(!n || n>=b.size())throw std::runtime_error("backend-worker-path-error");return std::filesystem::path(std::wstring(b.data(),n)).parent_path();
#else
 std::vector<char> b(65536);const auto n=readlink("/proc/self/exe",b.data(),b.size());if(n<=0 || size_t(n)>=b.size())throw std::runtime_error("backend-worker-path-error");return std::filesystem::path(std::string(b.data(),n)).parent_path();
#endif
}
class Process {
 using Clock=std::chrono::steady_clock;
 std::atomic<bool>& cancelled;Clock::time_point deadline=Clock::now()+std::chrono::seconds(4);std::string pending;
#ifdef _WIN32
 HANDLE process=nullptr,job=nullptr,input=nullptr,output=nullptr;
 static void close(HANDLE& h){if(h && h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=nullptr;}
#else
 pid_t pid=-1;int input=-1,output=-1;
 static void close(int& fd){if(fd>=0)::close(fd);fd=-1;}
#endif
 void check(){if(cancelled.load())throw std::runtime_error("cancelled");if(Clock::now()>=deadline)throw std::runtime_error("backend-time-limit");}
 bool running(){
#ifdef _WIN32
  return WaitForSingleObject(process,0)==WAIT_TIMEOUT;
#else
  int status=0;const auto result=waitpid(pid,&status,WNOHANG);if(result==pid){pid=-1;return false;}return result==0;
#endif
 }
 void stop() noexcept {
#ifdef _WIN32
  if(job)TerminateJobObject(job,1);else if(process)TerminateProcess(process,1);
  if(process)WaitForSingleObject(process,2000);close(input);close(output);close(process);close(job);
#else
  if(pid>0){kill(-pid,SIGKILL);kill(pid,SIGKILL);while(waitpid(pid,nullptr,0)<0 && errno==EINTR){}pid=-1;}close(input);close(output);
#endif
 }
public:
 Process(const std::filesystem::path& executable,std::atomic<bool>& cancel):cancelled(cancel){
#ifdef _WIN32
  HANDLE childInput=nullptr,childOutput=nullptr,nullError=INVALID_HANDLE_VALUE,thread=nullptr;
  LPPROC_THREAD_ATTRIBUTE_LIST attrs=nullptr;bool attrsReady=false;std::vector<unsigned char> storage;
  try{SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
   // A duplicated synchronous read handle cannot be used to peek at a pipe:
   // PeekNamedPipe can wait behind the child's pending ReadFile. Only the
   // parent's write end is overlapped, so cancellation never depends on reads.
   static std::atomic<unsigned long> sequence{0};
   const auto pipeName=L"\\\\.\\pipe\\FoxicMP-demux-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence);
   input=CreateNamedPipeW(pipeName.c_str(),PIPE_ACCESS_OUTBOUND|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,nullptr);
   if(input==INVALID_HANDLE_VALUE)throw std::runtime_error("backend-pipe-error");
   childInput=CreateFileW(pipeName.c_str(),GENERIC_READ,0,&sa,OPEN_EXISTING,0,nullptr);
   if(childInput==INVALID_HANDLE_VALUE)throw std::runtime_error("backend-pipe-error");
   OVERLAPPED connection{};connection.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
   if(!connection.hEvent)throw std::runtime_error("backend-pipe-error");
   const BOOL connected=ConnectNamedPipe(input,&connection);const DWORD connectError=connected?ERROR_SUCCESS:GetLastError();
   if(!connected && connectError==ERROR_IO_PENDING){CancelIoEx(input,&connection);DWORD ignored=0;GetOverlappedResult(input,&connection,&ignored,TRUE);}
   CloseHandle(connection.hEvent);
   if(!connected && connectError!=ERROR_PIPE_CONNECTED)throw std::runtime_error("backend-pipe-error");
   if(!CreatePipe(&output,&childOutput,&sa,65536))throw std::runtime_error("backend-pipe-error");SetHandleInformation(output,HANDLE_FLAG_INHERIT,0);
   nullError=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);if(nullError==INVALID_HANDLE_VALUE)throw std::runtime_error("backend-pipe-error");
   SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);storage.resize(size);attrs=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());if(!InitializeProcThreadAttributeList(attrs,1,0,&size))throw std::runtime_error("backend-spawn-error");attrsReady=true;
   HANDLE handles[]{childInput,childOutput,nullError};if(!UpdateProcThreadAttribute(attrs,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr))throw std::runtime_error("backend-spawn-error");
   STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=childInput;startup.StartupInfo.hStdOutput=childOutput;startup.StartupInfo.hStdError=nullError;startup.lpAttributeList=attrs;
   PROCESS_INFORMATION info{};std::wstring command=L"\""+executable.wstring()+L"\"";
   job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_PROCESS_MEMORY;limits.ProcessMemoryLimit=SIZE_T(1024)*1024*1024;
   if(!job || !SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("backend-spawn-error");
   if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,executable.parent_path().c_str(),&startup.StartupInfo,&info))throw std::runtime_error("backend-worker-unavailable");
   process=info.hProcess;thread=info.hThread;if(!AssignProcessToJobObject(job,process))throw std::runtime_error("backend-spawn-error");if(ResumeThread(thread)==DWORD(-1))throw std::runtime_error("backend-spawn-error");
   close(thread);close(childInput);close(childOutput);close(nullError);DeleteProcThreadAttributeList(attrs);attrs=nullptr;
  }catch(...){if(process)TerminateProcess(process,1);close(thread);close(childInput);close(childOutput);close(nullError);if(attrsReady && attrs)DeleteProcThreadAttributeList(attrs);stop();throw;}
#else
  int in[2]{-1,-1},out[2]{-1,-1},nul=-1;posix_spawn_file_actions_t actions;posix_spawnattr_t attributes;bool haveActions=false,haveAttributes=false;
  auto normalize=[](int& fd){if(fd>=0 && fd<3){int next=fcntl(fd,F_DUPFD_CLOEXEC,3);::close(fd);fd=next;}};
  try{if(pipe2(in,O_CLOEXEC) || pipe2(out,O_CLOEXEC))throw std::runtime_error("backend-pipe-error");for(auto* fd:{&in[0],&in[1],&out[0],&out[1]}){normalize(*fd);if(*fd<0)throw std::runtime_error("backend-pipe-error");}
   nul=open("/dev/null",O_WRONLY|O_CLOEXEC);normalize(nul);if(nul<0)throw std::runtime_error("backend-pipe-error");
   if(posix_spawn_file_actions_init(&actions))throw std::runtime_error("backend-spawn-error");haveActions=true;
   if(posix_spawnattr_init(&attributes))throw std::runtime_error("backend-spawn-error");haveAttributes=true;
   if(posix_spawn_file_actions_adddup2(&actions,in[0],0) || posix_spawn_file_actions_adddup2(&actions,out[1],1) || posix_spawn_file_actions_adddup2(&actions,nul,2) || posix_spawn_file_actions_addclosefrom_np(&actions,3) || posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP) || posix_spawnattr_setpgroup(&attributes,0))throw std::runtime_error("backend-spawn-error");
   auto name=executable.string();char* argv[]{name.data(),nullptr};if(posix_spawn(&pid,name.c_str(),&actions,&attributes,argv,environ)){pid=-1;throw std::runtime_error("backend-worker-unavailable");}
   input=in[1];in[1]=-1;output=out[0];out[0]=-1;
   if(fcntl(input,F_SETFL,fcntl(input,F_GETFL)|O_NONBLOCK) || fcntl(output,F_SETFL,fcntl(output,F_GETFL)|O_NONBLOCK))throw std::runtime_error("backend-pipe-error");
   for(auto* fd:{&in[0],&in[1],&out[0],&out[1],&nul})close(*fd);posix_spawn_file_actions_destroy(&actions);posix_spawnattr_destroy(&attributes);
  }catch(...){for(auto* fd:{&in[0],&in[1],&out[0],&out[1],&nul})close(*fd);if(haveActions)posix_spawn_file_actions_destroy(&actions);if(haveAttributes)posix_spawnattr_destroy(&attributes);stop();throw;}
#endif
 }
 ~Process(){stop();}
 Process(const Process&)=delete;Process& operator=(const Process&)=delete;
 void send(const std::string& line){if(line.size()>140000)throw std::runtime_error("backend-frame-limit");const auto bytes=line+"\n";size_t at=0;
#ifndef _WIN32
  // Block SIGPIPE in this worker thread only; EPIPE is an ordinary child failure.
  sigset_t set,old;sigemptyset(&set);sigaddset(&set,SIGPIPE);pthread_sigmask(SIG_BLOCK,&set,&old);
  struct Restore{sigset_t set,old;~Restore(){timespec zero{};while(sigtimedwait(&set,nullptr,&zero)>=0){}pthread_sigmask(SIG_SETMASK,&old,nullptr);}} restore{set,old};
#endif
  while(at<bytes.size()){check();if(!running())throw std::runtime_error("backend-worker-exited");
#ifdef _WIN32
   OVERLAPPED op{};op.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!op.hEvent)throw std::runtime_error("backend-pipe-error");
   DWORD n=0;bool pendingWrite=false;
   try{const auto count=DWORD((std::min)(size_t(4096),bytes.size()-at));
    if(!WriteFile(input,bytes.data()+at,count,&n,&op)){
     if(GetLastError()!=ERROR_IO_PENDING)throw std::runtime_error("backend-pipe-error");pendingWrite=true;
     while(WaitForSingleObject(op.hEvent,20)==WAIT_TIMEOUT)check();
     if(!GetOverlappedResult(input,&op,&n,FALSE))throw std::runtime_error("backend-pipe-error");pendingWrite=false;
    }
    if(!n)throw std::runtime_error("backend-pipe-error");at+=n;CloseHandle(op.hEvent);
   }catch(...){if(pendingWrite){CancelIoEx(input,&op);DWORD ignored=0;GetOverlappedResult(input,&op,&ignored,TRUE);}CloseHandle(op.hEvent);throw;}
#else
   const auto n=write(input,bytes.data()+at,(std::min)(size_t(4096),bytes.size()-at));if(n>0){at+=size_t(n);continue;}if(n<0 && errno==EINTR)continue;if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)){pollfd p{input,POLLOUT,0};poll(&p,1,20);continue;}throw std::runtime_error("backend-pipe-error");
#endif
  }
 }
 std::string receive(){for(;;){check();const auto end=pending.find('\n');if(end!=std::string::npos){if(end>140000)throw std::runtime_error("backend-frame-limit");auto line=pending.substr(0,end);pending.erase(0,end+1);return line;}if(pending.size()>140000)throw std::runtime_error("backend-frame-limit");char bytes[4096];
#ifdef _WIN32
   DWORD available=0;if(!PeekNamedPipe(output,nullptr,0,nullptr,&available,nullptr)){if(!running())throw std::runtime_error("backend-worker-exited");throw std::runtime_error("backend-pipe-error");}if(available){DWORD n=0;if(!ReadFile(output,bytes,(std::min)(DWORD(sizeof(bytes)),available),&n,nullptr) || !n)throw std::runtime_error("backend-pipe-error");pending.append(bytes,n);continue;}if(!running())throw std::runtime_error("backend-worker-exited");std::this_thread::sleep_for(std::chrono::milliseconds(5));
#else
   const auto n=read(output,bytes,sizeof(bytes));if(n>0){pending.append(bytes,size_t(n));continue;}if(n<0 && errno==EINTR)continue;if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)){pollfd p{output,POLLIN,0};poll(&p,1,20);continue;}throw std::runtime_error("backend-worker-exited");
#endif
  }}
};
}}}
