#pragma once
#include "DSUtil/HarvesterM4A.h"
#include "progressiverawaudio.h"
namespace foxicmp { namespace progressive {
struct M4AProfile {
    using Layout=harvester::m4a::Layout;using Waiting=harvester::m4a::Waiting;
    static const char* Tag(){return "m4a";}static const char* Hint(){return "m4a";}static const char* Codec(){return "aac-lc";}
    static const char* Mode(){return "standalone-indexed-m4a-aac";}static const char* Path(){return "audio.m4a.fmp4";}static const char* FramesKey(){return "m4aPackets";}
    static std::string Message(const char* text){return std::string("M4A: ")+text;}
};
template<> struct RawAudioProfileHooks<M4AProfile> {
    static constexpr bool MetadataRanges=true;
    static void Configure(M4AProfile::Layout& l,const Resource& r){
        Require(r.known && r.length>0 && r.ranges.size()<=2,"m4a","M4A requires known length and prefix plus optional index range","unsupported_media");
        std::vector<std::pair<uint64_t,uint64_t>> ranges;for(const auto& x:r.ranges)ranges.emplace_back(x.offset,x.length);l.Configure(r.length,ranges);
    }
    static std::string Codec(const M4AProfile::Layout& l){return l.info.codec;}
    static bool AcceptsCodec(const std::string& codec){return codec=="aac-lc" || codec=="he-aac";}
};
class M4ABridge:public RawAudioBridge<M4AProfile> {
public:
    using RawAudioBridge<M4AProfile>::RawAudioBridge;
    std::string details()const{return detailsWith([](const auto& layout,auto& d,auto& a){
        Value plan(rapidjson::kObjectType);plan.AddMember("sampleRate",layout.info.rate,a);plan.AddMember("channels",layout.info.channels,a);
        plan.AddMember("frameSamples",layout.info.frame,a);plan.AddMember("initialSkip",layout.info.skip,a);plan.AddMember("samples",Value(std::to_string(layout.info.total).c_str(),a),a);plan.AddMember("packets",Value(std::to_string(layout.info.packets).c_str(),a),a);
        d.AddMember("m4aWindow",plan,a);
    });}
};
}}
