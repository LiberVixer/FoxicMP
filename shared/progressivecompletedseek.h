// Active-session seeks after writer.complete; codec/container admission is unchanged.
// GPL-3.0-or-later.
#pragma once
namespace foxicmp { namespace progressive {
inline const char* CompletedSeekCapabilities() {
    return R"({"version":1,"mode":"confirmed-complete-seek","scope":"active-session","requiresWriterComplete":true,"readerIdentityPreserved":true,"sourceIdentityPreserved":true,"decoderReloadAllowed":true,"reopensOriginalPath":false,"afterReleasedEof":false,"formatsUnchanged":true,"validatedVideoCodecs":["h264"],"validatedAudioCodecs":["aac-lc"],"controls":["manualPause","speed","volumePercent","muted","primaryAudioSelection"]})";
}
}}
