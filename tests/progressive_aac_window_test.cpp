// AAC packet geometry and exact PCM slicing, independent of any decoder/OS.
#include "DSUtil/HarvesterAacWindow.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using harvester::pcm::AacWindow;
using harvester::pcm::Window;
using harvester::pcm::Slice;
static size_t checks=0;
static void check(bool v){++checks;if(!v)throw std::runtime_error("AAC window check "+std::to_string(checks));}
int main()try{
 for(const auto rate:{44100u,48000u})for(const auto channels:{1u,2u})
 for(const uint64_t count:{uint64_t(1),uint64_t(1023),uint64_t(1024),uint64_t(1025),uint64_t(rate)*4-137})
 for(const auto skip:{0u,1024u}){
  AacWindow p;check(p.Configure(rate,channels,count,skip));check(!p.Configure(rate,channels,0,skip));
  check(p.Rate()==rate && p.Channels()==channels && p.Samples()==count);
  check(p.RawBegin()==skip && p.RawEnd()==skip+count && p.EndPadding()<1024);
  check(p.EncodedEnd()==p.RawEnd()+p.EndPadding());
  check(!p.Packet(99,100,0));check(!p.Complete());
  for(uint64_t i=0;i<p.PacketCount();++i){const auto pts=int64_t(i*1024)-skip;const auto duration=i+1==p.PacketCount()?uint64_t(int64_t(count)-pts):1024;
   check(p.Packet(pts,pts,duration,i?0:skip));check(p.Complete()==(i+1==p.PacketCount()));}
  check(!p.Packet(int64_t(count),int64_t(count),1024));
  for(const auto target:{uint64_t(0),count-1}){
   Window w;check(p.RawWindow(w,target));uint64_t begin=0,end=0;check(p.TimestampWindow(target,begin,end));check(begin==target && end==count);
   uint64_t cursor=0,total=0;Slice s;
   while(cursor<p.EncodedEnd()+1024){const uint64_t n=1+(cursor*17+43)%2111;check(w.Take(n,rate,s));
    if(s.count){check(cursor+s.skip==skip+target+total);check(s.start==int64_t(total*10000000/rate));total+=s.count;}
    cursor+=n;}
   check(w.Complete() && total==count-target);check(!p.RawWindow(w,count));check(!p.TimestampWindow(count,begin,end));
  }
 }
 AacWindow p;check(!p.Configure(32000,1,100,0));check(!p.Configure(48000,3,100,0));check(!p.Configure(48000,1,UINT64_MAX,0));check(!p.Configure(48000,1,100,1025));
 check(p.Configure(48000,1,100,1024));check(p.Packet(-1024,-1024,1024,1024));check(!p.Complete());check(!p.Packet(0,0,99));check(p.Packet(0,0,100,0,924));check(p.Complete());
 std::cout<<"{\"result\":\"PASS\",\"checks\":"<<checks<<",\"geometries\":40}"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
