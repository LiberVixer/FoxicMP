// Private continuous AAC handshake. Not a public SDK/playback capability.
// GPL-3.0-or-later.
#pragma once
#include "progressivesession.h"
#include "progressivereaderlease.h"
#include "progressivesha256.h"
#include "DSUtil/HarvesterAacWindow.h"

namespace foxicmp { namespace progressive {
struct StreamAudioWindow {
    harvester::pcm::AacWindow pcm;
    unsigned trackId=0;
    std::string json,sha256;
    static StreamAudioWindow Parse(const Value& value) {
        auto check=[](bool ok){Require(ok,"audioWindow","Invalid AAC sample window","backend-protocol-error");};
        check(value.IsObject());harvester::UniqueKeys(value);
        Keys(value,"audioWindow",{"version","trackId","sampleRate","channels","rawOrigin","timestampOrigin","rawBegin","rawEnd","timestampBegin","timestampEnd","encodedEnd","endPadding"});
        StreamAudioWindow out;
        check(value["version"].IsUint() && value["version"].GetUint()==1);
        check(value["trackId"].IsUint() && value["trackId"].GetUint()>=1 && value["trackId"].GetUint()<=2);
        check(value["sampleRate"].IsUint() && value["channels"].IsUint());
        check(Text(value["rawOrigin"],"rawOrigin")=="untrimmed-decoder-samples" &&
              Text(value["timestampOrigin"],"timestampOrigin")=="presentation-samples");
        const auto skip=Unsigned(value["rawBegin"],"rawBegin",1024),samples=Unsigned(value["timestampEnd"],"timestampEnd",48000*3600);
        check(out.pcm.Configure(value["sampleRate"].GetUint(),value["channels"].GetUint(),samples,unsigned(skip)));
        check(Unsigned(value["timestampBegin"],"timestampBegin")==0 && Unsigned(value["rawEnd"],"rawEnd")==out.pcm.RawEnd() &&
              Unsigned(value["encodedEnd"],"encodedEnd")==out.pcm.EncodedEnd() && Unsigned(value["endPadding"],"endPadding")==out.pcm.EndPadding());
        out.trackId=value["trackId"].GetUint();out.json=lease_detail::Encode(value);
        Sha256 hash;hash.update(out.json.data(),out.json.size());out.sha256=hash.hex();return out;
    }
};
}}
