// Native internal TS transport fixture. No decoder, network or player IPC.
#include "DSUtil/HarvesterTS.h"
#include "../shared/progressivesession.h"
#include "../shared/progressivesha256.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
namespace p=foxicmp::progressive;
namespace ts=harvester::ts;
static std::string Encode(const rapidjson::Value& value) {
    rapidjson::StringBuffer buffer;rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);value.Accept(writer);return {buffer.GetString(),buffer.GetSize()};
}
static std::vector<uint8_t> Load(const rapidjson::Value& path,size_t limit) {
    std::ifstream input(std::filesystem::u8path(p::Text(path,"path",8192)),std::ios::binary|std::ios::ate);
    ts::Check(bool(input),"Cannot open TS test input","test-io");const auto size=input.tellg();
    ts::Check(size>=0 && uint64_t(size)<=limit,"TS test file exceeds its bound","ts-limit");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));input.seekg(0);
    ts::Check(bytes.empty() || bool(input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()))),"Cannot read TS test input","test-io");return bytes;
}
int main() {
    std::optional<ts::Program> program;std::optional<ts::Segment> last;std::string line;
    while(std::getline(std::cin,line))try {
        p::Require(line.size()<=65536,"$","Oversized test command");rapidjson::Document d;d.Parse(line.c_str());
        p::Require(!d.HasParseError() && d.IsObject() && d.HasMember("action"),"$","Invalid test command");
        const auto action=p::Text(d["action"],"action");std::vector<uint8_t> media;
        if(action=="init") {
            p::Require(d.HasMember("path") && d.HasMember("kind"),"$","Missing initialization fields");
            const auto kind=p::Text(d["kind"],"kind");p::Require(kind=="video" || kind=="audio","kind","Invalid track kind");
            const auto bytes=Load(d["path"],ts::MaxInitBytes);
            auto next=ts::Initialization({bytes.data(),bytes.size()},kind=="audio"?1:0);program=std::move(next);last.reset();
        }else if(action=="stage") {
            p::Require(bool(program) && d.HasMember("path"),"$","Missing program or segment");media=Load(d["path"],ts::MaxSegmentBytes);
            auto next=ts::Media(*program,{media.data(),media.size()});last=std::move(next);
        }else if(action=="reset"){program.reset();last.reset();}
        else p::Require(action=="status","action","Unknown TS test action");
        rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);
        if(program) {
            out.AddMember("program",program->number,a);out.AddMember("pmtPid",program->pmtPid,a);out.AddMember("streamPid",program->streamPid,a);
            out.AddMember("pcrPid",program->pcrPid,a);out.AddMember("streamType",program->streamType,a);
        }
        if(last) {
            out.AddMember("packetCount",uint32_t(last->packets.size()),a);out.AddMember("pcrCount",uint32_t(last->pcrCount),a);
            out.AddMember("firstPcr",p::Value(std::to_string(last->firstPcr).c_str(),a),a);out.AddMember("lastPcr",p::Value(std::to_string(last->lastPcr).c_str(),a),a);
            if(action=="stage") {
                p::Value packets(rapidjson::kArrayType);
                for(const auto& packet:last->packets) {
                    p::Value item(rapidjson::kObjectType);item.AddMember("pts",p::Value(std::to_string(packet.pts).c_str(),a),a);
                    item.AddMember("dts",p::Value(std::to_string(packet.dts).c_str(),a),a);item.AddMember("size",uint32_t(packet.size),a);
                    item.AddMember("transportOffset",uint32_t(packet.transportOffset),a);item.AddMember("transportEnd",uint32_t(packet.transportEnd),a);
                    p::Sha256 hash;p::Value ranges(rapidjson::kArrayType);
                    for(const auto& range:packet.payload) {
                        ts::Check(range.offset<=media.size() && range.size<=media.size()-range.offset,"Invalid staged payload range");
                        hash.update(media.data()+range.offset,range.size);p::Value pair(rapidjson::kArrayType);pair.PushBack(uint32_t(range.offset),a);pair.PushBack(uint32_t(range.size),a);ranges.PushBack(pair,a);
                    }
                    item.AddMember("sha256",p::Value(hash.hex().c_str(),a),a);item.AddMember("ranges",ranges,a);packets.PushBack(item,a);
                }
                out.AddMember("packets",packets,a);
            }
        }
        std::cout<<Encode(out)<<std::endl;
    }catch(const ts::Error& error) {
        rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);
        out.AddMember("code",p::Value(error.code.c_str(),a),a);out.AddMember("error",p::Value(error.what(),a),a);std::cout<<Encode(out)<<std::endl;
    }catch(const std::exception& error) {
        rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);out.AddMember("code","test-error",a);
        out.AddMember("error",p::Value(error.what(),a),a);std::cout<<Encode(out)<<std::endl;
    }
    return 0;
}
