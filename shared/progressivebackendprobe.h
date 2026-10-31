// Snapshot-only FFmpeg diagnostics through the same confirmed-range source as decoders.
// GPL-3.0-or-later. This interface never declares playback/remux support.
#pragma once
#include "progressiveresourceio.h"
#include "progressiveprocess.h"
namespace foxicmp { namespace progressive {
inline const char* BackendDemuxCapabilities(){return R"({"version":1,"mode":"libavformat-avio-probe-v1","analysisOnly":true,"playbackSupported":false,"availability":"checked-per-request","adjacentWorkerRequired":true,"formatHints":["mp4","m4a","mov","webm","matroska","mpegts","adts","mp3","ogg","flac","wav"],"readChunkBytes":65536,"maxReadBytes":16777216,"maxPackets":128,"maxStreams":32,"workerDeadlineMilliseconds":4000,"sourcePathsSentToWorker":false,"sourceUrlsSentToWorker":false,"externalIoAllowed":false,"integrityRequired":true})";}
inline const char* BackendRemuxCapabilities(){return R"({"version":1,"mode":"libavformat-avio-mp4-stage-v1","preparationOnly":true,"playbackSupported":false,"availability":"checked-per-request","formatHints":["mp4","m4a","mov"],"output":"mp4-clip","boundedClip":true,"wholeSourcePlayback":false,"codecs":["h264","aac"],"maxVideoStreams":1,"maxAudioStreams":1,"maxPackets":128,"maxOutputBytes":16777216,"storage":"bounded-private-memory","aacProfiles":["LC"],"aacRates":[44100,48000],"aacChannels":[1,2],"presentationStart":0,"reorderedVideoRequiresSourceEof":true,"gaplessEditsSealed":true,"originalsModified":false,"transcoding":false,"integrityRequired":true})";}
struct BackendProbeQuery {std::string resource,format;unsigned packetLimit=32;bool remux=false;};
inline bool BackendWaiting(const std::string& code){return code=="waiting-range" || code=="waiting-resource";}
inline BackendProbeQuery BackendQuery(const Value& value,const Snapshot& snapshot){
 Keys(value,"media",{"resourceId","formatHint"},{"packetLimit","remux"});BackendProbeQuery out;out.resource=Id(value["resourceId"],"media.resourceId");out.format=Text(value["formatHint"],"media.formatHint",16);
 Require(std::set<std::string>{"mp4","m4a","mov","webm","matroska","mpegts","adts","mp3","ogg","flac","wav"}.count(out.format)!=0,"media.formatHint","Unsupported demux hint","invalid_request");
 Require(snapshot.resources.count(out.resource)!=0,"media.resourceId","Unknown demux resource","invalid_request");Require(snapshot.resources.at(out.resource).integrity,"media.resourceId","Demux requires SHA-256","integrity-required");
 if(value.HasMember("remux")){Require(value["remux"].IsString() && std::string(value["remux"].GetString())=="mp4-clip","media.remux","Unsupported remux output","invalid_request");Require(out.format=="mp4" || out.format=="m4a" || out.format=="mov","media.formatHint","Unsupported remux input","invalid_request");out.remux=true;}
 if(value.HasMember("packetLimit")){Require(value["packetLimit"].IsUint() && value["packetLimit"].GetUint()>0 && value["packetLimit"].GetUint()<=128,"media.packetLimit","Packet limit must be 1..128","invalid_request");out.packetLimit=value["packetLimit"].GetUint();}return out;
}
inline std::string BackendResult(const std::string& code,const BackendProbeQuery& query,uint64_t bytes,uint64_t offset=0,unsigned length=0){
 rapidjson::Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("code",Value(code.c_str(),a),a);d.AddMember("resourceId",Value(query.resource.c_str(),a),a);d.AddMember("analysisOnly",!query.remux,a);if(query.remux)d.AddMember("preparationOnly",true,a);d.AddMember("playbackSupported",false,a);d.AddMember("bytesRead",Value(std::to_string(bytes).c_str(),a),a);
 if(length){d.AddMember("neededOffset",Value(std::to_string(offset).c_str(),a),a);d.AddMember("neededLength",length,a);}return lease_detail::Encode(d);
}
inline std::string ProbeBackend(ResourceSource& source,const BackendProbeQuery& query,std::atomic<bool>& cancelled,std::vector<char>* artifact=nullptr){uint64_t total=0;std::vector<char> staged;if(artifact)artifact->clear();
 try{const auto resource=source.describe(query.resource);auto executable=process_detail::ExecutableDirectory()/
#ifdef _WIN32
  "foxicmp-demux-worker.exe";
#else
  "foxicmp-demux-worker";
#endif
  process_detail::Process process(executable,cancelled);rapidjson::Document start;start.SetObject();auto& a=start.GetAllocator();start.AddMember("formatHint",Value(query.format.c_str(),a),a);start.AddMember("complete",resource.state=="complete",a);Value length;if(resource.known)length.SetString(std::to_string(resource.length).c_str(),a);else length.SetNull();start.AddMember("length",length,a);start.AddMember("packetLimit",query.packetLimit,a);if(query.remux)start.AddMember("remux","mp4-clip",a);process.send(lease_detail::Encode(start));
  for(;;){auto line=process.receive();rapidjson::Document d;d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(line.data(),line.size());Require(!d.HasParseError() && d.IsObject(),"backend","Invalid worker message","backend-protocol-error");harvester::UniqueKeys(d);
   if(d.HasMember("result")){Keys(d,"backend",{"result"});Require(d["result"].IsObject() && d["result"].HasMember("code") && d["result"]["code"].IsString(),"backend","Invalid worker result","backend-protocol-error");auto& result=d["result"];const auto code=Text(result["code"],"backend.code",64);Require(result.HasMember("analysisOnly") && result["analysisOnly"].IsBool(),"backend","Invalid worker mode","backend-protocol-error");
    if(code=="data"){Require(result["analysisOnly"].GetBool()==!query.remux && (!query.remux || (result.HasMember("preparationOnly") && result["preparationOnly"].IsBool() && result["preparationOnly"].GetBool())),"backend","Worker mode mismatch","backend-protocol-error");
     if(query.remux){Require(artifact && !staged.empty() && result.HasMember("remux") && result["remux"].IsObject(),"backend","Missing remux result","backend-protocol-error");auto& mux=result["remux"];Require(mux.HasMember("bytes") && Unsigned(mux["bytes"],"remux.bytes",16777216)==staged.size() && mux.HasMember("sha256"),"backend","Wrong remux length","backend-protocol-error");Sha256 hash;hash.update(staged.data(),staged.size());Require(Text(mux["sha256"],"remux.sha256",64)==hash.hex(),"backend","Wrong remux hash","backend-protocol-error");}
    }
    if(code=="data"){Require(result.HasMember("streams") && result["streams"].IsArray() && result["streams"].Size()<=32 && result.HasMember("packets") && result["packets"].IsArray() && result["packets"].Size()>0 && result["packets"].Size()<=query.packetLimit,"backend","Invalid demux result bounds","backend-protocol-error");source.verifyPinned();if(query.remux){Require(result["remux"].HasMember("gaplessEditsSealed") && result["remux"]["gaplessEditsSealed"].IsBool() && result["remux"]["gaplessEditsSealed"].GetBool(),"backend","Unsealed MP4 stage","backend-protocol-error");artifact->swap(staged);}}

    if(result.HasMember("playbackSupported"))result["playbackSupported"].SetBool(false);else result.AddMember("playbackSupported",false,d.GetAllocator());result.AddMember("resourceId",Value(query.resource.c_str(),d.GetAllocator()),d.GetAllocator());result.AddMember("parentBytesRead",Value(std::to_string(total).c_str(),d.GetAllocator()),d.GetAllocator());return lease_detail::Encode(result);
   }
   if(d.HasMember("io") && d["io"].IsString() && std::string(d["io"].GetString())=="write"){
    Require(query.remux,"backend","Unexpected worker output","backend-protocol-error");Keys(d,"backend.write",{"io","hex"});Require(d["hex"].IsString(),"backend","Invalid output bytes","backend-protocol-error");const auto n=d["hex"].GetStringLength();Require(n>0 && n<=131072 && n%2==0,"backend","Output frame exceeds bounds","backend-protocol-error");if(n/2>16777216-staged.size())return BackendResult("backend-output-limit",query,total);
    const auto* hex=d["hex"].GetString();auto digit=[](char c){return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:-1;};
    for(size_t i=0;i<n;i+=2){const int high=digit(hex[i]),low=digit(hex[i+1]);Require(high>=0 && low>=0,"backend","Invalid output encoding","backend-protocol-error");staged.push_back(char((high<<4)|low));}
    process.send("{\"code\":\"data\"}");continue;
   }
   Keys(d,"backend.read",{"io","offset","length"});Require(Text(d["io"],"backend.io")=="read","backend","Unknown worker operation","backend-protocol-error");const auto at=Unsigned(d["offset"],"backend.offset",MaxByte);Require(d["length"].IsUint(),"backend.length","Expected bounded integer length","backend-protocol-error");const auto count=d["length"].GetUint();Require(count && count<=65536 && count<=MaxByte-at,"backend","Invalid bounded worker read","backend-protocol-error");if(total+count>16777216)return BackendResult("backend-byte-limit",query,total);
   std::vector<char> bytes(count);const auto read=source.read(query.resource,at,bytes.data(),count);
   if(!read.data() && read.code!="eof")return BackendResult(read.code,query,total,at,count);
   rapidjson::Document reply;reply.SetObject();auto& alloc=reply.GetAllocator();reply.AddMember("code",Value(read.code.c_str(),alloc),alloc);
   if(read.data()){total+=read.bytes;const char* digits="0123456789abcdef";std::string hex;hex.reserve(read.bytes*2);for(size_t i=0;i<read.bytes;++i){const auto byte=static_cast<unsigned char>(bytes[i]);hex.push_back(digits[byte>>4]);hex.push_back(digits[byte&15]);}reply.AddMember("hex",Value(hex.c_str(),rapidjson::SizeType(hex.size()),alloc),alloc);}process.send(lease_detail::Encode(reply));
  }
 }catch(const Error& e){return BackendResult(e.code,query,total);}catch(const std::exception& e){return BackendResult(e.what(),query,total);}
}
}}
