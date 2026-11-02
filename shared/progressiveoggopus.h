#pragma once
#include "DSUtil/HarvesterOggOpus.h"
#include "progressiverawaudio.h"
namespace foxicmp { namespace progressive {
struct OggOpusProfile {
    using Layout=harvester::oggopus::Layout;using Waiting=harvester::oggopus::Waiting;
    static const char* Tag(){return "oggOpus";}static const char* Hint(){return "ogg-opus";}static const char* Codec(){return "opus";}
    static const char* Mode(){return "standalone-ogg-opus";}static const char* Path(){return "audio.ogg-opus.webm";}static const char* FramesKey(){return "oggOpusPackets";}
    static std::string Message(const char* text){std::string s=text;for(const auto* from:{"Raw FLAC","raw FLAC","native FLAC","scalar FLAC"}){size_t at=0;while((at=s.find(from,at))!=std::string::npos){s.replace(at,std::strlen(from),"Ogg Opus");at+=8;}}return s;}
};
class OggOpusBridge:public RawAudioBridge<OggOpusProfile> {
public:
    using RawAudioBridge<OggOpusProfile>::RawAudioBridge;
    harvester::Snapshot snapshot()const{auto s=RawAudioBridge<OggOpusProfile>::snapshot();s.tracks[0].container="webm";return s;}
    std::string details()const{rapidjson::Document d;d.Parse(RawAudioBridge<OggOpusProfile>::details().c_str());d["decoderContainer"].SetString("webm",d.GetAllocator());return lease_detail::Encode(d);}
};
}}
