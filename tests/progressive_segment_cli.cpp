// Segment fixture: bytes/packets are test output, never a player IPC payload.
#include "../shared/progressivesegmentstager.h"
#include <iostream>
namespace p=foxicmp::progressive;
int main(int argc,char** argv) {
    if(argc!=3 || std::string(argv[1])!="--registry")return 2;
    const auto registry=std::filesystem::u8path(argv[2]);p::lease_detail::Directory receipts(registry,true);
    std::shared_ptr<p::SegmentStager> stager,retained;std::shared_ptr<std::atomic<bool>> receipt;std::string line;
    while(std::getline(std::cin,line))try {
        rapidjson::Document d;d.Parse(line.c_str());p::Require(!d.HasParseError() && d.IsObject(),"$","Invalid command");const auto action=p::Text(d["action"],"action");
        if(action=="readers") {std::cout<<p::ReaderRequest(registry,line)<<std::endl;continue;}
        if(action=="open")stager=std::make_shared<p::SegmentStager>(std::filesystem::u8path(p::Text(d["directory"],"directory",8192)),registry,p::lease_detail::Encode(d["session"]));
        else if(action=="publish") {p::Require(bool(stager),"source","No source");stager->publish(p::lease_detail::Encode(d["session"]));}
        else if(action=="stage" || action=="demux") {
            p::Require(bool(stager),"source","No source");auto result=stager->stage(p::Id(d["trackId"],"trackId"),size_t(p::Unsigned(d["segmentIndex"],"segmentIndex",127)),action=="demux");
            rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);out.AddMember("code",p::Value(result.code.c_str(),a),a);out.AddMember("reason",p::Value(result.reason.c_str(),a),a);out.AddMember("bytesRead",uint32_t(result.bytes.size()),a);
            out.AddMember("initializationResourceId",p::Value(result.initializationResource.c_str(),a),a);out.AddMember("resourceId",p::Value(result.resource.c_str(),a),a);
            p::Sha256 hash;hash.update(result.bytes.data(),result.bytes.size());out.AddMember("sha256",p::Value(hash.hex().c_str(),a),a);
            if(action=="stage" && result.bytes.size()<=1024) {
                std::string hex;const char* digits="0123456789abcdef";for(auto c:result.bytes){auto v=static_cast<unsigned char>(c);hex+=digits[v>>4];hex+=digits[v&15];}out.AddMember("hex",p::Value(hex.c_str(),a),a);
            }
            p::Value packets(rapidjson::kArrayType);
            for(const auto& sample:result.samples) {
                p::Value packet(rapidjson::kObjectType);
                packet.AddMember("offset",p::Value(std::to_string(sample.offset).c_str(),a),a);packet.AddMember("size",sample.size,a);
                packet.AddMember("start",p::Value(std::to_string(sample.start).c_str(),a),a);packet.AddMember("end",p::Value(std::to_string(sample.end).c_str(),a),a);
                packet.AddMember("decode",p::Value(std::to_string(sample.decode).c_str(),a),a);packet.AddMember("sync",sample.sync,a);
                p::Sha256 payload;payload.update(result.bytes.data()+sample.offset,sample.size);packet.AddMember("sha256",p::Value(payload.hex().c_str(),a),a);packets.PushBack(packet,a);
            }
            out.AddMember("packets",packets,a);std::cout<<p::lease_detail::Encode(out)<<std::endl;continue;
        } else if(action=="release") {if(stager)receipt=stager->releaseReceipt();stager.reset();}
        else if(action=="retain")retained=stager;
        else if(action=="dropRetained")retained.reset();
        else if(action=="released") {std::cout<<"{\"ok\":true,\"released\":"<<(receipt && receipt->load()?"true":"false")<<"}"<<std::endl;continue;}
        else if(action=="cancel") {if(stager)stager->cancel();}
        else p::Require(false,"action","Unknown command");
        std::cout<<"{\"ok\":true}"<<std::endl;
    } catch(const p::Error& error) {
        rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);out.AddMember("code",p::Value(error.code.c_str(),a),a);out.AddMember("error",p::Value(error.what(),a),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;
    } catch(const std::exception& error) {
        rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);out.AddMember("code","segment-error",a);out.AddMember("error",p::Value(error.what(),a),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;
    }
    return 0;
}
