// Test-only translation unit: real FFmpeg remux with targeted AVIO checkpoints.
// Never package this executable as an application worker.
#include "../tools/progressive_demux_runtime.h"
#include <fstream>
#include <thread>
#ifndef _WIN32
#include <unistd.h>
#endif
namespace {
std::filesystem::path testDirectory(){
#ifdef _WIN32
    return foxicmp::demux_worker::directory();
#else
    char bytes[65536];const auto n=readlink("/proc/self/exe",bytes,sizeof(bytes));
    if(n<=0 || size_t(n)>=sizeof(bytes))throw std::runtime_error("test-worker-path");
    return std::filesystem::path(std::string(bytes,size_t(n))).parent_path();
#endif
}
void avioCheckpoint(const char* point,uint64_t offset,const char* hex,size_t length){
    using namespace foxicmp::demux_worker;
    const auto directory=testDirectory(),control=directory/"avio-fault-control.json";
    std::ifstream input(control,std::ios::binary);if(!input)return;
    const std::string config{std::istreambuf_iterator<char>(input),{}};input.close();
    Doc d;d.Parse(config.c_str());need(!d.HasParseError() && d.IsObject(),"test-control-invalid");
#ifdef _WIN32
    const auto pid=GetCurrentProcessId();
#else
    const auto pid=getpid();
#endif
    need(d.HasMember("pid") && d["pid"].IsUint64() && d.HasMember("point") && d["point"].IsString(),"test-control-invalid");
    if(d["pid"].GetUint64()!=uint64_t(pid) || std::string(d["point"].GetString())!=point)return;
    need(length>0 && !(length%2),"test-gate-empty-io");
    std::vector<uint8_t> bytes(length/2);
    auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c-'a'+10;};
    for(size_t i=0;i<bytes.size();++i)bytes[i]=uint8_t(digit(hex[i*2])*16+digit(hex[i*2+1]));
    size_t partial=0;
    if(std::string(point)=="write"){
        const auto prefix=std::string("{\"io\":\"write\",\"offset\":\"")+std::to_string(offset)+"\",\"hex\":\"";
        partial=prefix.size()+length/2;std::cout<<prefix;std::cout.write(hex,std::streamsize(length/2));std::cout<<std::flush;need(bool(std::cout),"test-partial-output-failed");
    }
    Doc marker;marker.SetObject();auto& a=marker.GetAllocator();put(marker,"point",point);marker.AddMember("pid",uint64_t(pid),a);put(marker,"offset",std::to_string(offset));marker.AddMember("bytes",uint64_t(bytes.size()),a);marker.AddMember("partialWireBytes",uint64_t(partial),a);put(marker,"dataSha256",hash(bytes.data(),bytes.size()));marker.AddMember("insideActualFfmpegAvio",true,a);
    const auto ready=directory/"avio-fault-ready.json",temporary=directory/"avio-fault-ready.tmp";
    {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out<<encode(marker);out.close();need(bool(out),"test-marker-write");}
    std::filesystem::rename(temporary,ready);
    const auto until=Clock::now()+std::chrono::seconds(20);
    while(std::filesystem::exists(control) && Clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    throw std::runtime_error("test-gate-not-killed");
}
}
#define FOXICMP_STREAM_WORKER_TEST_CHECKPOINT(point,offset,hex,length) avioCheckpoint(point,offset,hex,length)
#include "../tools/progressive_stream_remux_worker.cpp"
