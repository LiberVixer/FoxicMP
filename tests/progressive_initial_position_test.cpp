// Initial-position parser/admission boundaries; no decoder or GUI involved.
#include "../shared/progressivestreamplayback.h"
#include <iostream>
namespace p=foxicmp::progressive;
static unsigned checks=0;
static void check(bool value){++checks;if(!value)throw std::runtime_error("initial-position oracle failed");}
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
 std::cout<<"{\"result\":\"PASS\",\"checks\":"<<checks<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
