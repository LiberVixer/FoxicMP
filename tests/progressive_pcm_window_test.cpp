// Sample-boundary selection, independent of Windows/Qt and audio hardware.
#include "../src/DSUtil/HarvesterPcmWindow.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <random>
using harvester::pcm::Window;
using harvester::pcm::Slice;
static size_t checks=0;
static void check(bool v){++checks;if(!v)throw std::runtime_error("PCM window check "+std::to_string(checks));}
int main()try {
 for(const auto rate:{44100u,48000u}) {
  for(const uint64_t target:{uint64_t(0),uint64_t(1),uint64_t(rate)*2+1,uint64_t(rate)*4-1}) {
   const uint64_t begin=1105+target,end=1105+rate*4;
   const uint64_t first=begin>rate/4?(begin-rate/4)/1152*1152:0;
   Window w;check(w.Configure(begin,end,rate,first));check(!w.Complete());check(!w.Configure(end,begin,rate,first));
   Slice s;check(!w.Take(1,32000,s));
   std::vector<uint64_t> actual;std::mt19937 random(42);uint64_t cursor=first;
   while(cursor<end+2048) {
    const uint64_t n=1+random()%2311;check(w.Take(n,rate,s));
    if(s.count){check(s.start==int64_t(actual.size()*10000000/rate));for(uint64_t i=0;i<s.count;++i)actual.push_back(cursor+s.skip+i);}
    cursor+=n;
   }
   check(actual.size()==end-begin);for(size_t i=0;i<actual.size();++i)if(actual[i]!=begin+i)throw std::runtime_error("PCM sample order");
   check(w.Complete());check(w.Take(10000,rate,s) && s.count==0);w.Clear();check(!w.Active() && !w.Take(1,rate,s));
  }
 }
 Window w;Slice s;check(!w.Configure(0,10,32000,0));check(!w.Configure(0,0,44100,0));check(!w.Configure(5,10,44100,6));check(!w.Configure(0,UINT64_MAX,44100,0));
 check(w.Configure(5,10,44100,0));check(w.Take(UINT64_MAX,44100,s));check(!w.Take(1,44100,s));
 std::cout<<"{\"result\":\"PASS\",\"checks\":"<<checks<<",\"windows\":8}"<<std::endl;return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
