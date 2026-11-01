// Index-bearing MP3 with encoded-sample timeline, explicit unapplied gapless metadata.
// GPL-3.0-or-later.
#pragma once
#include "progressivemp3.h"
namespace foxicmp { namespace progressive {
struct Mp3IndexedProfile:Mp3Profile {
    using Layout=harvester::mp3::IndexedLayout;
    static const char* Tag(){return "mp3Indexed";}static const char* Hint(){return "mp3-indexed";}
    static const char* Mode(){return "standalone-mp3-indexed";}static const char* Path(){return "audio.mp3-indexed.fmp4";}
};
class Mp3IndexedBridge:public RawAudioBridge<Mp3IndexedProfile> {
public:
    using RawAudioBridge<Mp3IndexedProfile>::RawAudioBridge;
    std::string details()const{return detailsWith([](const auto& layout,auto& d,auto& a){
        const auto& index=layout.indexHeader;Value v(rapidjson::kObjectType);v.AddMember("type",Value(index.kind.c_str(),a),a);v.AddMember("declaredFrames",index.frames,a);v.AddMember("declaredBytes",index.bytes,a);v.AddMember("encoder",Value(index.encoder.c_str(),a),a);v.AddMember("encoderDelayPresent",index.hasEncoderDelay,a);v.AddMember("encoderDelay",index.encoderDelay,a);v.AddMember("encoderPadding",index.encoderPadding,a);v.AddMember("gaplessTrimApplied",false,a);v.AddMember("encoderTagCrcVerified",false,a);v.AddMember("timeline","encoded-samples",a);d.AddMember("mp3Index",v,a);
    });}
};
}}
