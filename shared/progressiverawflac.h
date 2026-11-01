// Native FLAC profile over the shared scalar audio bridge. GPL-3.0-or-later.
#pragma once
#include "DSUtil/HarvesterRawFlac.h"
#include "progressiverawaudio.h"
namespace foxicmp { namespace progressive {
struct RawFlacProfile {
    using Layout=harvester::rawflac::Layout;using Waiting=harvester::rawflac::Waiting;
    static const char* Tag(){return "rawFlac";}static const char* Hint(){return "flac";}static const char* Codec(){return "flac";}
    static const char* Mode(){return "standalone-flac";}static const char* Path(){return "audio.raw-flac.fmp4";}static const char* FramesKey(){return "rawFrames";}
    static std::string Message(const char* text){return text;}
};
using RawFlacBridge=RawAudioBridge<RawFlacProfile>;
}}
