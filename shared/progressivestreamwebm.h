// Explicit WebM copy-remux profiles, separate from the base discovery reply.
// GPL-3.0-or-later.
#pragma once
#include <initializer_list>
#include <string_view>

namespace foxicmp { namespace progressive {
struct ContinuousWebmProfile {
    const char* mode;
    const char* codec;
    std::string_view audioCodec;
};

inline constexpr ContinuousWebmProfile WebmVp8Video{"continuous-webm-vp8-video-v1","vp8",{}};
inline constexpr ContinuousWebmProfile WebmVp8Opus{"continuous-webm-vp8-opus-v1","vp8","opus"};
inline constexpr ContinuousWebmProfile WebmAv1Video{"continuous-webm-av1-video-v1","av1",{}};
inline constexpr ContinuousWebmProfile WebmAv1Opus{"continuous-webm-av1-opus-v1","av1","opus"};
inline constexpr ContinuousWebmProfile WebmVp8Vorbis{"continuous-webm-vp8-vorbis-v1","vp8","vorbis"};
inline constexpr ContinuousWebmProfile WebmVp9Vorbis{"continuous-webm-vp9-vorbis-v1","vp9","vorbis"};
inline constexpr ContinuousWebmProfile WebmAv1Vorbis{"continuous-webm-av1-vorbis-v1","av1","vorbis"};

inline const ContinuousWebmProfile* FindContinuousWebmProfile(std::string_view mode) {
    for(const auto* profile:{&WebmVp8Video,&WebmVp8Opus,&WebmAv1Video,&WebmAv1Opus,&WebmVp8Vorbis,&WebmVp9Vorbis,&WebmAv1Vorbis})
        if(mode==profile->mode)return profile;
    return nullptr;
}

inline const char* ContinuousWebmCapabilities() {
    return R"({"version":1,"action":"continuousWebmCapabilities","containerHint":"webm","extends":"continuousRemux","profiles":{"continuous-webm-vp8-video-v1":{"videoCodec":"vp8","audio":false},"continuous-webm-vp8-opus-v1":{"videoCodec":"vp8","audio":true},"continuous-webm-av1-video-v1":{"videoCodec":"av1","audio":false},"continuous-webm-av1-opus-v1":{"videoCodec":"av1","audio":true},"continuous-webm-vp8-vorbis-v1":{"videoCodec":"vp8","audio":true,"audioCodec":"vorbis"},"continuous-webm-vp9-vorbis-v1":{"videoCodec":"vp9","audio":true,"audioCodec":"vorbis"},"continuous-webm-av1-vorbis-v1":{"videoCodec":"av1","audio":true,"audioCodec":"vorbis"}},"knownLength":true,"sha256Required":true,"frameCount":"derived","videoDuration":"required-exact-millisecond-endpoint","simpleBlocks":true,"blockGroups":"explicit-duration","finiteClusters":true,"unknownSizeClusters":true,"audioCodec":"opus","opusPacketSamples":"from-toc","opusPreSkipSamples":[120,312],"discardPaddingPreserved":true,"maxInputBytes":134217728,"maxOutputBytes":67108864,"maxPackets":50000,"maxInitializationBytes":1048576,"maxClusterBytes":16777216,"vorbis":{"sampleRate":48000,"channels":[1,2],"codecDelay":"whole-samples-up-to-half-largest-block","seekPreroll":"previous-overlap-window","maxInitialVideoOffsetMilliseconds":100}})";
}
}}
