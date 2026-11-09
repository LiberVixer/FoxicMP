// Initial-position parser/admission boundaries; no decoder or GUI involved.
#include "../shared/progressivestreamplayback.h"
#include <iostream>
namespace p=foxicmp::progressive;
static unsigned checks=0;
static void check(bool value){++checks;if(!value)throw std::runtime_error("initial-position oracle failed at check "+std::to_string(checks));}
template<class F> static void rejects(F fn,const char* code){try{fn();}catch(const p::Error& e){check(e.code==code);return;}throw std::runtime_error("request was accepted");}
int main()try{
 const std::string prefix=R"({"version":1,"action":"continuousRemuxPlay","jobId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","requestId":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","sessionId":"cccccccccccccccccccccccccccccccc","generation":"1","writerInstanceId":"dddddddddddddddddddddddddddddddd")";
 check(p::ParseContinuousPlayback(prefix+"}").startSeconds==0);
 check(p::ParseContinuousPlayback(prefix+",\"startSeconds\":1.25}").startSeconds==1.25);
 check(p::ParseContinuousPlayback(prefix+",\"startSeconds\":1}").fingerprint==p::ParseContinuousPlayback(prefix+",\"startSeconds\":1.0}").fingerprint);
 for(const auto* value:{"true","\"1\"","null","-1","3600","1e309"})rejects([&]{p::ParseContinuousPlayback(prefix+",\"startSeconds\":"+value+"}");},"invalid_request");
 p::StreamCacheStatus state;p::ContinuousPlayback play;play.duration=240000000;play.output=[&]{return state;};play.checkInitialPosition();check(true);
 play.command.startSeconds=1;rejects([&]{play.checkInitialPosition();},"waiting-range");
 state.initialized=true;state.fragments=1;state.indexedEnd100ns=10000000;rejects([&]{play.checkInitialPosition();},"waiting-range");
 state.indexedEnd100ns=10000001;play.checkInitialPosition();check(true);
 play.command.startSeconds=1.00000001;rejects([&]{play.checkInitialPosition();},"waiting-range");
 state.indexedEnd100ns=20000000;play.checkInitialPosition();check(true);
 play.command.startSeconds=24;rejects([&]{play.checkInitialPosition();},"invalid_request");
 play.command.startSeconds=23;state.complete=true;state.indexedEnd100ns=0;play.checkInitialPosition();check(true);
 state.failure="cache-integrity-mismatch";rejects([&]{play.checkInitialPosition();},"cache-integrity-mismatch");
 state.failure.clear();state.retiring=true;rejects([&]{play.checkInitialPosition();},"continuous-playback-closed");
 state.retiring=false;state.removed=true;rejects([&]{play.checkInitialPosition();},"continuous-playback-closed");
 state.removed=false;state.complete=false;state.initialized=true;state.fragments=1;state.indexedEnd100ns=30000000;
 rejects([&]{play.checkSeek(1);},"continuous-seek-unavailable");play.checkSeek(1,true);check(true);
 rejects([&]{play.checkSeek(3,true);},"waiting-range");
 rejects([&]{play.checkSeek(-1,true);},"invalid_request");
 play.growingSeeks=16;rejects([&]{play.checkSeek(1,true);},"continuous-seek-limit");
 state.complete=true;play.checkSeek(1);check(true);
 const std::string control=R"({"version":1,"action":"continuousPlaybackControl","playbackId":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","command":)";
 check(p::ParseContinuousPlayback(control+"\"seek\",\"seconds\":1,\"allowGrowing\":true}").allowGrowing);
 rejects([&]{p::ParseContinuousPlayback(control+"\"pause\",\"allowGrowing\":true}");},"invalid_request");
 play.growingSeeks=0;state.complete=false;
 play.queueSeek(5);check(play.pendingSeekSeconds==5 && play.seekRevision==1 && play.growingSeeks==0 && play.seekState=="waiting-range");
 play.queueSeek(7);check(play.pendingSeekSeconds==7 && play.seekRevision==2);
 rejects([&]{play.checkSeek(play.pendingSeekSeconds,true);},"waiting-range");
 state.indexedEnd100ns=80000000;play.checkSeek(play.pendingSeekSeconds,true);check(true);
 play.clearPendingSeek("applied");check(play.pendingSeekSeconds<0 && play.seekState=="applied" && play.seekRevision==2);
 play.queueSeek(9);play.clearPendingSeek();check(play.pendingSeekSeconds<0 && play.seekState=="cancelled");
 play.queueSeek(9);rejects([&]{play.queueSeek(3600);},"invalid_request");check(play.pendingSeekSeconds==9 && play.seekRevision==4);
 play.growingSeeks=16;rejects([&]{play.queueSeek(2);},"continuous-seek-limit");check(play.pendingSeekSeconds==9 && play.seekRevision==4);
 check(p::ParseContinuousPlayback(control+"\"seek\",\"seconds\":9,\"allowGrowing\":true,\"waitForRange\":true}").waitForRange);
 rejects([&]{p::ParseContinuousPlayback(control+"\"seek\",\"seconds\":9,\"waitForRange\":true}");},"invalid_request");
 rejects([&]{p::ParseContinuousPlayback(control+"\"seek\",\"seconds\":9,\"allowGrowing\":true,\"waitForRange\":1}");},"invalid_request");
 check(p::ParseContinuousPlayback(control+"\"cancelSeek\"}").command=="cancelSeek");
 play.committedSeek(true);check(play.growingSeeks==16 && play.pendingSeekSeconds<0);
 play.growingSeeks=0;play.committedSeek(false);check(play.growingSeeks==0);
 std::cout<<"{\"result\":\"PASS\",\"checks\":"<<checks<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
