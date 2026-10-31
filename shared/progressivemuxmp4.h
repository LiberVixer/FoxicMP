// Verified shared fMP4 profile over the common scalar bridge. GPL-3.0-or-later.
#pragma once
#include "DSUtil/HarvesterMP4Muxed.h"
#include "progressivemuxscalar.h"
namespace foxicmp { namespace progressive {
struct MuxedMp4Profile {
    using Layout=harvester::muxmp4::Layout;
    static constexpr uint64_t MaxAppend=16777216,Tolerance=0;
    static const char* Tag(){return "muxedMp4";}
    static const char* Hint(){return "fmp4-muxed";}
    static const char* Mode(){return "growing-muxed-fmp4-h264-aac-lc";}
    static const char* Name(){return "fmp4";}
    static const char* Video(){return "h264";}
    static const char* Audio(){return "aac-lc";}
    static const char* Path(unsigned k){return k?"audio.fmp4":"video.fmp4";}
    static const char* Container(unsigned k){return k?"m4a":"mp4";}
};
using MuxedMp4Bridge=SharedMuxedBridge<MuxedMp4Profile>;
}}
