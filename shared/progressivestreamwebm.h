// Explicit VP8/AV1 WebM copy-remux profiles, separate from the base discovery reply.
// GPL-3.0-or-later.
#pragma once
#include <initializer_list>
#include <string_view>

namespace foxicmp { namespace progressive {
struct ContinuousWebmProfile {
    const char* mode;
    const char* codec;
    bool opus;
};

inline constexpr ContinuousWebmProfile WebmVp8Video{"continuous-webm-vp8-video-v1","vp8",false};
inline constexpr ContinuousWebmProfile WebmVp8Opus{"continuous-webm-vp8-opus-v1","vp8",true};
inline constexpr ContinuousWebmProfile WebmAv1Video{"continuous-webm-av1-video-v1","av1",false};
inline constexpr ContinuousWebmProfile WebmAv1Opus{"continuous-webm-av1-opus-v1","av1",true};

inline const ContinuousWebmProfile* FindContinuousWebmProfile(std::string_view mode) {
    for(const auto* profile:{&WebmVp8Video,&WebmVp8Opus,&WebmAv1Video,&WebmAv1Opus})
        if(mode==profile->mode)return profile;
    return nullptr;
}

inline const char* ContinuousWebmCapabilities() {
    return R"({"version":1,"action":"continuousWebmCapabilities","containerHint":"webm","extends":"continuousRemux","profiles":{"continuous-webm-vp8-video-v1":{"videoCodec":"vp8","audio":false},"continuous-webm-vp8-opus-v1":{"videoCodec":"vp8","audio":true},"continuous-webm-av1-video-v1":{"videoCodec":"av1","audio":false},"continuous-webm-av1-opus-v1":{"videoCodec":"av1","audio":true}},"knownLength":true,"sha256Required":true,"frameCount":"derived","videoDuration":"required-exact-millisecond-endpoint","simpleBlocks":true,"blockGroups":"explicit-duration","finiteClusters":true,"unknownSizeClusters":true,"audioCodec":"opus","opusPacketSamples":"from-toc","opusPreSkipSamples":[120,312],"discardPaddingPreserved":true,"maxInputBytes":134217728,"maxOutputBytes":67108864,"maxPackets":50000,"maxInitializationBytes":1048576,"maxClusterBytes":16777216})";
}
}}
