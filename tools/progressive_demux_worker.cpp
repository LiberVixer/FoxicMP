// FFmpeg demux through bounded parent-owned AVIO; no source paths or URLs. GPL-3.0-or-later.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
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
 FN(format,avformat_alloc_output_context2);FN(format,avformat_new_stream);FN(format,avformat_write_header);FN(format,av_interleaved_write_frame);FN(format,av_write_trailer);FN(format,avio_flush);
 FN(codec,avcodec_parameters_copy);FN(codec,av_packet_rescale_ts);
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
struct Output {
 std::vector<uint8_t> buffer;int64_t position=0;std::string failure;
 static int writeBytes(void* opaque,const uint8_t* data,int count) noexcept {auto& self=*static_cast<Output*>(opaque);
  try{need(count>0 && self.position>=0 && uint64_t(count)<=16777216-uint64_t(self.position),"backend-output-limit");const auto end=size_t(self.position)+size_t(count);if(end>self.buffer.size())self.buffer.resize(end);std::memcpy(self.buffer.data()+self.position,data,count);self.position+=count;return count;}
  catch(const std::exception& e){self.failure=e.what();return AVERROR(EIO);}
 }
 static int write(void* opaque,uint8_t* data,int count){return writeBytes(opaque,data,count);}
 static int write(void* opaque,const uint8_t* data,int count){return writeBytes(opaque,data,count);}
 static int64_t seek(void* opaque,int64_t offset,int whence){auto& self=*static_cast<Output*>(opaque);if(whence==AVSEEK_SIZE)return int64_t(self.buffer.size());whence&=~AVSEEK_FORCE;int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?self.position:whence==SEEK_END?int64_t(self.buffer.size()):-1;if(base<0 || offset< -base || offset>16777216-base)return AVERROR(EINVAL);self.position=base+offset;return self.position;}
 void send(){const char* digits="0123456789abcdef";for(size_t at=0;at<buffer.size();){const size_t n=(std::min)(size_t(65536),buffer.size()-at);std::string hex;hex.reserve(n*2);for(size_t i=0;i<n;++i){const auto byte=buffer[at+i];hex+=digits[byte>>4];hex+=digits[byte&15];}std::cout<<"{\"io\":\"write\",\"hex\":\""<<hex<<"\"}"<<std::endl;std::string line;need(bool(std::getline(std::cin,line)) && line.size()<=4096,"parent-disconnected");Doc reply;reply.Parse(line.c_str());need(reply.IsObject() && reply.HasMember("code") && reply["code"].IsString() && std::string(reply["code"].GetString())=="data","parent-write-rejected");at+=n;}}
};
class Remux {
 Api& api;AVFormatContext* context=nullptr;AVIOContext* io=nullptr;Output output;bool reorderedVideo=false;unsigned movieScale=1;std::vector<int64_t> factors,ends;std::vector<const AVCodecParameters*> parameters;
 void cleanup(){if(context)api.avformat_free_context(context);context=nullptr;if(io){api.av_free(io->buffer);api.avio_context_free(&io);}}
public:
 Remux(Api& functions,AVFormatContext* source,Input& input):api(functions){
  try{need(source->nb_streams>0 && source->nb_streams<=2,"unsupported-remux-streams");bool video=false,audio=false;
   for(unsigned i=0;i<source->nb_streams;++i){const auto* stream=source->streams[i];const auto* cp=stream->codecpar;need(stream->start_time==0 && stream->time_base.num>0 && stream->time_base.den>0,"unsupported-remux-clock");
    const auto denominator=uint64_t(stream->time_base.den),multiple=uint64_t(movieScale)/std::gcd(uint64_t(movieScale),denominator)*denominator;need(multiple<=INT32_MAX,"unsupported-remux-timescale");movieScale=unsigned(multiple);parameters.push_back(cp);need(cp && cp->extradata_size>0 && cp->extradata_size<=1048576,"unsupported-remux-config");
    if(cp->codec_type==AVMEDIA_TYPE_VIDEO && cp->codec_id==AV_CODEC_ID_H264 && !video){video=true;reorderedVideo=cp->video_delay>0;}
    else if(cp->codec_type==AVMEDIA_TYPE_AUDIO && cp->codec_id==AV_CODEC_ID_AAC && !audio){need(cp->profile==AV_PROFILE_AAC_LOW && (cp->sample_rate==48000 || cp->sample_rate==44100) && cp->ch_layout.nb_channels>0 && cp->ch_layout.nb_channels<=2 && cp->frame_size==1024 && stream->time_base.num==1 && stream->time_base.den==cp->sample_rate,"unsupported-remux-aac-config");audio=true;}
    else throw std::runtime_error("unsupported-remux-codec");
   }
   for(unsigned i=0;i<source->nb_streams;++i){factors.push_back(int64_t(movieScale/source->streams[i]->time_base.den)*source->streams[i]->time_base.num);ends.push_back(0);}
   need(api.avformat_alloc_output_context2(&context,nullptr,"mp4",nullptr)>=0 && context,"remux-muxer-unavailable");
   auto* buffer=static_cast<uint8_t*>(api.av_malloc(32768));need(buffer!=nullptr,"out-of-memory");io=api.avio_alloc_context(buffer,32768,1,&output,nullptr,Output::write,Output::seek);
   if(!io){api.av_free(buffer);throw std::runtime_error("out-of-memory");}io->seekable=AVIO_SEEKABLE_NORMAL;
   context->pb=io;context->flags|=AVFMT_FLAG_CUSTOM_IO;context->opaque=&input;context->io_open=Input::external;context->interrupt_callback={Input::interrupt,&input};context->avoid_negative_ts=AVFMT_AVOID_NEG_TS_DISABLED;
   for(unsigned i=0;i<source->nb_streams;++i){auto* stream=api.avformat_new_stream(context,nullptr);need(stream!=nullptr,"out-of-memory");need(api.avcodec_parameters_copy(stream->codecpar,source->streams[i]->codecpar)>=0,"remux-copy-failed");stream->codecpar->codec_tag=0;stream->id=int(i+1);stream->time_base=source->streams[i]->time_base;}
   AVDictionary* options=nullptr;api.av_dict_set(&options,"use_editlist","1",0);api.av_dict_set(&options,"movie_timescale",std::to_string(movieScale).c_str(),0);api.av_dict_set(&options,"use_stream_ids_as_track_ids","1",0);
   auto rc=api.avformat_write_header(context,&options);api.av_dict_free(&options);need(rc>=0,"remux-header-failed");
  }catch(...){cleanup();throw;}
 }
 ~Remux(){cleanup();}
 void packet(AVPacket* packet,AVRational timebase){need(!(packet->flags&AV_PKT_FLAG_CORRUPT),"corrupt-remux-packet");for(int i=0;i<packet->side_data_elems;++i)need(packet->side_data[i].type!=AV_PKT_DATA_ENCRYPTION_INFO && packet->side_data[i].type!=AV_PKT_DATA_ENCRYPTION_INIT_INFO,"unsupported-remux-encryption");const auto index=packet->stream_index;int64_t duration=packet->duration;
  for(int i=0;i<packet->side_data_elems;++i)if(packet->side_data[i].type==AV_PKT_DATA_SKIP_SAMPLES){const auto& side=packet->side_data[i];need(side.size>=10 && parameters[index]->codec_id==AV_CODEC_ID_AAC,"unsupported-remux-trim");const uint32_t discard=uint32_t(side.data[4])|uint32_t(side.data[5])<<8|uint32_t(side.data[6])<<16|uint32_t(side.data[7])<<24;need(discard<=1024 && duration>0 && duration<=1024,"unsupported-remux-trim");duration=(std::max)(duration,int64_t(1024))-discard;}
  need(packet->pts!=AV_NOPTS_VALUE && duration>0 && packet->pts<=INT64_MAX-duration,"unsupported-remux-packet-clock");const auto end=packet->pts+duration;
  if(end>0){need(end<=INT64_MAX/factors[index],"unsupported-remux-time-range");ends[index]=(std::max)(ends[index],end*factors[index]);}
  api.av_packet_rescale_ts(packet,timebase,context->streams[index]->time_base);packet->pos=-1;need(api.av_interleaved_write_frame(context,packet)>=0,"remux-packet-failed");need(output.failure.empty(),output.failure.c_str());}
 Doc finish(bool eof,unsigned packets){need(eof || !reorderedVideo,"unsupported-remux-reorder");need(api.av_write_trailer(context)>=0,"remux-trailer-failed");api.avio_flush(io);need(io->error>=0 && output.failure.empty(),"remux-output-failed");need(!output.buffer.empty(),"empty-remux-output");Doc d;d.SetObject();auto& a=d.GetAllocator();put(d,"container","mp4");put(d,"bytes",std::to_string(output.buffer.size()));put(d,"sha256",hash(output.buffer.data(),output.buffer.size()));d.AddMember("gaplessEditsSealed",true,a);d.AddMember("sourceEof",eof,a);d.AddMember("packetLimited",!eof,a);d.AddMember("packets",packets,a);d.AddMember("transcoded",false,a);put(d,"storage","bounded-private-memory");Value edits(rapidjson::kArrayType);for(unsigned i=0;i<ends.size();++i){need(ends[i]>0,"remux-incomplete-stream");Value edit(rapidjson::kObjectType);edit.AddMember("trackId",i+1,a);edit.AddMember("movieTimescale",movieScale,a);edit.AddMember("duration",Value(std::to_string(ends[i]).c_str(),a),a);edits.PushBack(edit,a);}d.AddMember("edits",edits,a);output.send();return d;}
};
Doc run(Api& api,const Doc& start){const bool remux=start.HasMember("remux");if(remux)need(start["remux"].IsString() && std::string(start["remux"].GetString())=="mp4-clip" && start.HasMember("formatHint") && start["formatHint"].IsString() && (std::string(start["formatHint"].GetString())=="mp4" || std::string(start["formatHint"].GetString())=="m4a" || std::string(start["formatHint"].GetString())=="mov"),"unsupported-remux-format");need(start.HasMember("formatHint") && start["formatHint"].IsString() && start.HasMember("complete") && start["complete"].IsBool(),"invalid-start");Input input;input.complete=start["complete"].GetBool();if(start.HasMember("length") && start["length"].IsString()){const std::string n=start["length"].GetString();need(!n.empty() && n.size()<=19 && n.find_first_not_of("0123456789")==std::string::npos,"invalid-length");input.length=std::stoll(n);}
 unsigned limit=start.HasMember("packetLimit") && start["packetLimit"].IsUint()?start["packetLimit"].GetUint():32;need(limit>0 && limit<=128,"invalid-packet-limit");
 auto* buffer=static_cast<uint8_t*>(api.av_malloc(32768));need(buffer!=nullptr,"out-of-memory");auto* io=api.avio_alloc_context(buffer,32768,0,&input,Input::read,nullptr,Input::seek);if(!io){api.av_free(buffer);throw std::runtime_error("out-of-memory");}
 AVFormatContext* context=api.avformat_alloc_context();AVPacket* packet=nullptr;Doc result;result.SetObject();auto& a=result.GetAllocator();
 try{need(context!=nullptr,"out-of-memory");context->pb=io;context->opaque=&input;context->flags|=AVFMT_FLAG_CUSTOM_IO;context->interrupt_callback={Input::interrupt,&input};context->io_open=Input::external;context->max_streams=32;
  auto* fmt=api.av_find_input_format(formatName(start["formatHint"].GetString()));need(fmt!=nullptr,"demuxer-unavailable");AVDictionary* options=nullptr;api.av_dict_set(&options,"probesize","1048576",0);api.av_dict_set(&options,"analyzeduration","1000000",0);api.av_dict_set(&options,"max_probe_packets","128",0);api.av_dict_set(&options,"skip_estimate_duration_from_pts","1",0);int rc=api.avformat_open_input(&context,"resource",fmt,&options);api.av_dict_free(&options);if(rc>=0)rc=api.avformat_find_stream_info(context,nullptr);bool eof=false;Value packets(rapidjson::kArrayType);std::unique_ptr<Remux> mux;if(rc>=0 && remux && input.failure.empty())mux=std::make_unique<Remux>(api,context,input);
  if(rc>=0){packet=api.av_packet_alloc();need(packet!=nullptr,"out-of-memory");for(unsigned i=0;i<limit;++i){rc=api.av_read_frame(context,packet);if(rc<0){eof=rc==AVERROR_EOF;break;}need(packet->size>0 && packet->size<=16777216 && packet->stream_index>=0 && unsigned(packet->stream_index)<context->nb_streams,"packet-limit");Value v(rapidjson::kObjectType);v.AddMember("stream",packet->stream_index,a);v.AddMember("bytes",packet->size,a);v.AddMember("sha256",Value(hash(packet->data,packet->size).c_str(),a),a);for(const auto& time:{std::pair<const char*,int64_t>{"pts",packet->pts},{"dts",packet->dts},{"duration",packet->duration},{"position",packet->pos}}){Value value;if(time.second==AV_NOPTS_VALUE)value.SetNull();else value.SetString(std::to_string(time.second).c_str(),a);v.AddMember(Value(time.first,a),value,a);}packets.PushBack(v,a);if(mux)mux->packet(packet,context->streams[packet->stream_index]->time_base);api.av_packet_unref(packet);}}
  std::string code=input.failure;if(code.empty()){if(Clock::now()>=input.deadline)code="backend-time-limit";else if(rc<0 && !eof)code="invalid-media";else if(packets.Empty())code="no-packets";else if(eof && !input.complete)code="waiting-range";else code="data";}
  put(result,"code",code);result.AddMember("analysisOnly",!remux,a);if(remux)result.AddMember("preparationOnly",true,a);result.AddMember("playbackSupported",false,a);result.AddMember("demuxEof",eof && input.complete && code=="data",a);put(result,"bytesRead",std::to_string(input.readBytes));
  if(code=="data"){if(mux){auto staged=mux->finish(eof,packets.Size());result.AddMember("remux",Value(staged,a),a);}need(context->nb_streams<=32,"stream-limit");Value streams(rapidjson::kArrayType);for(unsigned i=0;i<context->nb_streams;++i){auto* st=context->streams[i];auto* cp=st->codecpar;need(cp && cp->extradata_size>=0 && cp->extradata_size<=1048576,"extradata-limit");Value v(rapidjson::kObjectType);v.AddMember("index",i,a);auto kind=api.av_get_media_type_string(cp->codec_type);v.AddMember("kind",Value(kind?kind:"unknown",a),a);v.AddMember("codec",Value(api.avcodec_get_name(cp->codec_id),a),a);v.AddMember("timebaseNumerator",st->time_base.num,a);v.AddMember("timebaseDenominator",st->time_base.den,a);v.AddMember("sampleRate",cp->sample_rate,a);v.AddMember("channels",cp->ch_layout.nb_channels,a);v.AddMember("width",cp->width,a);v.AddMember("height",cp->height,a);v.AddMember("extradataBytes",cp->extradata_size,a);v.AddMember("extradataSha256",Value(hash(cp->extradata,cp->extradata_size).c_str(),a),a);streams.PushBack(v,a);}result.AddMember("streams",streams,a);result.AddMember("packets",packets,a);put(result,"format",context->iformat->name);}
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
