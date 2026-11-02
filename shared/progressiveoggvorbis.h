#pragma once
#include "DSUtil/HarvesterOggVorbis.h"
#include "progressiverawaudio.h"
namespace foxicmp { namespace progressive {
struct OggVorbisProfile {
    using Layout=harvester::oggvorbis::Layout;using Waiting=harvester::oggvorbis::Waiting;
    static const char* Tag(){return "oggVorbis";}static const char* Hint(){return "ogg-vorbis";}static const char* Codec(){return "vorbis";}
    static const char* Mode(){return "standalone-ogg-vorbis";}static const char* Path(){return "audio.ogg-vorbis.webm";}static const char* FramesKey(){return "oggVorbisPackets";}
    static std::string Message(const char* text){std::string s=text;for(const auto* from:{"Raw FLAC","raw FLAC","native FLAC","scalar FLAC"}){size_t at=0;while((at=s.find(from,at))!=std::string::npos){s.replace(at,std::strlen(from),"Ogg Vorbis");at+=10;}}return s;}
};
class OggVorbisBridge:public RawAudioBridge<OggVorbisProfile> {
public:
    using RawAudioBridge<OggVorbisProfile>::RawAudioBridge;
    harvester::Snapshot snapshot()const{auto s=RawAudioBridge<OggVorbisProfile>::snapshot();s.tracks[0].container="webm";return s;}
    std::string details()const{rapidjson::Document d;d.Parse(RawAudioBridge<OggVorbisProfile>::details().c_str());d["decoderContainer"].SetString("webm",d.GetAllocator());return lease_detail::Encode(d);}
};
}}
