// RIFF/WAVE PCM16 profile over the shared scalar audio bridge.
#pragma once
#include "DSUtil/HarvesterWav.h"
#include "progressiverawaudio.h"
namespace foxicmp { namespace progressive {
struct WavProfile {
    using Layout=harvester::wav::Layout;using Waiting=harvester::wav::Waiting;
    static const char* Tag(){return "wav";}static const char* Hint(){return "wav";}static const char* Codec(){return "pcm-s16le";}
    static const char* Mode(){return "standalone-wav-pcm16";}static const char* Path(){return "audio.wav-pcm16.fmp4";}static const char* FramesKey(){return "wavPackets";}
    static std::string Message(const char* text){std::string s=text;for(const auto* from:{"Raw FLAC","raw FLAC","native FLAC","scalar FLAC"}){size_t at=0;while((at=s.find(from,at))!=std::string::npos){s.replace(at,std::strlen(from),"WAV PCM");at+=7;}}return s;}
};
using WavBridge=RawAudioBridge<WavProfile>;
struct Wav24Profile:WavProfile {
    using Layout=harvester::wav::Layout24;
    static const char* Tag(){return "wavPcm24";}static const char* Hint(){return "wav-pcm24";}static const char* Codec(){return "pcm-s24le";}
    static const char* Mode(){return "standalone-wav-pcm24";}static const char* Path(){return "audio.wav-pcm24.fmp4";}
};
using Wav24Bridge=RawAudioBridge<Wav24Profile>;
}}
