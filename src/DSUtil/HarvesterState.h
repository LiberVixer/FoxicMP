// FoxicMP progressive-session protocol. GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include "ExtLib/rapidjson/include/rapidjson/document.h"

namespace harvester {
constexpr size_t MaxManifestBytes = 65536;
enum class State { Downloading, Paused, Merging, Complete, Cancelled, Failed };
inline State ParseState(const rapidjson::Value& value, bool track) {
    if (!value.IsString()) throw std::runtime_error("Invalid state type");
    const std::string s(value.GetString(), value.GetStringLength());
    if (s == "downloading") return State::Downloading;
    if (s == "paused") return State::Paused;
    if (!track && s == "merging") return State::Merging;
    if (s == "complete") return State::Complete;
    if (s == "cancelled") return State::Cancelled;
    if (s == "failed") return State::Failed;
    throw std::runtime_error("Invalid session state");
}
inline const rapidjson::Value& Field(const rapidjson::Value& v, const char* name) {
    if (!v.IsObject() || !v.HasMember(name)) throw std::runtime_error("Missing session field");
    return v[name];
}
inline std::string String(const rapidjson::Value& v, const char* name) {
    const auto& f = Field(v, name);
    if (!f.IsString() || !f.GetStringLength() || f.GetStringLength() > 255) throw std::runtime_error("Invalid string field");
    std::string s(f.GetString(), f.GetStringLength());
    if (s.find('\0') != std::string::npos) throw std::runtime_error("Embedded null in session field");
    return s;
}
inline uint64_t Number(const rapidjson::Value& v, const char* name) {
    const auto& f = Field(v, name);
    if (!f.IsUint64() || f.GetUint64() > uint64_t(std::numeric_limits<int64_t>::max())) throw std::runtime_error("Invalid byte count or revision");
    return f.GetUint64();
}
inline void UniqueKeys(const rapidjson::Value& v, unsigned depth=0) {
    if(depth>32 || (v.IsObject() && v.MemberCount()>128) || (v.IsArray() && v.Size()>128)) throw std::runtime_error("Excessive JSON nesting or field count");
    if (v.IsObject()) {
        for (auto a=v.MemberBegin(); a!=v.MemberEnd(); ++a) {
            for (auto b=v.MemberBegin(); b!=a; ++b) {
                if (a->name == b->name) throw std::runtime_error("Duplicate JSON field");
            }
            UniqueKeys(a->value,depth+1);
        }
    } else if (v.IsArray()) { for (const auto& x : v.GetArray()) UniqueKeys(x,depth+1); }
}
struct Track {
    std::string path, container;
    uint64_t available=0, total=0;
    bool initializationReady=false, fragmented=false;
    State state=State::Downloading;
};
struct Snapshot {
    std::string sessionId;
    uint64_t generation=0, revision=0;
    int64_t duration=0, timestampOffset=0;
    State state=State::Downloading;
    std::array<Track,2> tracks;
};
inline Snapshot Parse(const std::string& text) {
    if (text.empty() || text.size() > MaxManifestBytes) throw std::runtime_error("Invalid manifest size");
    rapidjson::Document d;
    d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(text.data(), text.size());
    if (d.HasParseError() || !d.IsObject()) throw std::runtime_error("Invalid manifest JSON");
    UniqueKeys(d);
    if (Number(d,"schemaVersion") != 1) throw std::runtime_error("Unsupported session schema");
    Snapshot s;
    s.sessionId=String(d,"sessionId");
    if (s.sessionId.size()!=32 || s.sessionId.find_first_not_of("0123456789abcdef")!=std::string::npos) throw std::runtime_error("sessionId must be a random 128-bit hex identifier");
    s.generation=Number(d,"generation"); s.revision=Number(d,"revision");
    if (!s.generation || !s.revision) throw std::runtime_error("Zero generation or revision");
    s.state=ParseState(Field(d,"state"),false);
    if(d.HasMember("duration100ns"))s.duration=int64_t(Number(d,"duration100ns"));
    if(d.HasMember("timestampOffset100ns"))s.timestampOffset=int64_t(Number(d,"timestampOffset100ns"));
    const auto& tracks=Field(d,"tracks");
    if(!tracks.IsObject() || tracks.MemberCount()!=2) throw std::runtime_error("Exactly two tracks are required");
    for (int i=0;i<2;++i) {
        const auto& v=Field(tracks,i?"audio":"video"); auto& t=s.tracks[i];
        t.path=String(v,"path"); t.container=String(v,"container");
        if (t.path=="." || t.path==".." || t.path.find_first_of("/\\:<>|?*\"")!=std::string::npos || t.path.back()=='.' || t.path.back()==' ') throw std::runtime_error("Tracks must be plain local filenames");
        if (t.container!=(i?"m4a":"mp4")) throw std::runtime_error("Only MP4 video and M4A audio are supported");
        t.available=Number(v,"availablePrefixBytes");
        if(v.HasMember("fragmented")) {if(!v["fragmented"].IsBool())throw std::runtime_error("Invalid fragmented flag");t.fragmented=v["fragmented"].GetBool();}
        const auto& total=Field(v,"expectedTotalBytes");
        t.total=total.IsNull() && t.fragmented ? 0 : Number(v,"expectedTotalBytes");
        if ((t.fragmented && s.duration<=0) || (!t.total && !t.fragmented) || (t.total && t.available>t.total)) throw std::runtime_error("Unknown or invalid total length");
        const auto& ready=Field(v,"initializationReady");
        if (!ready.IsBool()) throw std::runtime_error("Invalid initializationReady");
        t.initializationReady=ready.GetBool(); t.state=ParseState(Field(v,"state"),true);
        if (t.state==State::Complete && t.available!=t.total) throw std::runtime_error("Incomplete track marked complete");
    }
    if (s.tracks[0].path==s.tracks[1].path) throw std::runtime_error("Audio and video must be distinct files");
    if ((s.state==State::Complete || s.state==State::Merging) && (s.tracks[0].state!=State::Complete || s.tracks[1].state!=State::Complete)) throw std::runtime_error("Session completed before both tracks");
    return s;
}
inline bool Advance(const Snapshot& old, const Snapshot& next) {
    if (next.sessionId!=old.sessionId) throw std::runtime_error("Session identity changed");
    if (next.generation<old.generation || (next.generation==old.generation && next.revision<=old.revision)) return false;
    if (next.generation!=old.generation) throw std::runtime_error("Session generation changed: reopen playback");
    if((old.state==State::Complete || old.state==State::Cancelled || old.state==State::Failed) && next.state!=old.state) throw std::runtime_error("Terminal session state changed");
    for (int i=0;i<2;++i) {
        const auto& a=old.tracks[i]; const auto& b=next.tracks[i];
        if (a.path!=b.path || a.container!=b.container || (a.total!=b.total && (a.total || !a.fragmented || !b.total)) || a.fragmented!=b.fragmented || old.duration!=next.duration || old.timestampOffset!=next.timestampOffset || b.available<a.available || (a.initializationReady && !b.initializationReady)) throw std::runtime_error("Track identity or confirmed prefix changed");
        if ((a.state==State::Complete || a.state==State::Cancelled || a.state==State::Failed) && b.state!=a.state) throw std::runtime_error("Terminal track state changed");
    }
    return true;
}
inline bool EndReady(const Track& track) {
    return track.state==State::Complete && track.available==track.total;
}
inline bool RangeReady(uint64_t offset,uint64_t length,uint64_t prefix) {
    return offset<=prefix && length<=prefix-offset;
}
inline int64_t AddTime(int64_t a,int64_t b) {
    if((b>0 && a>std::numeric_limits<int64_t>::max()-b) || (b<0 && a<std::numeric_limits<int64_t>::min()-b))
        throw std::runtime_error("Timestamp overflow");
    return a+b;
}
inline int64_t Time100ns(int64_t ticks,uint32_t scale) {
    if(!scale)throw std::runtime_error("Zero MP4 timescale");
    constexpr int64_t unit=10000000;
    const auto seconds=ticks/scale, remainder=ticks%scale;
    if(seconds>std::numeric_limits<int64_t>::max()/unit || seconds<std::numeric_limits<int64_t>::min()/unit)
        throw std::runtime_error("MP4 timestamp exceeds clock range");
    return AddTime(seconds*unit,remainder*unit/scale);
}
struct Sample { uint64_t offset=0,size=0; int64_t start=0,end=0,decode=0; bool sync=false; };
// Conservative presentation boundary: an undecoded reference/B frame must never be crossed.
inline int64_t ReadyEnd(const std::vector<Sample>& samples,uint64_t prefix,int64_t duration) {
    int64_t end=0;
    for (const auto& s:samples) {
        if (!RangeReady(s.offset,s.size,prefix)) return std::max<int64_t>(0,std::min(end,std::min(s.start,s.decode)));
        end=std::max(end,s.end);
    }
    return std::max<int64_t>(0,std::min(end,duration));
}
} // namespace harvester
