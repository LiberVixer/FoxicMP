// Verified shared WebM VP9/AV1 + Opus profiles. GPL-3.0-or-later.
#pragma once
#include "DSUtil/HarvesterWebMMuxed.h"
#include "progressivemuxscalar.h"
namespace foxicmp { namespace progressive {
struct MuxedWebmProfile {
    using Layout=harvester::muxwebm::Layout;
    static constexpr uint64_t MaxAppend=16777216,Tolerance=10000;
    static const char* Tag(){return "muxedWebm";}
    static const char* Hint(){return "webm-muxed";}
    static const char* Mode(const std::string& video){return video=="av1"?"growing-muxed-webm-av1-opus":"growing-muxed-webm-vp9-opus";}
    static const char* Name(){return "webm";}
    static const char* Video(){return "vp9";}
    static bool AcceptsVideo(const std::string& codec){return codec=="vp9" || codec=="av1";}
    static Layout::Validate Validation(Layout::Validate validate,const std::string& video){
        return [validate,video](unsigned k,const harvester::WebMInfo& info){
            harvester::webm::Check(info.codec==(k?"opus":video),"Shared WebM actual codec differs from declared profile");validate(k,info);
        };
    }
    static const char* Audio(){return "opus";}
    static const char* Path(unsigned k){return k?"audio.webm":"video.webm";}
    static const char* Container(unsigned){return "webm";}
};
using MuxedWebmBridge=SharedMuxedBridge<MuxedWebmProfile>;
}}
