// Verified shared WebM VP8/VP9/AV1 + Opus/Vorbis profiles. GPL-3.0-or-later.
#pragma once
#include "DSUtil/HarvesterWebMMuxed.h"
#include "progressivemuxscalar.h"
namespace foxicmp { namespace progressive {
struct MuxedWebmProfile {
    using Layout=harvester::muxwebm::Layout;
    static constexpr uint64_t MaxAppend=16777216,Tolerance=10000;
    static const char* Tag(){return "muxedWebm";}
    static const char* Hint(){return "webm-muxed";}
    static const char* Mode(const std::string& video,const std::string& audio){if(audio=="vorbis")return video=="vp8"?"growing-muxed-webm-vp8-vorbis":video=="av1"?"growing-muxed-webm-av1-vorbis":"growing-muxed-webm-vp9-vorbis";return video=="vp8"?"growing-muxed-webm-vp8-opus":video=="av1"?"growing-muxed-webm-av1-opus":"growing-muxed-webm-vp9-opus";}
    static const char* Name(){return "webm";}
    static const char* Video(){return "vp9";}
    static bool AcceptsVideo(const std::string& codec){return codec=="vp8" || codec=="vp9" || codec=="av1";}
    static Layout::Validate Validation(Layout::Validate validate,const std::string& video,const std::string& audio){
        return [validate,video,audio](unsigned k,const harvester::WebMInfo& info){
            harvester::webm::Check(info.codec==(k?audio:video),"Shared WebM actual codec differs from declared profile");validate(k,info);
        };
    }
    static bool AcceptsAudio(const std::string& codec){return codec=="opus" || codec=="vorbis";}
    static const char* Audio(){return "opus";}
    static const char* Path(unsigned k){return k?"audio.webm":"video.webm";}
    static const char* Container(unsigned){return "webm";}
};
using MuxedWebmBridge=SharedMuxedBridge<MuxedWebmProfile>;
}}
