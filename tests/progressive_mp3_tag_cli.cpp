// Scalar MP3 metadata/trim-plan fixture; no UI, decoder or audio device.
#include "../shared/progressivesession.h"
#include "DSUtil/HarvesterMP3.h"
#include <iostream>
namespace p=foxicmp::progressive;
static harvester::mp3::Bytes bytes(const rapidjson::Value& input){
    const auto hex=p::Text(input,"hex",harvester::mp3::MaxFrame*2);
    if(hex.size()%2)throw std::runtime_error("Odd hex input");
    harvester::mp3::Bytes out;out.reserve(hex.size()/2);
    const auto digit=[](char c){if(c>='0' && c<='9')return c-'0';if(c>='a' && c<='f')return c-'a'+10;throw std::runtime_error("Invalid hex digit");};
    for(size_t n=0;n<hex.size();n+=2)out.push_back(uint8_t((digit(hex[n])<<4)|digit(hex[n+1])));
    return out;
}
int main(){
    std::string line;
    while(std::getline(std::cin,line))try {
        rapidjson::Document d;d.Parse(line.c_str());if(!d.IsObject() || !d.HasMember("hex"))throw std::runtime_error("Invalid request");
        harvester::mp3::Info info;harvester::mp3::IndexHeader tag;
        const auto parse=[&](const rapidjson::Value& value){auto b=bytes(value);if(b.size()<4)throw std::runtime_error("Short frame");info.parse(b.data());tag.parse(b,info);};
        if(d.HasMember("previousHex"))parse(d["previousHex"]);
        parse(d["hex"]);
        const auto rate=d.HasMember("planRate")?d["planRate"].GetUint():info.rate;
        const auto delay=d.HasMember("decoderDelay")?d["decoderDelay"].GetUint():529;
        const auto plan=harvester::mp3::TrimPlan::Analyze(tag,rate,delay);
        rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);out.AddMember("state",p::Value(tag.TagState(),a),a);out.AddMember("crcChecked",tag.tagCrcChecked,a);out.AddMember("crcVerified",tag.tagCrcVerified,a);out.AddMember("storedCrc16",tag.tagCrc,a);out.AddMember("calculatedCrc16",tag.calculatedTagCrc,a);out.AddMember("coveredBytes",uint32_t(tag.tagCoveredBytes),a);out.AddMember("ready",plan.ready,a);out.AddMember("reasonCode",p::Value(plan.reason,a),a);out.AddMember("begin",uint64_t(plan.begin),a);out.AddMember("end",uint64_t(plan.end),a);out.AddMember("audibleSamples",uint64_t(plan.audibleSamples),a);
        rapidjson::StringBuffer buffer;rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);out.Accept(writer);std::cout<<buffer.GetString()<<std::endl;
    }catch(const std::exception& e){std::cout<<"{\"ok\":false}"<<std::endl;}
}
