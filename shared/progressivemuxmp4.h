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
    static const char* Mode(const std::string& video,const std::string& audio){
        if(audio=="opus")return video=="vp9"?"growing-muxed-fmp4-vp9-opus":video=="av1"?"growing-muxed-fmp4-av1-opus":video=="hevc"?"growing-muxed-fmp4-hevc-opus":"growing-muxed-fmp4-h264-opus";
        return video=="vp9"?"growing-muxed-fmp4-vp9-aac-lc":video=="av1"?"growing-muxed-fmp4-av1-aac-lc":video=="hevc"?"growing-muxed-fmp4-hevc-aac-lc":"growing-muxed-fmp4-h264-aac-lc";
    }
    static const char* Name(){return "fmp4";}
    static const char* Video(){return "h264";}
    static bool AcceptsVideo(const std::string& codec){return codec==Video() || codec=="hevc" || codec=="av1" || codec=="vp9";}
    static Layout::Validate Validation(Layout::Validate validate,const std::string&,const std::string&){return validate;}
    static bool AcceptsAudio(const std::string& codec){return codec==Audio() || codec=="opus";}
    static const char* Audio(){return "aac-lc";}
    static const char* Path(unsigned k){return k?"audio.fmp4":"video.fmp4";}
    static const char* Container(unsigned k){return k?"m4a":"mp4";}
};
using MuxedMp4Bridge=SharedMuxedBridge<MuxedMp4Profile>;
}}
