// MPEG-1 Layer III profile over the shared scalar audio bridge. GPL-3.0-or-later.
#pragma once
#include "DSUtil/HarvesterMP3.h"
#include "progressiverawaudio.h"
namespace foxicmp { namespace progressive {
struct Mp3Profile {
    using Layout=harvester::mp3::Layout;using Waiting=harvester::mp3::Waiting;
    static const char* Tag(){return "mp3";}static const char* Hint(){return "mp3";}static const char* Codec(){return "mp3";}
    static const char* Mode(){return "standalone-mp3";}static const char* Path(){return "audio.mp3.fmp4";}static const char* FramesKey(){return "mp3Frames";}
    static std::string Message(const char* text){std::string s=text;for(const auto* from:{"Raw FLAC","raw FLAC","native FLAC","scalar FLAC"}){size_t at=0;while((at=s.find(from,at))!=std::string::npos){s.replace(at,std::strlen(from),"MP3");at+=3;}}return s;}
};
using Mp3Bridge=RawAudioBridge<Mp3Profile>;
}}
