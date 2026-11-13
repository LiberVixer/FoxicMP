// Internal continuous copy-remux component. No player capability is advertised.
#include "progressive_demux_runtime.h"
#include "../shared/progressivestreamcapabilities.h"
#include "../src/DSUtil/HarvesterAacWindow.h"
#include "../src/DSUtil/HarvesterHevc.h"
#ifndef AV_PROFILE_UNKNOWN
#define AV_PROFILE_UNKNOWN FF_PROFILE_UNKNOWN
#endif
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
 uint64_t bytes=0;foxicmp::progressive::Sha256 digest;std::string failure;bool windowRequired=false,windowAccepted=false;
 void audioWindow(const Doc& window){const auto body=encode(window),digest=hash(reinterpret_cast<const uint8_t*>(body.data()),body.size());std::cout<<"{\"io\":\"audio-window\",\"window\":"<<body<<",\"sha256\":\""<<digest<<"\"}"<<std::endl;auto reply=receive();need(code(reply)=="audio-window-accepted" && reply.HasMember("sha256") && reply["sha256"].IsString() && reply["sha256"]==digest.c_str(),"audio-window-not-accepted");windowAccepted=true;}
 static int writeBytes(void* opaque,const uint8_t* data,int count) noexcept {auto& s=*static_cast<StreamOutput*>(opaque);try{
  need(!s.windowRequired || s.windowAccepted,"audio-window-not-accepted");need(count>0 && uint64_t(count)<=OutputLimit-s.bytes,"backend-output-limit");const char* digits="0123456789abcdef";
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
 need(start.MemberCount()==4 && start.HasMember("mode") && start["mode"].IsString() && start.HasMember("formatHint") && start["formatHint"].IsString() && start["formatHint"]=="mp4" && start.HasMember("complete") && start["complete"].IsBool() && start.HasMember("length") && start["length"].IsString(),"unsupported-stream-start");
 // Audio output requires an explicit PCM-window acknowledgement before media
 // bytes. The default mode remains video-only.
 const bool alignedAudio=start["mode"]=="continuous-h264-aac-aligned-v1";
 const bool windowAudio=start["mode"]=="continuous-h264-aac-window-v1" || start["mode"]=="continuous-hevc-aac-window-v1",hasAudio=alignedAudio || windowAudio;
 const bool hevc=start["mode"]=="continuous-hevc-reordered-video-v1" || start["mode"]=="continuous-hevc-aac-window-v1";
 const bool reordered=hevc || hasAudio || start["mode"]=="continuous-h264-reordered-video-v1";
 need(reordered || start["mode"]=="continuous-h264-video-v1","unsupported-stream-start");
 std::string n(start["length"].GetString(),start["length"].GetStringLength());need(!n.empty() && n.size()<=10 && n[0]!='0' && n.find_first_not_of("0123456789")==std::string::npos,"invalid-length");StreamInput source;source.length=std::stoll(n);source.complete=start["complete"].GetBool();need(uint64_t(source.length)<=InputLimit,"input-size-limit");StreamOutput sink;sink.windowRequired=windowAudio;Context c(api);
 auto allocate=[&](void* opaque,bool output){auto* bytes=static_cast<uint8_t*>(api.av_malloc(32768));need(bytes!=nullptr,"out-of-memory");auto* io=output?api.avio_alloc_context(bytes,32768,1,opaque,nullptr,StreamOutput::write,nullptr):api.avio_alloc_context(bytes,32768,0,opaque,StreamInput::read,nullptr,StreamInput::seek);if(!io)api.av_free(bytes);need(io!=nullptr,"out-of-memory");return io;};
 c.inputIo=allocate(&source,false);c.input=api.avformat_alloc_context();need(c.input!=nullptr,"out-of-memory");c.input->pb=c.inputIo;c.input->opaque=&source;c.input->flags|=AVFMT_FLAG_CUSTOM_IO;c.input->io_open=StreamInput::external;c.input->interrupt_callback={StreamInput::interrupt,&source};c.input->max_streams=2;
 AVDictionary* opts=nullptr;api.av_dict_set(&opts,"probesize","1048576",0);api.av_dict_set(&opts,"analyzeduration","1000000",0);int rc=api.avformat_open_input(&c.input,"resource",api.av_find_input_format("mov"),&opts);api.av_dict_free(&opts);need(source.failure.empty(),source.failure.c_str());need(rc>=0,"invalid-media");
 need(c.input->nb_streams==(hasAudio?2u:1u),"unsupported-stream-count");
 // The transient format probe needs no decoder pool in a packet-copy process.
 std::vector<AVDictionary*> decoderOptions(c.input->nb_streams,nullptr);for(auto& options:decoderOptions)api.av_dict_set(&options,"threads","1",0);rc=api.avformat_find_stream_info(c.input,decoderOptions.data());for(auto& options:decoderOptions)api.av_dict_free(&options);need(source.failure.empty(),source.failure.c_str());need(rc>=0,"invalid-media");
 int videoIndex=-1,audioIndex=-1;unsigned movieScale=1;uint64_t declaredAudioSamples=0,declaredHevcPackets=0;harvester::HevcParameterSets hevcParameters;
 for(unsigned i=0;i<c.input->nb_streams;++i){auto* stream=c.input->streams[i];auto* cp=stream->codecpar;
  need(cp && cp->extradata_size>0 && cp->extradata_size<=1048576,"unsupported-stream-codec");need(stream->start_time==0 && stream->time_base.num>0 && stream->time_base.den>0,"unsupported-stream-clock");
  if(cp->codec_type==AVMEDIA_TYPE_VIDEO && cp->codec_id==(hevc?AV_CODEC_ID_HEVC:AV_CODEC_ID_H264) && videoIndex<0){
   videoIndex=int(i);need(reordered ? cp->video_delay>=0 && cp->video_delay<=16 : cp->video_delay==0,"unsupported-stream-reorder");
   if(hevc){
    need(stream->duration>0 && stream->duration<=int64_t(3600)*stream->time_base.den/stream->time_base.num,"unsupported-stream-clock");
    need(stream->nb_frames>0 && uint64_t(stream->nb_frames)<=PacketLimit,"unsupported-stream-hevc-index");declaredHevcPackets=uint64_t(stream->nb_frames);
    need(cp->codec_tag==MKTAG('h','v','c','1') || cp->codec_tag==MKTAG('h','e','v','1'),"unsupported-stream-hevc-configuration");
    harvester::HEVCConfiguration(cp->extradata,size_t(cp->extradata_size),cp->codec_tag==MKTAG('h','v','c','1'),
     [](bool ok,const char*){need(ok,"unsupported-stream-hevc-configuration");},&hevcParameters);
    need(hevcParameters.size()==3 && cp->format==((cp->extradata[17]&7)?AV_PIX_FMT_YUV420P10LE:AV_PIX_FMT_YUV420P),"unsupported-stream-hevc-configuration");
   }
  }
  else if(hasAudio && cp->codec_type==AVMEDIA_TYPE_AUDIO && cp->codec_id==AV_CODEC_ID_AAC && audioIndex<0){
   // A very short AAC track can sit beyond find_stream_info's bounded probe.
   // Recover only the canonical LC ASC (1024 samples, no SBR/PS), never guess
   // from a codec hint or extend the probe into unpublished media bytes.
   if((cp->profile==AV_PROFILE_UNKNOWN || cp->profile==AV_PROFILE_AAC_LOW) && (cp->frame_size==0 || cp->frame_size==1024) && (cp->extradata_size==2 || cp->extradata_size==5)){
    const unsigned asc=(unsigned(cp->extradata[0])<<8)|cp->extradata[1];
    const unsigned rateIndex=cp->sample_rate==48000?3:cp->sample_rate==44100?4:15;
    const bool extension=cp->extradata_size==2 || (cp->extradata[2]==0x56 && cp->extradata[3]==0xe5 && cp->extradata[4]==0);
    if(cp->ch_layout.nb_channels>=1 && cp->ch_layout.nb_channels<=2 && rateIndex!=15 && extension &&
       asc==((2u<<11)|(rateIndex<<7)|(unsigned(cp->ch_layout.nb_channels)<<3))){cp->profile=AV_PROFILE_AAC_LOW;cp->frame_size=1024;}
   }
   need(cp->profile==AV_PROFILE_AAC_LOW && (cp->sample_rate==44100 || cp->sample_rate==48000) && cp->ch_layout.nb_channels>=1 && cp->ch_layout.nb_channels<=2 && cp->frame_size==1024 && stream->time_base.num==1 && stream->time_base.den==cp->sample_rate,"unsupported-stream-aac-config");
   need(stream->duration>0 && stream->duration<=int64_t(cp->sample_rate)*3600 && (!alignedAudio || stream->duration%1024==0),"unsupported-stream-aac-end-trim");audioIndex=int(i);declaredAudioSamples=uint64_t(stream->duration);
  }else throw std::runtime_error("unsupported-stream-codec");
  const auto denominator=uint64_t(stream->time_base.den),multiple=uint64_t(movieScale)/std::gcd(uint64_t(movieScale),denominator)*denominator;need(multiple<=INT32_MAX,"unsupported-stream-timescale");movieScale=unsigned(multiple);
 }
 need(videoIndex>=0 && (!hasAudio || audioIndex>=0),"unsupported-stream-codec");
 need(api.avformat_alloc_output_context2(&c.output,nullptr,"mp4",nullptr)>=0 && c.output,"remux-muxer-unavailable");c.outputIo=allocate(&sink,true);c.outputIo->seekable=0;c.output->pb=c.outputIo;c.output->flags|=AVFMT_FLAG_CUSTOM_IO;c.output->opaque=&source;c.output->io_open=StreamInput::external;c.output->interrupt_callback={StreamInput::interrupt,&source};c.output->avoid_negative_ts=AVFMT_AVOID_NEG_TS_DISABLED;
 // A short audio track must not hold every video fragment until source EOF.
 // This bounds the mux timestamp queue, not wall-clock reader latency.
 if(windowAudio)c.output->max_interleave_delta=250000;
 for(unsigned i=0;i<c.input->nb_streams;++i){auto* out=api.avformat_new_stream(c.output,nullptr);need(out && api.avcodec_parameters_copy(out->codecpar,c.input->streams[i]->codecpar)>=0,"remux-copy-failed");out->codecpar->codec_tag=hevc?c.input->streams[i]->codecpar->codec_tag:0;out->id=int(i+1);out->time_base=c.input->streams[i]->time_base;}
 // Reordered packets begin with negative DTS. An empty moov/negative CTS
 // shifts their clocks; delay moov until the first fragment can seal an edit
 // list. The exact input timescale avoids rounding its initial decode delay.
 // Aligned AAC shares that exact movie clock; no per-track timestamp reset.
 api.av_dict_set(&opts,"movflags",reordered ? "delay_moov+frag_keyframe+default_base_moof" : "empty_moov+frag_keyframe+default_base_moof+negative_cts_offsets",0);
 if(reordered){api.av_dict_set(&opts,"use_editlist","1",0);api.av_dict_set(&opts,"movie_timescale",std::to_string(movieScale).c_str(),0);}
 api.av_dict_set(&opts,"use_stream_ids_as_track_ids","1",0);rc=api.avformat_write_header(c.output,&opts);api.av_dict_free(&opts);need(sink.failure.empty(),sink.failure.c_str());need(rc>=0,"remux-header-failed");api.avio_flush(c.outputIo);need(sink.failure.empty(),sink.failure.c_str());need(c.outputIo->error>=0,"remux-output-failed");
 c.packet=api.av_packet_alloc();need(c.packet!=nullptr,"out-of-memory");unsigned packets=0,videoPackets=0,audioPackets=0;int64_t audioSamples=0;unsigned initialSkip=0;harvester::pcm::AacWindow audioWindow;Doc windowDescription;
 for(;;){need(source.failure.empty(),source.failure.c_str());need(!StreamInput::interrupt(&source),"stream-time-limit");rc=api.av_read_frame(c.input,c.packet);need(source.failure.empty(),source.failure.c_str());if(rc==AVERROR_EOF){while(!source.complete)source.wait("source-complete",0);break;}need(rc>=0,"invalid-media");need(++packets<=PacketLimit,"packet-limit");need(c.packet->stream_index>=0 && unsigned(c.packet->stream_index)<c.input->nb_streams && c.packet->size>0 && c.packet->size<=16777216 && !(c.packet->flags&AV_PKT_FLAG_CORRUPT),"invalid-stream-packet");need(c.packet->pts!=AV_NOPTS_VALUE && c.packet->dts!=AV_NOPTS_VALUE && c.packet->duration>0,"unsupported-stream-packet-clock");
  for(int i=0;i<c.packet->side_data_elems;++i)need(c.packet->side_data[i].type!=AV_PKT_DATA_ENCRYPTION_INFO && c.packet->side_data[i].type!=AV_PKT_DATA_ENCRYPTION_INIT_INFO,"unsupported-stream-encryption");
  const auto index=c.packet->stream_index;
  if(hevc && index==videoIndex){
   for(int i=0;i<c.packet->side_data_elems;++i)need(c.packet->side_data[i].type!=AV_PKT_DATA_NEW_EXTRADATA,"unsupported-stream-configuration-change");
   const bool firstVideo=hasAudio?videoPackets==0:packets==1;
   need(!firstVideo || ((c.packet->flags&AV_PKT_FLAG_KEY) && c.packet->pts==0),"unsupported-stream-hevc-packet");
   harvester::HEVCPacket(c.packet->data,size_t(c.packet->size),hevcParameters,need,firstVideo);
  }
  if(hasAudio){
   unsigned skip=0,discard=0;bool trimSide=false;
   for(int i=0;i<c.packet->side_data_elems;++i){const auto& side=c.packet->side_data[i];need(side.type!=AV_PKT_DATA_NEW_EXTRADATA,"unsupported-stream-configuration-change");
    if(side.type==AV_PKT_DATA_SKIP_SAMPLES){need(index==audioIndex && side.size>=10 && !trimSide,"unsupported-stream-aac-trim");trimSide=true;auto le32=[](const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;};skip=le32(side.data);discard=le32(side.data+4);}
   }
   if(index==audioIndex){
    if(audioPackets==0){initialSkip=skip;need(initialSkip<=1024 && c.packet->pts==-int64_t(initialSkip) && (!hevc || initialSkip==1024),"unsupported-stream-aac-trim");}
    if(windowAudio){
     if(audioPackets==0){auto* cp=c.input->streams[audioIndex]->codecpar;need(audioWindow.Configure(cp->sample_rate,cp->ch_layout.nb_channels,declaredAudioSamples,initialSkip),"unsupported-stream-aac-window");
      windowDescription.SetObject();auto& a=windowDescription.GetAllocator();windowDescription.AddMember("version",1,a);windowDescription.AddMember("trackId",audioIndex+1,a);windowDescription.AddMember("sampleRate",audioWindow.Rate(),a);windowDescription.AddMember("channels",audioWindow.Channels(),a);
      put(windowDescription,"rawOrigin","untrimmed-decoder-samples");put(windowDescription,"timestampOrigin","presentation-samples");put(windowDescription,"rawBegin",std::to_string(audioWindow.RawBegin()));put(windowDescription,"rawEnd",std::to_string(audioWindow.RawEnd()));put(windowDescription,"timestampBegin","0");put(windowDescription,"timestampEnd",std::to_string(audioWindow.Samples()));put(windowDescription,"encodedEnd",std::to_string(audioWindow.EncodedEnd()));put(windowDescription,"endPadding",std::to_string(audioWindow.EndPadding()));
     }
     need(audioWindow.Packet(c.packet->pts,c.packet->dts,uint64_t(c.packet->duration),skip,discard),"unsupported-stream-aac-clock");
     if(!sink.windowAccepted)sink.audioWindow(windowDescription);
    }else need((!trimSide || (audioPackets==0 && discard==0)) && c.packet->pts==c.packet->dts && c.packet->duration==1024 && c.packet->pts==int64_t(audioPackets)*1024-initialSkip,"unsupported-stream-aac-clock");
    ++audioPackets;audioSamples+=1024;
   }else ++videoPackets;
  }
  api.av_packet_rescale_ts(c.packet,c.input->streams[index]->time_base,c.output->streams[index]->time_base);c.packet->pos=-1;rc=api.av_interleaved_write_frame(c.output,c.packet);api.av_packet_unref(c.packet);need(sink.failure.empty(),sink.failure.c_str());need(rc>=0,"remux-packet-failed");api.avio_flush(c.outputIo);need(c.outputIo->error>=0 && sink.failure.empty(),"remux-output-failed");
 }
 need(packets>0,"no-packets");if(hevc)need((hasAudio?videoPackets:packets)==declaredHevcPackets,"incomplete-stream-hevc-samples");if(hasAudio)need(videoPackets>0 && audioPackets>0 && (windowAudio?audioWindow.Complete():uint64_t(audioSamples-initialSkip)==declaredAudioSamples),"unsupported-stream-aac-end-trim");rc=api.av_write_trailer(c.output);api.avio_flush(c.outputIo);need(sink.failure.empty(),sink.failure.c_str());need(rc>=0 && c.outputIo->error>=0,"remux-trailer-failed");
 Doc result;result.SetObject();auto& a=result.GetAllocator();put(result,"code","data");put(result,"mode",std::string(start["mode"].GetString(),start["mode"].GetStringLength()));result.AddMember("sourceComplete",true,a);result.AddMember("sourceEof",true,a);result.AddMember("continuousOutput",true,a);result.AddMember("playbackSupported",false,a);result.AddMember("transcoded",false,a);result.AddMember("packets",packets,a);put(result,"bytesRead",std::to_string(source.readBytes));put(result,"bytesWritten",std::to_string(sink.bytes));put(result,"sha256",sink.digest.hex());put(result,"waits",std::to_string(source.waits));
 if(hasAudio){result.AddMember("videoPackets",videoPackets,a);result.AddMember("audioPackets",audioPackets,a);result.AddMember("aacInitialSkipSamples",initialSkip,a);put(result,"audioPresentationSamples",std::to_string(declaredAudioSamples));result.AddMember("aacEndTrimRequired",windowAudio && audioWindow.EndPadding()!=0,a);if(windowAudio){result.AddMember("pcmWindowRequired",true,a);result.AddMember("aacPcmWindow",Value(windowDescription,a),a);}}return result;
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
