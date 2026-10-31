// Verified shared WebM VP9/Opus profile. GPL-3.0-or-later.
#pragma once
#include "DSUtil/HarvesterWebMMuxed.h"
#include "progressivemuxscalar.h"
namespace foxicmp { namespace progressive {
struct MuxedWebmProfile {
    using Layout=harvester::muxwebm::Layout;
    static constexpr uint64_t MaxAppend=16777216,Tolerance=10000;
    static const char* Tag(){return "muxedWebm";}
    static const char* Hint(){return "webm-muxed";}
    static const char* Mode(){return "growing-muxed-webm-vp9-opus";}
    static const char* Name(){return "webm";}
    static const char* Video(){return "vp9";}
    static const char* Audio(){return "opus";}
    static const char* Path(unsigned k){return k?"audio.webm":"video.webm";}
    static const char* Container(unsigned){return "webm";}
};
using MuxedWebmBridge=SharedMuxedBridge<MuxedWebmProfile>;
}}
