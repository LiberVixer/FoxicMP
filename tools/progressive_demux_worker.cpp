// FFmpeg demux through bounded parent-owned AVIO; no source paths or URLs. GPL-3.0-or-later.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include "ExtLib/rapidjson/include/rapidjson/document.h"
#include "ExtLib/rapidjson/include/rapidjson/stringbuffer.h"
#include "ExtLib/rapidjson/include/rapidjson/writer.h"
#include "../shared/progressivesha256.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/mem.h>
}
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <dlfcn.h>
#include <sys/resource.h>
#endif
namespace {
using Doc=rapidjson::Document;using Value=rapidjson::Value;using Clock=std::chrono::steady_clock;
void need(bool yes,const char* reason){if(!yes)throw std::runtime_error(reason);}
std::string encode(const Value& v){rapidjson::StringBuffer b;rapidjson::Writer<rapidjson::StringBuffer> w(b);v.Accept(w);return {b.GetString(),b.GetSize()};}
void put(Doc& d,const char* k,const std::string& v){d.AddMember(Value(k,d.GetAllocator()),Value(v.c_str(),rapidjson::SizeType(v.size()),d.GetAllocator()),d.GetAllocator());}
std::string hash(const uint8_t* p,size_t n){foxicmp::progressive::Sha256 s;s.update(p,n);return s.hex();}
std::filesystem::path directory(){
#ifdef _WIN32
 std::vector<wchar_t> b(32768);auto n=GetModuleFileNameW(nullptr,b.data(),DWORD(b.size()));need(n && n<b.size(),"worker-path-error");return std::filesystem::path(std::wstring(b.data(),n)).parent_path();
#else
 return {};
#endif
}
class Library {
#ifdef _WIN32
 HMODULE module=nullptr;
#else
 void* module=nullptr;
#endif
public:
 Library(const char* base,int major){
#ifdef _WIN32
 auto path=directory()/(std::string(base)+"-"+std::to_string(major)+".dll");module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
 auto path="lib"+std::string(base)+".so."+std::to_string(major);module=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
#endif
 need(module!=nullptr,"backend-library-unavailable");}
 // This worker handles one request. Keep FFmpeg/dependency registries loaded
 // until process exit: unloading their DSOs invalidates global cleanup roots.
 ~Library()=default;
 Library(const Library&)=delete;Library& operator=(const Library&)=delete;
 template<class T>T symbol(const char* name){
#ifdef _WIN32
 auto p=GetProcAddress(module,name);
#else
 auto p=dlsym(module,name);
#endif
 need(p!=nullptr,"backend-symbol-unavailable");return reinterpret_cast<T>(p);}
};
struct Api {
 Library util{"avutil",LIBAVUTIL_VERSION_MAJOR},codec{"avcodec",LIBAVCODEC_VERSION_MAJOR},format{"avformat",LIBAVFORMAT_VERSION_MAJOR};
#define FN(lib,name) decltype(&::name) name=lib.symbol<decltype(&::name)>(#name)
 FN(util,avutil_version);FN(codec,avcodec_version);FN(format,avformat_version);FN(format,avformat_configuration);
 FN(util,av_dict_set);FN(util,av_dict_free);FN(util,av_malloc);FN(util,av_free);FN(util,av_log_set_level);FN(util,av_get_media_type_string);
 FN(format,avio_alloc_context);FN(format,avio_context_free);FN(format,avformat_alloc_context);FN(format,avformat_free_context);
 FN(format,av_find_input_format);FN(format,avformat_open_input);FN(format,avformat_close_input);FN(format,avformat_find_stream_info);FN(format,av_read_frame);
 FN(codec,av_packet_alloc);FN(codec,av_packet_free);FN(codec,av_packet_unref);FN(codec,avcodec_get_name);
#undef FN
 Api(){need((avformat_version()>>16)==LIBAVFORMAT_VERSION_MAJOR && (avcodec_version()>>16)==LIBAVCODEC_VERSION_MAJOR && (avutil_version()>>16)==LIBAVUTIL_VERSION_MAJOR,"backend-abi-mismatch");av_log_set_level(AV_LOG_QUIET);}
 Doc info(){Doc d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("avformatVersion",avformat_version(),a);d.AddMember("avcodecVersion",avcodec_version(),a);d.AddMember("avutilVersion",avutil_version(),a);auto config=avformat_configuration();put(d,"configurationSha256",hash(reinterpret_cast<const uint8_t*>(config),strlen(config)));return d;}
};
const char* formatName(const std::string& hint){if(hint=="mp4" || hint=="m4a" || hint=="mov")return "mov";if(hint=="webm" || hint=="matroska")return "matroska";if(hint=="mpegts")return "mpegts";if(hint=="adts")return "aac";if(hint=="mp3" || hint=="ogg" || hint=="flac" || hint=="wav")return hint=="mp3"?"mp3":hint=="ogg"?"ogg":hint=="flac"?"flac":"wav";throw std::runtime_error("unsupported-container-hint");}
struct Input {
 int64_t at=0,length=-1;bool complete=false;uint64_t readBytes=0,budget=16777216;std::string failure;
 Clock::time_point deadline=Clock::now()+std::chrono::seconds(3);
 static int interrupt(void* opaque){auto& s=*static_cast<Input*>(opaque);return !s.failure.empty() || Clock::now()>=s.deadline;}
 static int read(void* opaque,uint8_t* data,int count) noexcept {auto& s=*static_cast<Input*>(opaque);try{
  if(interrupt(opaque)){if(s.failure.empty())s.failure="backend-time-limit";return AVERROR_EXIT;}
  need(count>0,"invalid-backend-read");count=(std::min)(count,65536);
  if(s.readBytes+uint64_t(count)>s.budget){s.failure="backend-byte-limit";return AVERROR_EXIT;}
  std::cout<<"{\"io\":\"read\",\"offset\":\""<<s.at<<"\",\"length\":"<<count<<"}"<<std::endl;
  std::string line;need(bool(std::getline(std::cin,line)) && line.size()<=140000,"parent-disconnected");Doc d;d.Parse(line.c_str());need(d.IsObject() && d.HasMember("code") && d["code"].IsString(),"invalid-parent-reply");auto code=std::string(d["code"].GetString());
  if(code=="eof"){need(s.complete && s.length>=0 && s.at==s.length,"unconfirmed-eof");return AVERROR_EOF;}
  if(code!="data"){s.failure=code;return AVERROR_EXIT;}
  need(d.HasMember("hex") && d["hex"].IsString(),"missing-read-data");const auto size=d["hex"].GetStringLength();need(size>0 && !(size&1) && size<=unsigned(count)*2,"invalid-read-data");const auto* p=d["hex"].GetString();
  auto digit=[](char c){return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:-1;};
  for(size_t i=0;i<size/2;++i){auto a=digit(p[2*i]),b=digit(p[2*i+1]);need(a>=0 && b>=0,"invalid-read-hex");data[i]=uint8_t((a<<4)|b);}
  need(s.at<=INT64_MAX-int64_t(size/2),"read-position-overflow");s.at+=size/2;s.readBytes+=size/2;return int(size/2);
 }catch(...){s.failure="worker-io-error";return AVERROR_EXIT;}}
 static int64_t seek(void* opaque,int64_t offset,int whence) noexcept {auto& s=*static_cast<Input*>(opaque);if(interrupt(opaque))return AVERROR_EXIT;if(whence==AVSEEK_SIZE)return s.length<0?AVERROR(ENOSYS):s.length;
  whence&=~AVSEEK_FORCE;int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?s.at:whence==SEEK_END?s.length:-1;
  if(base<0 || (offset>0 && base>INT64_MAX-offset) || (offset<0 && offset< -base))return AVERROR(EINVAL);const auto target=base+offset;if(target<0 || (s.length>=0 && target>s.length))return AVERROR(EINVAL);s.at=target;return target;}
 static int external(AVFormatContext* context,AVIOContext**,const char*,int,AVDictionary**){static_cast<Input*>(context->opaque)->failure="external-io-denied";return AVERROR(EPERM);}
};
Doc run(Api& api,const Doc& start){need(start.HasMember("formatHint") && start["formatHint"].IsString() && start.HasMember("complete") && start["complete"].IsBool(),"invalid-start");Input input;input.complete=start["complete"].GetBool();if(start.HasMember("length") && start["length"].IsString()){const std::string n=start["length"].GetString();need(!n.empty() && n.size()<=19 && n.find_first_not_of("0123456789")==std::string::npos,"invalid-length");input.length=std::stoll(n);}
 unsigned limit=start.HasMember("packetLimit") && start["packetLimit"].IsUint()?start["packetLimit"].GetUint():32;need(limit>0 && limit<=128,"invalid-packet-limit");
 auto* buffer=static_cast<uint8_t*>(api.av_malloc(32768));need(buffer!=nullptr,"out-of-memory");auto* io=api.avio_alloc_context(buffer,32768,0,&input,Input::read,nullptr,Input::seek);if(!io){api.av_free(buffer);throw std::runtime_error("out-of-memory");}
 AVFormatContext* context=api.avformat_alloc_context();AVPacket* packet=nullptr;Doc result;result.SetObject();auto& a=result.GetAllocator();
 try{need(context!=nullptr,"out-of-memory");context->pb=io;context->opaque=&input;context->flags|=AVFMT_FLAG_CUSTOM_IO;context->interrupt_callback={Input::interrupt,&input};context->io_open=Input::external;context->max_streams=32;
  auto* fmt=api.av_find_input_format(formatName(start["formatHint"].GetString()));need(fmt!=nullptr,"demuxer-unavailable");AVDictionary* options=nullptr;api.av_dict_set(&options,"probesize","1048576",0);api.av_dict_set(&options,"analyzeduration","1000000",0);api.av_dict_set(&options,"max_probe_packets","128",0);api.av_dict_set(&options,"skip_estimate_duration_from_pts","1",0);int rc=api.avformat_open_input(&context,"resource",fmt,&options);api.av_dict_free(&options);if(rc>=0)rc=api.avformat_find_stream_info(context,nullptr);bool eof=false;Value packets(rapidjson::kArrayType);
  if(rc>=0){packet=api.av_packet_alloc();need(packet!=nullptr,"out-of-memory");for(unsigned i=0;i<limit;++i){rc=api.av_read_frame(context,packet);if(rc<0){eof=rc==AVERROR_EOF;break;}need(packet->size>0 && packet->size<=16777216 && packet->stream_index>=0 && unsigned(packet->stream_index)<context->nb_streams,"packet-limit");Value v(rapidjson::kObjectType);v.AddMember("stream",packet->stream_index,a);v.AddMember("bytes",packet->size,a);v.AddMember("sha256",Value(hash(packet->data,packet->size).c_str(),a),a);for(const auto& time:{std::pair<const char*,int64_t>{"pts",packet->pts},{"dts",packet->dts},{"duration",packet->duration},{"position",packet->pos}}){Value value;if(time.second==AV_NOPTS_VALUE)value.SetNull();else value.SetString(std::to_string(time.second).c_str(),a);v.AddMember(Value(time.first,a),value,a);}packets.PushBack(v,a);api.av_packet_unref(packet);}}
  std::string code=input.failure;if(code.empty()){if(Clock::now()>=input.deadline)code="backend-time-limit";else if(rc<0 && !eof)code="invalid-media";else if(packets.Empty())code="no-packets";else if(eof && !input.complete)code="waiting-range";else code="data";}
  put(result,"code",code);result.AddMember("analysisOnly",true,a);result.AddMember("playbackSupported",false,a);result.AddMember("demuxEof",eof && input.complete && code=="data",a);put(result,"bytesRead",std::to_string(input.readBytes));
  if(code=="data"){need(context->nb_streams<=32,"stream-limit");Value streams(rapidjson::kArrayType);for(unsigned i=0;i<context->nb_streams;++i){auto* st=context->streams[i];auto* cp=st->codecpar;need(cp && cp->extradata_size>=0 && cp->extradata_size<=1048576,"extradata-limit");Value v(rapidjson::kObjectType);v.AddMember("index",i,a);auto kind=api.av_get_media_type_string(cp->codec_type);v.AddMember("kind",Value(kind?kind:"unknown",a),a);v.AddMember("codec",Value(api.avcodec_get_name(cp->codec_id),a),a);v.AddMember("timebaseNumerator",st->time_base.num,a);v.AddMember("timebaseDenominator",st->time_base.den,a);v.AddMember("sampleRate",cp->sample_rate,a);v.AddMember("channels",cp->ch_layout.nb_channels,a);v.AddMember("width",cp->width,a);v.AddMember("height",cp->height,a);v.AddMember("extradataBytes",cp->extradata_size,a);v.AddMember("extradataSha256",Value(hash(cp->extradata,cp->extradata_size).c_str(),a),a);streams.PushBack(v,a);}result.AddMember("streams",streams,a);result.AddMember("packets",packets,a);put(result,"format",context->iformat->name);}
  auto backend=api.info();result.AddMember("backend",Value(backend,a),a);
 }catch(...){if(packet)api.av_packet_free(&packet);if(context)api.avformat_close_input(&context);api.av_free(io->buffer);api.avio_context_free(&io);throw;}
 if(packet)api.av_packet_free(&packet);if(context)api.avformat_close_input(&context);api.av_free(io->buffer);api.avio_context_free(&io);return result;
}
}
int main(int argc,char** argv){
#ifdef _WIN32
 _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
#ifndef _WIN32
 // Child-local limits; the player process and desktop session are unaffected.
 const rlimit cpu{3,3},files{64,64};
 if(setrlimit(RLIMIT_CPU,&cpu) || setrlimit(RLIMIT_NOFILE,&files))return 2;
#if !defined(__SANITIZE_ADDRESS__)
 // ASan reserves shadow address space before main; ordinary builds keep this cap.
 const rlimit memory{1024ULL*1024*1024,1024ULL*1024*1024};
 if(setrlimit(RLIMIT_AS,&memory))return 2;
#endif
#endif
 try{Api api;if(argc==2 && std::string(argv[1])=="--info"){auto d=api.info();put(d,"code","data");std::cout<<encode(d)<<std::endl;return 0;}need(argc==1,"invalid-arguments");std::string line;need(bool(std::getline(std::cin,line)) && line.size()<=4096,"invalid-start");Doc start;start.Parse(line.c_str());need(start.IsObject(),"invalid-start");auto result=run(api,start);std::cout<<"{\"result\":"<<encode(result)<<"}"<<std::endl;return 0;}
 catch(const std::exception& e){Doc out;out.SetObject();put(out,"code",e.what());out.AddMember("analysisOnly",true,out.GetAllocator());std::cout<<"{\"result\":"<<encode(out)<<"}"<<std::endl;return 1;}
}
