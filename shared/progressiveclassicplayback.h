// Classic-specific decoder adapter capability.
#pragma once
#include "progressivefmp4bridge.h"
namespace foxicmp { namespace progressive {
inline const char* ClassicMp3GaplessCapability(){
    return R"({"version":1,"mode":"standalone-mp3-gapless-v1","profiles":["standalone-mp3-gapless"],"container":"mp3-gapless","decoderContainer":"fmp4","codec":"mp3","mpegVersion":1,"layer":3,"channels":[1,2],"sampleRates":[44100,48000],"headerTypes":["Xing","Info"],"encoderTagCrcRequired":true,"timeline":"audible-samples","pcmWindowAdapter":"classic-directshow-mp3-window-v1","gaplessTrim":true,"decoderDelaySamples":529,"seekPrerollMilliseconds":250,"trackCount":1,"resourceCount":1,"integrityRequired":true,"durationRequired":true,"live":false,"segments":false,"transcoding":false,"originalsModified":false})";
}
inline std::string ClassicPlaybackCapabilities(){
    rapidjson::Document d;d.Parse(Fmp4V2Capabilities());auto& a=d.GetAllocator();
    rapidjson::Document cap;cap.Parse(ClassicMp3GaplessCapability());d["standalone"].AddMember("mp3Gapless",Value(cap,a),a);
    d["profiles"].PushBack("standalone-mp3-gapless",a);d["containers"].PushBack("mp3-gapless",a);
    return lease_detail::Encode(d);
}
}}
