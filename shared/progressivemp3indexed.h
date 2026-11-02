// Index-bearing MP3 with encoded-sample timeline, explicit unapplied gapless metadata.
// GPL-3.0-or-later.
#pragma once
#include "progressivemp3.h"
namespace foxicmp { namespace progressive {
struct Mp3IndexedProfile:Mp3Profile {
    using Layout=harvester::mp3::IndexedLayout;static constexpr bool Audible=false;
    static const char* Tag(){return "mp3Indexed";}static const char* Hint(){return "mp3-indexed";}
    static const char* Mode(){return "standalone-mp3-indexed";}static const char* Path(){return "audio.mp3-indexed.fmp4";}
};
template<class Profile,class Clock=RawAudioClock> class IndexedMp3Bridge:public RawAudioBridge<Profile,Clock> {
public:
    using RawAudioBridge<Profile,Clock>::RawAudioBridge;
    std::string details()const{return this->detailsWith([](const auto& layout,auto& d,auto& a){
        const auto& index=layout.indexHeader;Value v(rapidjson::kObjectType);v.AddMember("type",Value(index.kind.c_str(),a),a);v.AddMember("declaredFrames",index.frames,a);v.AddMember("declaredBytes",index.bytes,a);v.AddMember("encoder",Value(index.encoder.c_str(),a),a);v.AddMember("encoderDelayPresent",index.hasEncoderDelay,a);v.AddMember("encoderDelay",index.encoderDelay,a);v.AddMember("encoderPadding",index.encoderPadding,a);v.AddMember("gaplessTrimApplied",false,a);v.AddMember("encoderTagCrcVerified",index.tagCrcVerified,a);v.AddMember("timeline",Value(Profile::Audible?"audible-samples":"encoded-samples",a),a);if(Profile::Audible)v.AddMember("outputTrimRequired",true,a);
        Value tag(rapidjson::kObjectType);tag.AddMember("version",1,a);tag.AddMember("state",Value(index.TagState(),a),a);tag.AddMember("complete",index.tagComplete,a);tag.AddMember("revision",index.tagRevision,a);tag.AddMember("crcChecked",index.tagCrcChecked,a);tag.AddMember("crcVerified",index.tagCrcVerified,a);tag.AddMember("musicCrcVerified",false,a);
        if(index.tagCrcChecked){tag.AddMember("storedCrc16",index.tagCrc,a);tag.AddMember("calculatedCrc16",index.calculatedTagCrc,a);tag.AddMember("coveredBytes",uint32_t(index.tagCoveredBytes),a);}v.AddMember("encoderTag",tag,a);
        const auto plan=harvester::mp3::TrimPlan::Analyze(index,layout.info.rate,529);Value trim(rapidjson::kObjectType);trim.AddMember("analysisOnly",!Profile::Audible,a);trim.AddMember("ready",plan.ready,a);trim.AddMember("reasonCode",Value(plan.reason,a),a);trim.AddMember("decoderPolicy","ffmpeg-mpeg1-layer3-529-v1",a);trim.AddMember("decoderDelaySamples",plan.decoderDelay,a);trim.AddMember("sampleRate",plan.rate,a);trim.AddMember("codedSamples",Value(std::to_string(plan.codedSamples).c_str(),a),a);
        if(plan.ready){trim.AddMember("beginSample",Value(std::to_string(plan.begin).c_str(),a),a);trim.AddMember("endSample",Value(std::to_string(plan.end).c_str(),a),a);trim.AddMember("audibleSamples",Value(std::to_string(plan.audibleSamples).c_str(),a),a);trim.AddMember("audibleDuration100ns",Value(std::to_string(harvester::mp3::TrimPlan::Ticks(plan.audibleSamples,plan.rate)).c_str(),a),a);trim.AddMember("decoderTimestampOffset100ns",Value(std::to_string(harvester::mp3::TrimPlan::Ticks(plan.begin,plan.rate)).c_str(),a),a);}v.AddMember("trimPlan",trim,a);d.AddMember("mp3Index",v,a);
    });}
};
using Mp3IndexedBridge=IndexedMp3Bridge<Mp3IndexedProfile>;
struct Mp3GaplessProfile:Mp3IndexedProfile {
    static constexpr bool Audible=true;
    static const char* Tag(){return "mp3Gapless";}static const char* Hint(){return "mp3-gapless";}
    static const char* Mode(){return "standalone-mp3-gapless";}static const char* Path(){return "audio.mp3-gapless.fmp4";}
};
struct Mp3AudibleClock {
    template<class Layout> static harvester::mp3::TrimPlan Plan(const Layout& l){
        auto p=harvester::mp3::TrimPlan::Analyze(l.indexHeader,l.info.rate,529);
        Require(p.ready,"mp3Gapless",p.reason,"unsupported_media");return p;
    }
    template<class Layout> static uint64_t Total(const Layout& l){return Plan(l).audibleSamples;}
    template<class Layout> static uint64_t Samples(const Layout& l){auto p=Plan(l);return l.Samples()<=p.begin?0:(std::min)(l.Samples(),p.end)-p.begin;}
    template<class Layout> static int64_t Offset(const Layout& l){auto p=Plan(l);return int64_t(harvester::mp3::TrimPlan::Ticks(p.begin,p.rate));}
    template<class Layout> static uint64_t Bytes(const Layout& l,bool complete){
        auto p=Plan(l);if(complete || l.Samples()<p.end)return l.view.bytes;
        // Withhold the fragment containing the audible end until writer
        // complete. Otherwise atrim can signal EOF while the source is growing.
        const auto frame=(p.end-1)/1152;
        Require(l.view.spans.size()==1+l.Frames()*3 && frame<l.Frames(),"mp3Gapless","Invalid owned projection spans","unsupported_media");
        return l.view.spans.at(size_t(1+frame*3)).offset;
    }
};
using Mp3GaplessBridge=IndexedMp3Bridge<Mp3GaplessProfile,Mp3AudibleClock>;
}}
