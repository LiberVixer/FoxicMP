#pragma once
#include "DSUtil/HarvesterEAC3.h"
#include "progressiverawaudio.h"
namespace foxicmp { namespace progressive {
struct Eac3Profile {
    using Layout=harvester::eac3::Layout;using Waiting=harvester::eac3::Waiting;
    static const char* Tag(){return "eac3";}static const char* Hint(){return "eac3";}static const char* Codec(){return "eac3";}
    static const char* Mode(){return "standalone-eac3";}static const char* Path(){return "audio.eac3.fmp4";}static const char* FramesKey(){return "eac3Frames";}
    static std::string Message(const char* text){std::string s=text;for(const auto* from:{"Raw FLAC","raw FLAC","native FLAC","scalar FLAC"}){size_t at=0;while((at=s.find(from,at))!=std::string::npos){s.replace(at,std::strlen(from),"E-AC-3");at+=6;}}return s;}
};
using Eac3Bridge=RawAudioBridge<Eac3Profile>;
}}
