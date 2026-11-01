// ADTS AAC-LC profile over the shared scalar audio bridge. GPL-3.0-or-later.
#pragma once
#include "DSUtil/HarvesterADTS.h"
#include "progressiverawaudio.h"
namespace foxicmp { namespace progressive {
struct AdtsProfile {
    using Layout=harvester::adts::Layout;using Waiting=harvester::adts::Waiting;
    static const char* Tag(){return "adts";}static const char* Hint(){return "adts";}static const char* Codec(){return "aac-lc";}
    static const char* Mode(){return "standalone-adts-aac-lc";}static const char* Path(){return "audio.adts.fmp4";}static const char* FramesKey(){return "adtsFrames";}
    static std::string Message(const char* text){std::string s=text;for(const auto* from:{"Raw FLAC","raw FLAC","native FLAC","scalar FLAC"}){size_t at=0;while((at=s.find(from,at))!=std::string::npos){s.replace(at,std::strlen(from),"ADTS AAC");at+=8;}}return s;}
};
using AdtsBridge=RawAudioBridge<AdtsProfile>;
}}
