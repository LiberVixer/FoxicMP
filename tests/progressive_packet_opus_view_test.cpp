// Decoder projection clock/padding regression, no decoder or filesystem.
#include "../src/DSUtil/HarvesterPacketOpusView.h"
#include "../src/DSUtil/HarvesterOggOpus.h"
#include <iostream>
static void check(bool v){if(!v)throw std::runtime_error("Packet Opus view assertion");}
int main()try{
 using Bytes=std::vector<uint8_t>;
 Bytes head={'O','p','u','s','H','e','a','d',1,1,120,0,0x80,0xbb,0,0,0,0,0};
 harvester::oggopus::Info config;config.channels=1;config.skip=120;
 const auto init=harvester::oggopus::Init(config,head);
 for(uint64_t trim:{17u,185u,665u,959u}){
  harvester::PacketOpusView projected;projected.Initialize(init,1);
  const int64_t ns=int64_t(trim*1000000000/48000);
  projected.Add(0,0,1,0);projected.Add(120,1,1,ns);
  Bytes source{0x80,0x98},out;
  for(const auto& span:projected.view.spans){
   check(span.offset==out.size());
   if(span.memory)out.insert(out.end(),span.memory->begin(),span.memory->end());
   else out.insert(out.end(),source.begin()+size_t(span.physical),source.begin()+size_t(span.physical+span.length));
  }
  auto read=[&](uint64_t at,void* bytes,size_t n){if(!harvester::RangeReady(at,n,out.size()))return false;std::memcpy(bytes,out.data()+at,n);return true;};
  auto info=harvester::WebMInitialization(read,out.size(),1);
  check(info.scale==1000 && info.delay==2500000 && info.channels==1);
  harvester::webm::Element eb,seg;check(harvester::webm::Header(out.data(),out.size(),eb));
  check(harvester::webm::Header(out.data()+eb.end,out.size()-eb.end,seg));
  const harvester::webm::Data body{out.data()+eb.end+seg.data,out.size()-size_t(eb.end+seg.data)};
  unsigned packets=0;
  for(const auto& c:body.Children())if(c.id==0x1f43b675){
   check(c.body.One(0xe7).Number()==(packets?2500u:0u));
   const auto group=c.body.One(0xa0);const auto block=group.One(0xa1);check(block.size==5 && block.bytes[4]==source[packets]);
   const auto pad=group.One(0x75a2,true);check(packets?pad.bytes && pad.Signed()==ns:!pad.bytes);++packets;
  }
  check(packets==2 && out.size()==projected.view.bytes);
 }
 harvester::PacketOpusView bounded;bounded.Initialize(init,1);
 for(unsigned i=0;i<3;++i)bounded.Add(i*960,0,16777216,0);
 const auto bytes=bounded.view.bytes;bool refused=false;
 try{bounded.Add(2880,0,16777216,0);}catch(const std::exception&){refused=true;}
 check(refused && bounded.view.bytes==bytes);
 std::cout<<"{\"result\":\"PASS\",\"signedPaddingCases\":4,\"audioScaleNanoseconds\":1000,\"payloadUnchanged\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
