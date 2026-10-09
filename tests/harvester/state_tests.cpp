#include "DSUtil/HarvesterState.h"
#include <cassert>
#include <iostream>
#include <functional>
using namespace harvester;
void rejects(const std::function<void()>& action) {
    bool rejected=false;try {action();} catch(const std::exception&) {rejected=true;}assert(rejected);
}
std::string manifest() {
    return R"({"schemaVersion":1,"sessionId":"0123456789abcdef0123456789abcdef","generation":1,"revision":1,"state":"downloading","tracks":{"video":{"path":"video.mp4.part","container":"mp4","availablePrefixBytes":100,"expectedTotalBytes":1000,"initializationReady":true,"state":"downloading"},"audio":{"path":"audio.m4a.part","container":"m4a","availablePrefixBytes":100,"expectedTotalBytes":1000,"initializationReady":true,"state":"downloading"}}})";
}
void replace(std::string& text,const std::string& a,const std::string& b) {text.replace(text.find(a),a.size(),b);}
int main() {
    auto old=Parse(manifest()),next=old;
    next.revision=2;next.tracks[0].available=200;assert(Advance(old,next));
    assert(!Advance(next,old));assert(!Advance(old,old));
    next.tracks[0].available=99;rejects([&]{Advance(old,next);});
    next=old;next.generation=2;rejects([&]{Advance(old,next);});
    next=old;next.revision=2;next.tracks[0].total=2000;rejects([&]{Advance(old,next);});
    next=old;next.revision=2;next.tracks[0].path="else.mp4";rejects([&]{Advance(old,next);});
    next=old;next.revision=2;next.tracks[0].initializationReady=false;rejects([&]{Advance(old,next);});
    auto text=manifest();replace(text,"\"schemaVersion\":1","\"schemaVersion\":1,\"schemaVersion\":1");rejects([&]{Parse(text);});
    for(auto filename:{"../outside.mp4", "https://site/movie", "C:\\movie", "video.mp4:secret", "folder/movie", "..", "trailing.", "trailing "}) {
        text=manifest();replace(text,"video.mp4.part",filename);rejects([&]{Parse(text);});
    }
    for(auto total:{"null","-1","0","true","9223372036854775808","99"}) {
        text=manifest();replace(text,"\"expectedTotalBytes\":1000",std::string("\"expectedTotalBytes\":")+total);rejects([&]{Parse(text);});
    }
    text=manifest();replace(text,"\"state\":\"downloading\"","\"state\":\"complete\"");rejects([&]{Parse(text);});
    // Growing fMP4 tracks may have no length until the writer confirms completion.
    text=manifest();replace(text,"\"schemaVersion\":1","\"schemaVersion\":1,\"duration100ns\":900000000,\"timestampOffset100ns\":10000000");
    for(int i=0;i<2;++i)replace(text,"\"expectedTotalBytes\":1000","\"fragmented\":true,\"expectedTotalBytes\":null");
    auto fragment=Parse(text);assert(fragment.tracks[0].fragmented && !fragment.tracks[0].total);
    auto finished=fragment;finished.revision++;finished.tracks[0].total=100;finished.tracks[0].state=State::Complete;
    assert(Advance(fragment,finished));assert(EndReady(finished.tracks[0]));
    auto changed=finished;changed.revision++;changed.tracks[0].total=200;rejects([&]{Advance(finished,changed);});
    changed=fragment;changed.revision++;changed.duration++;rejects([&]{Advance(fragment,changed);});
    changed=fragment;changed.revision++;changed.timestampOffset++;rejects([&]{Advance(fragment,changed);});
    changed=fragment;changed.revision++;changed.tracks[0].fragmented=false;rejects([&]{Advance(fragment,changed);});
    auto invalid=text;replace(invalid,"\"duration100ns\":900000000","\"duration100ns\":0");rejects([&]{Parse(invalid);});
    invalid=text;replace(invalid,"\"fragmented\":true","\"fragmented\":1");rejects([&]{Parse(invalid);});
    invalid=text;replace(invalid,"\"state\":\"downloading\"","\"state\":\"complete\"");rejects([&]{Parse(invalid);});
    rejects([]{Parse(std::string(65537,' '));});
    assert(Time100ns(1024,48000)==213333);assert(Time100ns(-1024,48000)==-213333);
    assert(Time100ns(AddTime(0,-1024),48000)==-213333); // AAC preroll/edit-list shift stays negative.
    assert(Time100ns(AddTime(48000,48000),48000)==20000000); // Positive empty edit is preserved.
    rejects([]{Time100ns(1,0);});rejects([]{Time100ns(INT64_MAX,1);});
    rejects([]{AddTime(INT64_MAX,1);});rejects([]{AddTime(INT64_MIN,-1);});
    auto endTrack=old.tracks[0];endTrack.available=endTrack.total;
    assert(!EndReady(endTrack)); // All bytes alone are not a successful EOF.
    endTrack.state=State::Complete;assert(EndReady(endTrack));
    endTrack.available--;assert(!EndReady(endTrack));
    endTrack.state=State::Failed;assert(!EndReady(endTrack));
    assert(!RangeReady(UINT64_MAX-1,10,UINT64_MAX));assert(RangeReady(100,0,100));assert(!RangeReady(101,0,100));
    std::vector<Sample> samples={{100,50,0,10,0,true},{150,50,20,30,10,false},{200,50,10,20,20,false}};
    assert(ReadyEnd(samples,149,30)==0); // Partial keyframe is unavailable.
    assert(ReadyEnd(samples,150,30)==10);
    assert(ReadyEnd(samples,199,30)==10); // Partial reference packet is unavailable.
    assert(ReadyEnd(samples,200,30)==10); // Missing B-frame prevents crossing its presentation time.
    assert(ReadyEnd(samples,250,30)==30);
    std::vector<Sample> audio={{0,10,5,15,5,true},{10,10,15,25,15,true}};
    assert(std::min(ReadyEnd(samples,250,30),ReadyEnd(audio,10,25))==15);
    assert(std::min(ReadyEnd(samples,200,30),ReadyEnd(audio,20,25))==10);
    std::cout << "Session validation, revision/generation, overflow, partial-packet and common-buffer tests passed\n";
}
