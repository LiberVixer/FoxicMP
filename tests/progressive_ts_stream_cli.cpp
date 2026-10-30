// Internal TS ResourceSource fixture, never a player IPC endpoint.
#include "../shared/progressivetsstream.h"
#include <iostream>
namespace p=foxicmp::progressive;
static void Emit(p::ResourceRead value,const std::vector<char>& bytes,bool untouched=true) {
    rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);out.AddMember("code",p::Value(value.code.c_str(),a),a);out.AddMember("bytesRead",uint32_t(value.bytes),a);out.AddMember("untouchedOnError",untouched,a);
    std::string hex;const char* digits="0123456789abcdef";for(size_t i=0;i<value.bytes;++i){const auto c=uint8_t(bytes[i]);hex+=digits[c>>4];hex+=digits[c&15];}
    out.AddMember("hex",p::Value(hex.c_str(),a),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;
}
int main(int argc,char** argv) {
    if(argc!=3 || std::string(argv[1])!="--registry")return 2;
    const auto registry=std::filesystem::u8path(argv[2]);p::lease_detail::Directory receipts(registry,true);
    std::shared_ptr<p::TsResourceStream> stream,retained;std::shared_ptr<std::atomic<bool>> released;std::string line;
    while(std::getline(std::cin,line))try {
        rapidjson::Document d;d.Parse(line.c_str());p::Require(!d.HasParseError() && d.IsObject() && d.HasMember("action"),"$","Invalid command");const auto action=p::Text(d["action"],"action");
        if(action=="readers" || action=="readerStatus" || action=="readerForget"){std::cout<<p::ReaderRequest(registry,line)<<std::endl;continue;}
        if(action=="open")stream=std::make_shared<p::TsResourceStream>(std::filesystem::u8path(p::Text(d["directory"],"directory",8192)),registry,p::lease_detail::Encode(d["session"]));
        else if(action=="retain")retained=stream;
        else if(action=="dropRetained")retained.reset();
        else if(action=="release"){if(stream)released=stream->releaseReceipt();stream.reset();}
        else if(action=="released"){std::cout<<"{\"ok\":true,\"released\":"<<(released && released->load()?"true":"false")<<"}"<<std::endl;continue;}
        else {
            p::Require(bool(stream),"stream","No TS stream");
            if(action=="publish")stream->publish(p::lease_detail::Encode(d["session"]));
            else if(action=="cancel")stream->cancel();
            else if(action=="verify")stream->verify();
            else if(action=="status") {
                const auto s=stream->status();rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);out.AddMember("revision",p::Value(std::to_string(s.revision).c_str(),a),a);
                out.AddMember("errorCode",p::Value(s.code.c_str(),a),a);out.AddMember("available",p::Value(std::to_string(s.available).c_str(),a),a);out.AddMember("complete",s.complete,a);out.AddMember("resources",uint32_t(s.resources),a);out.AddMember("pages",uint32_t(s.pages),a);
                p::Value tracks(rapidjson::kArrayType);for(size_t i=0;i<2;++i){p::Value t(rapidjson::kObjectType);t.AddMember("bytes",p::Value(std::to_string(s.bytes[i]).c_str(),a),a);t.AddMember("units",uint32_t(s.units[i]),a);t.AddMember("initialized",s.initialized[i],a);t.AddMember("complete",s.trackComplete[i],a);t.AddMember("ready",p::Value(std::to_string(s.ready[i]).c_str(),a),a);tracks.PushBack(t,a);}out.AddMember("tracks",tracks,a);std::cout<<p::lease_detail::Encode(out)<<std::endl;continue;
            }else if(action=="read") {
                const auto count=size_t(p::Unsigned(d["length"],"length",65536));std::vector<char> bytes(count,'?');
                const auto read=stream->read(d["track"].GetUint(),p::Unsigned(d["offset"],"offset",p::MaxByte),bytes.data(),count);
                Emit(read,bytes,read.data() || std::all_of(bytes.begin(),bytes.end(),[](char c){return c=='?';}));continue;
            }else if(action=="sample") {
                const auto packet=stream->sample(d["track"].GetUint(),size_t(p::Unsigned(d["index"],"index",harvester::ts::MaxIndexUnits+1)),d.HasMember("encoded") && d["encoded"].GetBool());
                rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);out.AddMember("code",p::Value(packet.result.code.c_str(),a),a);out.AddMember("size",uint32_t(packet.bytes.size()),a);
                p::Sha256 hash;hash.update(packet.bytes.data(),packet.bytes.size());out.AddMember("sha256",p::Value(hash.hex().c_str(),a),a);out.AddMember("start",p::Value(std::to_string(packet.sample.start).c_str(),a),a);out.AddMember("end",p::Value(std::to_string(packet.sample.end).c_str(),a),a);out.AddMember("decode",p::Value(std::to_string(packet.sample.decode).c_str(),a),a);out.AddMember("sync",packet.sample.sync,a);
                if(d.HasMember("includeBytes") && d["includeBytes"].GetBool()){std::string hex;const char* digits="0123456789abcdef";for(const auto c:packet.bytes){hex+=digits[uint8_t(c)>>4];hex+=digits[uint8_t(c)&15];}out.AddMember("hex",p::Value(hex.c_str(),a),a);}
                std::cout<<p::lease_detail::Encode(out)<<std::endl;continue;
            }else if(action=="seek") {
                const auto position=stream->seekSample(d["track"].GetUint(),p::Signed(d["position"],"position"));std::cout<<"{\"ok\":true,\"sample\":"<<position<<"}"<<std::endl;continue;
            }else p::Require(false,"action","Unknown command");
        }
        std::cout<<"{\"ok\":true}"<<std::endl;
    }catch(const p::Error& error){rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);out.AddMember("code",p::Value(error.code.c_str(),a),a);out.AddMember("error",p::Value(error.what(),a),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;}
     catch(const harvester::ts::Error& error){rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);out.AddMember("code",p::Value(error.code.c_str(),a),a);out.AddMember("error",p::Value(error.what(),a),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;}
     catch(const std::exception& error){rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);out.AddMember("code","ts-stream-error",a);out.AddMember("error",p::Value(error.what(),a),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;}
    return 0;
}
