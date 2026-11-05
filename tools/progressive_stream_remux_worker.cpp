// Internal continuous copy-remux component. No player capability is advertised.
#include "progressive_demux_runtime.h"
#include "../shared/progressivestreamcapabilities.h"
namespace {
using namespace foxicmp::demux_worker;
constexpr uint64_t InputLimit=128*1024*1024,OutputLimit=64*1024*1024;
constexpr unsigned PacketLimit=50000;
Doc receive(size_t limit=4096){std::string line;char ch=0;bool ended=false;while(std::cin.get(ch)){if(ch=='\n'){ended=true;break;}need(line.size()<limit,"parent-frame-limit");line.push_back(ch);}need(ended,"parent-disconnected");Doc d;d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(line.c_str());need(!d.HasParseError() && d.IsObject(),"invalid-parent-reply");return d;}
std::string code(const Doc& d){need(d.HasMember("code") && d["code"].IsString(),"invalid-parent-reply");return {d["code"].GetString(),d["code"].GetStringLength()};}
struct StreamInput {
 int64_t at=0,length=0;bool complete=false;uint64_t readBytes=0,waits=0;std::string failure;
 Clock::time_point deadline=Clock::now()+std::chrono::hours(1);
 static int interrupt(void* opaque){auto& s=*static_cast<StreamInput*>(opaque);return !s.failure.empty() || Clock::now()>=s.deadline;}
 void wait(const char* reason,unsigned count){
  ++waits;std::cout<<"{\"io\":\"wait\",\"reason\":\""<<reason<<"\",\"offset\":\""<<at<<"\",\"length\":"<<count<<"}"<<std::endl;
  auto reply=receive();auto c=code(reply);need(c!="cancelled","cancelled");need(c=="resume" && reply.HasMember("complete") && reply["complete"].IsBool(),"invalid-resume");
  need(!complete || reply["complete"].GetBool(),"completion-withdrawn");complete=reply["complete"].GetBool();
 }
 static int read(void* opaque,uint8_t* data,int count) noexcept {auto& s=*static_cast<StreamInput*>(opaque);try{
  need(count>0,"invalid-backend-read");count=(std::min)(count,65536);
  for(;;){if(interrupt(opaque)){if(s.failure.empty())s.failure="stream-time-limit";return AVERROR_EXIT;}need(s.readBytes+uint64_t(count)<=InputLimit,"backend-byte-limit");
   std::cout<<"{\"io\":\"read\",\"offset\":\""<<s.at<<"\",\"length\":"<<count<<"}"<<std::endl;auto d=receive(140000);auto c=code(d);
   if(c=="waiting-range"){s.wait("waiting-range",unsigned(count));continue;}
   need(c!="cancelled","cancelled");
   if(c=="eof"){need(s.complete && s.at==s.length,"unconfirmed-eof");return AVERROR_EOF;}
   if(c!="data")throw std::runtime_error(c);
   need(d.HasMember("hex") && d["hex"].IsString(),"invalid-read-data");const auto n=d["hex"].GetStringLength();need(n && !(n&1) && n<=unsigned(count)*2 && n/2<=uint64_t(s.length-s.at),"invalid-read-data");
   const auto* p=d["hex"].GetString();auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
   for(size_t i=0;i<n/2;++i){auto a=digit(p[2*i]),b=digit(p[2*i+1]);need(a>=0 && b>=0,"invalid-read-hex");data[i]=uint8_t((a<<4)|b);}
#ifdef FOXICMP_STREAM_WORKER_TEST_CHECKPOINT
   FOXICMP_STREAM_WORKER_TEST_CHECKPOINT("read",uint64_t(s.at),p,n);
#endif
   s.at+=n/2;s.readBytes+=n/2;return int(n/2);
  }
 }catch(const std::exception& e){s.failure=e.what();return AVERROR_EXIT;}}
 static int64_t seek(void* opaque,int64_t offset,int whence){auto& s=*static_cast<StreamInput*>(opaque);if(interrupt(opaque))return AVERROR_EXIT;if(whence==AVSEEK_SIZE)return s.length;whence&=~AVSEEK_FORCE;
  int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?s.at:whence==SEEK_END?s.length:-1;if(base<0 || offset< -base || offset>s.length-base)return AVERROR(EINVAL);s.at=base+offset;return s.at;
 }
 static int external(AVFormatContext* context,AVIOContext**,const char*,int,AVDictionary**){static_cast<StreamInput*>(context->opaque)->failure="external-io-denied";return AVERROR(EPERM);}
};
struct StreamOutput {
 uint64_t bytes=0;foxicmp::progressive::Sha256 digest;std::string failure;
 static int writeBytes(void* opaque,const uint8_t* data,int count) noexcept {auto& s=*static_cast<StreamOutput*>(opaque);try{
  need(count>0 && uint64_t(count)<=OutputLimit-s.bytes,"backend-output-limit");const char* digits="0123456789abcdef";
  for(size_t at=0;at<size_t(count);){auto n=(std::min)(size_t(65536),size_t(count)-at);std::string hex;hex.reserve(n*2);for(size_t i=0;i<n;++i){auto b=data[at+i];hex+=digits[b>>4];hex+=digits[b&15];}
#ifdef FOXICMP_STREAM_WORKER_TEST_CHECKPOINT
   FOXICMP_STREAM_WORKER_TEST_CHECKPOINT("write",s.bytes,hex.data(),hex.size());
#endif
   std::cout<<"{\"io\":\"write\",\"offset\":\""<<s.bytes<<"\",\"hex\":\""<<hex<<"\"}"<<std::endl;auto reply=receive();auto c=code(reply);need(c!="cancelled","cancelled");need(c=="data","parent-write-rejected");s.digest.update(data+at,n);s.bytes+=n;at+=n;
  }return count;
 }catch(const std::exception& e){s.failure=e.what();return AVERROR(EIO);}}
 static int write(void* o,uint8_t* p,int n){return writeBytes(o,p,n);}
 static int write(void* o,const uint8_t* p,int n){return writeBytes(o,p,n);}
};
struct Context {
 Api& api;AVIOContext* inputIo=nullptr;AVIOContext* outputIo=nullptr;AVFormatContext* input=nullptr;AVFormatContext* output=nullptr;AVPacket* packet=nullptr;
 explicit Context(Api& a):api(a){}
 ~Context(){if(packet)api.av_packet_free(&packet);if(input)api.avformat_close_input(&input);if(output)api.avformat_free_context(output);for(auto* io:{inputIo,outputIo})if(io){api.av_free(io->buffer);api.avio_context_free(&io);}}
};
Doc run(Api& api,const Doc& start){
 need(start.MemberCount()==4 && start.HasMember("mode") && start["mode"].IsString() && start["mode"]=="continuous-h264-video-v1" && start.HasMember("formatHint") && start["formatHint"].IsString() && start["formatHint"]=="mp4" && start.HasMember("complete") && start["complete"].IsBool() && start.HasMember("length") && start["length"].IsString(),"unsupported-stream-start");
 std::string n(start["length"].GetString(),start["length"].GetStringLength());need(!n.empty() && n.size()<=10 && n[0]!='0' && n.find_first_not_of("0123456789")==std::string::npos,"invalid-length");StreamInput source;source.length=std::stoll(n);source.complete=start["complete"].GetBool();need(uint64_t(source.length)<=InputLimit,"input-size-limit");StreamOutput sink;Context c(api);
 auto allocate=[&](void* opaque,bool output){auto* bytes=static_cast<uint8_t*>(api.av_malloc(32768));need(bytes!=nullptr,"out-of-memory");auto* io=output?api.avio_alloc_context(bytes,32768,1,opaque,nullptr,StreamOutput::write,nullptr):api.avio_alloc_context(bytes,32768,0,opaque,StreamInput::read,nullptr,StreamInput::seek);if(!io)api.av_free(bytes);need(io!=nullptr,"out-of-memory");return io;};
 c.inputIo=allocate(&source,false);c.input=api.avformat_alloc_context();need(c.input!=nullptr,"out-of-memory");c.input->pb=c.inputIo;c.input->opaque=&source;c.input->flags|=AVFMT_FLAG_CUSTOM_IO;c.input->io_open=StreamInput::external;c.input->interrupt_callback={StreamInput::interrupt,&source};c.input->max_streams=2;
 AVDictionary* opts=nullptr;api.av_dict_set(&opts,"probesize","1048576",0);api.av_dict_set(&opts,"analyzeduration","1000000",0);int rc=api.avformat_open_input(&c.input,"resource",api.av_find_input_format("mov"),&opts);api.av_dict_free(&opts);need(source.failure.empty(),source.failure.c_str());need(rc>=0,"invalid-media");
 need(c.input->nb_streams==1,"unsupported-stream-count");
 // The transient format probe needs no decoder pool in a packet-copy process.
 AVDictionary* decoderOptions=nullptr;api.av_dict_set(&decoderOptions,"threads","1",0);rc=api.avformat_find_stream_info(c.input,&decoderOptions);api.av_dict_free(&decoderOptions);need(source.failure.empty(),source.failure.c_str());need(rc>=0,"invalid-media");auto* stream=c.input->streams[0];auto* cp=stream->codecpar;need(cp && cp->codec_type==AVMEDIA_TYPE_VIDEO && cp->codec_id==AV_CODEC_ID_H264 && cp->extradata_size>0 && cp->extradata_size<=1048576,"unsupported-stream-codec");need(cp->video_delay==0,"unsupported-stream-reorder");need(stream->start_time==0 && stream->time_base.num>0 && stream->time_base.den>0,"unsupported-stream-clock");
 need(api.avformat_alloc_output_context2(&c.output,nullptr,"mp4",nullptr)>=0 && c.output,"remux-muxer-unavailable");c.outputIo=allocate(&sink,true);c.outputIo->seekable=0;c.output->pb=c.outputIo;c.output->flags|=AVFMT_FLAG_CUSTOM_IO;c.output->opaque=&source;c.output->io_open=StreamInput::external;c.output->interrupt_callback={StreamInput::interrupt,&source};c.output->avoid_negative_ts=AVFMT_AVOID_NEG_TS_DISABLED;
 auto* out=api.avformat_new_stream(c.output,nullptr);need(out && api.avcodec_parameters_copy(out->codecpar,cp)>=0,"remux-copy-failed");out->codecpar->codec_tag=0;out->id=1;out->time_base=stream->time_base;
 api.av_dict_set(&opts,"movflags","empty_moov+frag_keyframe+default_base_moof+negative_cts_offsets",0);api.av_dict_set(&opts,"use_stream_ids_as_track_ids","1",0);rc=api.avformat_write_header(c.output,&opts);api.av_dict_free(&opts);need(sink.failure.empty(),sink.failure.c_str());need(rc>=0,"remux-header-failed");api.avio_flush(c.outputIo);need(sink.failure.empty(),sink.failure.c_str());need(c.outputIo->error>=0,"remux-output-failed");
 c.packet=api.av_packet_alloc();need(c.packet!=nullptr,"out-of-memory");unsigned packets=0;
 for(;;){need(source.failure.empty(),source.failure.c_str());need(!StreamInput::interrupt(&source),"stream-time-limit");rc=api.av_read_frame(c.input,c.packet);need(source.failure.empty(),source.failure.c_str());if(rc==AVERROR_EOF){while(!source.complete)source.wait("source-complete",0);break;}need(rc>=0,"invalid-media");need(++packets<=PacketLimit,"packet-limit");need(c.packet->stream_index==0 && c.packet->size>0 && c.packet->size<=16777216 && !(c.packet->flags&AV_PKT_FLAG_CORRUPT),"invalid-stream-packet");need(c.packet->pts!=AV_NOPTS_VALUE && c.packet->dts!=AV_NOPTS_VALUE && c.packet->duration>0,"unsupported-stream-packet-clock");
  for(int i=0;i<c.packet->side_data_elems;++i)need(c.packet->side_data[i].type!=AV_PKT_DATA_ENCRYPTION_INFO && c.packet->side_data[i].type!=AV_PKT_DATA_ENCRYPTION_INIT_INFO,"unsupported-stream-encryption");
  api.av_packet_rescale_ts(c.packet,stream->time_base,out->time_base);c.packet->pos=-1;rc=api.av_interleaved_write_frame(c.output,c.packet);api.av_packet_unref(c.packet);need(sink.failure.empty(),sink.failure.c_str());need(rc>=0,"remux-packet-failed");api.avio_flush(c.outputIo);need(c.outputIo->error>=0 && sink.failure.empty(),"remux-output-failed");
 }
 need(packets>0,"no-packets");rc=api.av_write_trailer(c.output);api.avio_flush(c.outputIo);need(sink.failure.empty(),sink.failure.c_str());need(rc>=0 && c.outputIo->error>=0,"remux-trailer-failed");
 Doc result;result.SetObject();auto& a=result.GetAllocator();put(result,"code","data");put(result,"mode","continuous-h264-video-v1");result.AddMember("sourceComplete",true,a);result.AddMember("sourceEof",true,a);result.AddMember("continuousOutput",true,a);result.AddMember("playbackSupported",false,a);result.AddMember("transcoded",false,a);result.AddMember("packets",packets,a);put(result,"bytesRead",std::to_string(source.readBytes));put(result,"bytesWritten",std::to_string(sink.bytes));put(result,"sha256",sink.digest.hex());put(result,"waits",std::to_string(source.waits));return result;
}
}
int main(int argc,char** argv){
#ifdef _WIN32
 _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#else
 const rlimit cpu{30,30},files{64,64};if(setrlimit(RLIMIT_CPU,&cpu) || setrlimit(RLIMIT_NOFILE,&files))return 2;
#if !defined(__SANITIZE_ADDRESS__)
 const rlimit memory{1024ULL*1024*1024,1024ULL*1024*1024};if(setrlimit(RLIMIT_AS,&memory))return 2;
#endif
#endif
 try{
  need(argc==1 || (argc==2 && std::string(argv[1])=="--info"),"invalid-arguments");Api api;
  if(argc==2){auto info=api.info();auto& a=info.GetAllocator();info.AddMember("version",1,a);info.AddMember("workerProtocol",1,a);info.AddMember("runtimeVerified",true,a);put(info,"mode","continuous-h264-video-v1");Doc cap;cap.Parse(foxicmp::progressive::ContinuousRemuxCapabilities());info.AddMember("contract",Value(cap,a),a);std::cout<<encode(info)<<std::endl;return 0;}
  auto start=receive();auto result=run(api,start);std::cout<<"{\"result\":"<<encode(result)<<"}"<<std::endl;return 0;
 }
 catch(const std::exception& e){Doc result;result.SetObject();put(result,"code",e.what());result.AddMember("sourceComplete",false,result.GetAllocator());result.AddMember("playbackSupported",false,result.GetAllocator());std::cout<<"{\"result\":"<<encode(result)<<"}"<<std::endl;return 1;}
}
