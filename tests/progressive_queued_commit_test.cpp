// Deterministic queued-seek boundaries, shared by all three adapters.
// These tests exercise reentrant observers without a decoder or GUI.
#include "../shared/progressivestreamplayback.h"
#include <iostream>
namespace p=foxicmp::progressive;
using Result=p::ContinuousPlayback::QueuedApply;
static unsigned checks=0;
static void check(bool value){++checks;if(!value)throw std::runtime_error("queued-commit oracle failed at check "+std::to_string(checks));}
template<class F> static void rejects(F fn,const char* code){try{fn();}catch(const p::Error& e){check(e.code==code);return;}throw std::runtime_error("expected rejection");}
struct Fixture {
 p::StreamCacheStatus state;p::ContinuousPlayback play;bool owned=true;unsigned submits=0;double target=-1;
 std::function<void()> observer;
 Fixture(){state.initialized=true;state.fragments=1;state.indexedEnd100ns=80000000;play.duration=120000000;play.output=[this]{if(observer)observer();return state;};play.queueSeek(2);}
 Result apply(std::function<bool(double)> ready={},std::function<bool(double)> submit={}){
  return play.applyQueuedSeek([&]{return owned;},[&](double seconds){return ready?ready(seconds):true;},[&](double seconds){++submits;target=seconds;return submit?submit(seconds):true;});
 }
};
int main()try{
 {Fixture f;check(f.apply()==Result::Applied);check(f.submits==1 && f.target==2 && f.play.growingSeeks==1 && f.play.seekState=="applied" && f.play.pendingSeekSeconds<0);check(f.apply()==Result::None && f.submits==1);}
 {Fixture f;f.state.indexedEnd100ns=20000000;rejects([&]{f.apply();},"waiting-range");check(f.submits==0 && f.play.growingSeeks==0 && f.play.pendingSeekSeconds==2);f.state.indexedEnd100ns++;check(f.apply()==Result::Applied);}
 {Fixture f;check(f.apply([](double){return false;})==Result::Waiting);check(f.submits==0 && f.play.growingSeeks==0);check(f.apply({},[](double){return false;})==Result::Rejected);check(f.play.pendingSeekSeconds==2 && f.play.growingSeeks==0);check(f.apply()==Result::Applied && f.submits==2);}
 // Cancel, replace (including an identical target), close, ownership loss,
 // ordinary seek, and failing output at each observable pre-submit boundary.
 for(unsigned boundary=0;boundary<3;++boundary)for(unsigned action=0;action<8;++action){
  Fixture f;bool fired=false;
  auto mutate=[&]{if(fired)return;fired=true;
   switch(action){
    case 0:f.play.clearPendingSeek();break;
    case 1:f.play.queueSeek(3);break;
    case 2:f.play.clearPendingSeek();f.play.queueSeek(2);break;
    case 3:f.play.closed=true;break;
    case 4:f.owned=false;break;
    case 5:f.play.committedSeek(false);break;
    case 6:f.state.failure="writer-failed";break;
    case 7:f.state.retiring=true;break;
   }
  };
  if(boundary==0)f.observer=mutate;
  if(boundary==1)f.play.producerError=[&]{mutate();return std::string();};
  bool error=false;Result result=Result::None;
  try{result=f.apply([&](double){if(boundary==2)mutate();return true;});}
  catch(const p::Error& e){error=true;check(e.code==(action==6?"writer-failed":"continuous-playback-closed"));}
  check(fired && f.submits==0 && f.play.growingSeeks==0);
  if(action>=6)check(error);else check(!error && result==Result::Superseded);
  if(action==0)check(f.play.seekState=="cancelled" && f.play.pendingSeekSeconds<0);
  if(action==1)check(f.play.pendingSeekSeconds==3 && f.play.seekRevision==2);
  if(action==2)check(f.play.pendingSeekSeconds==2 && f.play.seekRevision==2);
  if(action==5)check(f.play.seekState=="superseded" && f.play.pendingSeekSeconds<0);
 }
 // Mutations after an accepted decoder handoff must survive completion of
 // the old operation, whose resource budget is still charged exactly once.
 for(unsigned action=0;action<5;++action){Fixture f;
  check(f.apply({},[&](double){
   f.play.committedSeek(true,true);f.play.committedSeek(true,true);
   if(action==0)f.play.clearPendingSeek();
   if(action==1)f.play.queueSeek(3);
   if(action==2){f.play.clearPendingSeek();f.play.queueSeek(2);}
   if(action==3)f.play.closed=true;
   if(action==4)f.owned=false;
   return true;
  })==Result::Applied);
  check(f.submits==1 && f.play.growingSeeks==1);
  if(action==0)check(f.play.pendingSeekSeconds<0 && f.play.seekState=="cancelled");
  if(action==1)check(f.play.pendingSeekSeconds==3 && f.play.seekRevision==2);
  if(action==2)check(f.play.pendingSeekSeconds==2 && f.play.seekRevision==2);
  if(action==1 || action==2){check(f.apply()==Result::Applied);check(f.play.growingSeeks==2 && f.play.pendingSeekSeconds<0);}
 }
 {Fixture f;check(f.apply([&](double){check(f.apply()==Result::None);return true;},[&](double){check(f.apply()==Result::None);return true;})==Result::Applied);check(f.submits==1 && f.play.growingSeeks==1);}
 {Fixture f;bool caught=false;try{f.apply([](double)->bool{throw std::runtime_error("test");});}catch(const std::runtime_error&){caught=true;}check(caught && f.submits==0);check(f.apply()==Result::Applied);}
 {Fixture f;check(f.apply([&](double){check(f.play.queuedSubmissionAllowed());f.play.clearPendingSeek();check(!f.play.queuedSubmissionAllowed());return true;})==Result::Superseded);check(!f.play.queuedSubmissionAllowed());}
 {Fixture f;f.play.growingSeeks=15;check(f.apply()==Result::Applied && f.play.growingSeeks==16);rejects([&]{f.play.queueSeek(3);},"continuous-seek-limit");f.state.complete=true;f.play.queueSeek(3);check(f.apply()==Result::Applied && f.play.growingSeeks==16);}
 {Fixture f;f.play.growingSeeks=15;check(f.apply({},[&](double){check(f.play.queuedSubmissionAllowed());rejects([&]{f.play.checkSeek(3,true);},"continuous-seek-limit");rejects([&]{f.play.queueSeek(3);},"continuous-seek-limit");return false;})==Result::Rejected);check(f.play.growingSeeks==15);f.play.queueSeek(3);check(f.apply()==Result::Applied && f.play.growingSeeks==16);}
 {Fixture f;check(f.apply([&](double){f.state.complete=true;return true;})==Result::Applied);check(f.play.growingSeeks==0);}
 std::cout<<"{\"result\":\"PASS\",\"checks\":"<<checks<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
