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
        const auto& index=layout.indexHeader;Value v(rapidjson::kObjectType);v.AddMember("type",Value(index.kind.c_str(),a),a);v.AddMember("declaredFrames",index.frames,a);v.AddMember("declaredBytes",index.bytes,a);v.AddMember("encoder",Value(index.encoder.c_str(),a),a);v.AddMember("encoderDelayPresent",index.hasEncoderDelay,a);v.AddMember("encoderDelay",index.encoderDelay,a);v.AddMember("encoderPadding",index.encoderPadding,a);v.AddMember("gaplessTrimApplied",false,a);v.AddMember("encoderTagCrcVerified",index.tagCrcVerified,a);v.AddMember("timeline","encoded-samples",a);
        Value tag(rapidjson::kObjectType);tag.AddMember("version",1,a);tag.AddMember("state",Value(index.TagState(),a),a);tag.AddMember("complete",index.tagComplete,a);tag.AddMember("revision",index.tagRevision,a);tag.AddMember("crcChecked",index.tagCrcChecked,a);tag.AddMember("crcVerified",index.tagCrcVerified,a);tag.AddMember("musicCrcVerified",false,a);
        if(index.tagCrcChecked){tag.AddMember("storedCrc16",index.tagCrc,a);tag.AddMember("calculatedCrc16",index.calculatedTagCrc,a);tag.AddMember("coveredBytes",uint32_t(index.tagCoveredBytes),a);}v.AddMember("encoderTag",tag,a);
        const auto plan=harvester::mp3::TrimPlan::Analyze(index,layout.info.rate,529);Value trim(rapidjson::kObjectType);trim.AddMember("analysisOnly",true,a);trim.AddMember("ready",plan.ready,a);trim.AddMember("reasonCode",Value(plan.reason,a),a);trim.AddMember("decoderPolicy","ffmpeg-mpeg1-layer3-529-v1",a);trim.AddMember("decoderDelaySamples",plan.decoderDelay,a);trim.AddMember("sampleRate",plan.rate,a);trim.AddMember("codedSamples",Value(std::to_string(plan.codedSamples).c_str(),a),a);
        if(plan.ready){trim.AddMember("beginSample",Value(std::to_string(plan.begin).c_str(),a),a);trim.AddMember("endSample",Value(std::to_string(plan.end).c_str(),a),a);trim.AddMember("audibleSamples",Value(std::to_string(plan.audibleSamples).c_str(),a),a);trim.AddMember("audibleDuration100ns",Value(std::to_string(harvester::mp3::TrimPlan::Ticks(plan.audibleSamples,plan.rate)).c_str(),a),a);trim.AddMember("decoderTimestampOffset100ns",Value(std::to_string(harvester::mp3::TrimPlan::Ticks(plan.begin,plan.rate)).c_str(),a),a);}v.AddMember("trimPlan",trim,a);d.AddMember("mp3Index",v,a);
    });}
};
}}
