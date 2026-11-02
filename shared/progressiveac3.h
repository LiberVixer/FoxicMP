#pragma once
#include "DSUtil/HarvesterAC3.h"
#include "progressiverawaudio.h"
namespace foxicmp { namespace progressive {
struct Ac3Profile {
    using Layout=harvester::ac3::Layout;using Waiting=harvester::ac3::Waiting;
    static const char* Tag(){return "ac3";}static const char* Hint(){return "ac3";}static const char* Codec(){return "ac3";}
    static const char* Mode(){return "standalone-ac3";}static const char* Path(){return "audio.ac3.fmp4";}static const char* FramesKey(){return "ac3Frames";}
    static std::string Message(const char* text){std::string s=text;for(const auto* from:{"Raw FLAC","raw FLAC","native FLAC","scalar FLAC"}){size_t at=0;while((at=s.find(from,at))!=std::string::npos){s.replace(at,std::strlen(from),"AC-3");at+=4;}}return s;}
};
using Ac3Bridge=RawAudioBridge<Ac3Profile>;
}}
