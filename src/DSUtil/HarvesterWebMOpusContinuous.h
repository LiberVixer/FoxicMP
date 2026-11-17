// Bounded structural WebM sealer for the explicit VP9/Opus streaming profile.
// Unknown-size Clusters close only at a parsed Segment child or confirmed EOF.
// Original packet bytes, clocks, CodecDelay and DiscardPadding are preserved.
// GPL-3.0-or-later.
#pragma once
#include "HarvesterWebMMuxed.h"
#include "HarvesterWebMContinuousClock.h"
#include "HarvesterOggOpus.h"
#include "HarvesterVP9.h"
#include "HarvesterOpusSampleClock.h"
#include <deque>
#include <memory>
namespace harvester {
class ContinuousWebmOpus {
public:
    using Bytes=std::vector<uint8_t>;
    using Read=std::function<size_t(uint8_t*,size_t)>;
private:
    WebmContinuousClock videoClock;
    OpusSampleClock derivedAudio;
    const bool simpleVfr;
    Read input;uint64_t length,videoExpected,audioSamples,audioExpected,at=0,videoCount=0,audioCount=0;
    uint64_t videoEndMs=0;
    unsigned clusters=0,elements=0;bool ended=false,configured=false;WebMInfo video,audio;Vp9Configuration config;
    struct Element{webm::Element value;Bytes header;};
    Element look;bool hasLook=false;
    Bytes pending;size_t delivered=0;
    std::array<uint8_t,65536> inputBuffer;size_t inputAt=0,inputSize=0;uint64_t fetched=0;
    static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
    // Read may return a short confirmed prefix. Prefetch never waits for a full
    // buffer and avoids thousands of tiny parent IPC/integrity roundtrips.
    void exact(uint8_t* p,size_t n){check(at<=length && n<=length-at,"truncated-stream-webm-element");size_t done=0;while(done<n){if(inputAt==inputSize){const auto wanted=size_t((std::min)(length-fetched,uint64_t(inputBuffer.size())));check(wanted>0,"truncated-stream-webm-element");inputSize=input(inputBuffer.data(),wanted);inputAt=0;check(inputSize && inputSize<=wanted,"truncated-stream-webm-element");fetched+=inputSize;}const auto got=(std::min)(n-done,inputSize-inputAt);std::memcpy(p+done,inputBuffer.data()+inputAt,got);inputAt+=got;done+=got;at+=got;}}
    Element header(){Element e;e.value.start=at;e.header.resize(1);exact(e.header.data(),1);const auto w=webm::Width(e.header[0],4);e.header.resize(w+1);exact(e.header.data()+1,w);const auto s=webm::Width(e.header[w],8);const auto old=e.header.size();e.header.resize(w+s);if(e.header.size()>old)exact(e.header.data()+old,e.header.size()-old);auto start=e.value.start;check(webm::Header(e.header.data(),e.header.size(),e.value),"unsupported-stream-webm-header");e.value.start=start;e.value.data+=start;if(!e.value.unknown){check(e.value.size<=length-at,"truncated-stream-webm-element");e.value.end+=start;}return e;}
    Bytes body(const Element& e,uint64_t max){check(!e.value.unknown && e.value.size<=max,"stream-webm-element-limit");Bytes b(size_t(e.value.size));exact(b.data(),b.size());return b;}
    static void join(Bytes& a,const Bytes& b){a.insert(a.end(),b.begin(),b.end());}
    static bool top(uint32_t id){return id==0x1f43b675 || id==0x1c53bb6b || id==0x1254c367;}
    static void seal(Element& e,uint64_t bytes){const auto w=webm::Width(e.header[0],4),s=unsigned(e.header.size()-w);check(bytes<(uint64_t(1)<<(7*s))-1,"stream-webm-cluster-limit");for(unsigned i=0;i<s;++i)e.header[w+s-1-i]=uint8_t(bytes>>(8*i));e.header[w]|=uint8_t(0x80u>>(s-1));}
    void initialize(){
        auto e=header();check(e.value.id==0x1a45dfa3,"unsupported-stream-webm-doctype");auto b=body(e,4096);check(webm::Data{b.data(),b.size()}.One(0x4282).Text()=="webm","unsupported-stream-webm-doctype");join(pending,e.header);join(pending,b);
        e=header();check(e.value.id==0x18538067 && e.value.unknown,"unsupported-stream-webm-segment");join(pending,e.header);
        for(unsigned n=0;n<32;++n){e=header();check(at<=1048576,"stream-webm-head-limit");if(e.value.id==0x1f43b675){look=e;hasLook=true;break;}check(e.value.id==0x1549a966 || e.value.id==0x1654ae6b || e.value.id==0x114d9b74 || e.value.id==0x1254c367 || e.value.id==0xec || e.value.id==0xbf,"unsupported-stream-webm-metadata");b=body(e,1048576-at);if(!simpleVfr || (e.value.id!=0x114d9b74 && e.value.id!=0xbf)){join(pending,e.header);join(pending,b);}}
        check(hasLook,"stream-webm-head-limit");muxwebm::Layout head(videoClock.variable);
        head.Update([&](uint64_t pos,void* out,size_t n){if(!RangeReady(pos,n,pending.size()))return false;std::memcpy(out,pending.data()+pos,n);return true;},pending.size(),false,[&](unsigned k,const WebMInfo& info){if(k)audio=info;else video=info;});
        check(head.Initialized() && video.codec=="vp9" && video.scale==1000000 && audio.scale==1000000 && videoClock.DefaultDuration(video.frameDuration) && (!simpleVfr || (videoClock.nominalDefault?video.frameDuration>0:video.frameDuration==0)) && video.delay==0 && audio.codec=="opus" && audio.channels>=1 && audio.channels<=2 && (videoClock.packetAudio?(audio.delay==2500000 || audio.delay==6500000):audio.delay==6500000),"unsupported-stream-webm-configuration");
        // Inspect immutable codec-private data, rather than trusting a codec hint.
        for(unsigned k=0;k<2;++k){const auto& bytes=*head.tracks[k].spans.front().memory;webm::Element eb,seg;check(webm::Header(bytes.data(),bytes.size(),eb) && webm::Header(bytes.data()+eb.end,bytes.size()-eb.end,seg),"unsupported-stream-webm-header");const auto data=webm::Data{bytes.data()+eb.end+seg.data,bytes.size()-size_t(eb.end+seg.data)};auto entry=data.One(0x1654ae6b).One(0xae);
            if(k){auto priv=entry.One(0x63a2);check(priv.size==19 && !std::memcmp(priv.bytes,"OpusHead",8) && priv.bytes[8]==1 && priv.bytes[9]==audio.channels && (videoClock.packetAudio?((priv.bytes[10]==120 && priv.bytes[11]==0) || (priv.bytes[10]==56 && priv.bytes[11]==1)):priv.bytes[10]==56 && priv.bytes[11]==1) && priv.bytes[16]==0 && priv.bytes[17]==0 && priv.bytes[18]==0 && webm::OptionalNumber(entry,0x56bb,0)==80000000,"unsupported-stream-opus-configuration");if(videoClock.derivedAudio)derivedAudio.Configure(unsigned(audio.delay*48000/1000000000));}
            else{if(simpleVfr && !videoClock.nominalDefault)for(const auto& f:entry.Children())check(f.id!=0x23e383,"unsupported-stream-webm-default-duration");auto v=entry.One(0xe0);for(auto id:{0x54aau,0x54bbu,0x54ccu,0x54ddu,0x53c0u,0x53b8u})check(webm::OptionalNumber(v,id,0)==0,"unsupported-stream-webm-geometry");check(webm::OptionalNumber(v,0x54b2,0)==0 && webm::OptionalNumber(v,0x54b0,video.width)==video.width && webm::OptionalNumber(v,0x54ba,video.height)==video.height,"unsupported-stream-webm-geometry");}
        }
    }
    void block(webm::Data b,uint64_t clock,bool group,int64_t padding,uint64_t duration=0,bool reference=false){
        check(b.size>=5,"unsupported-stream-webm-block");auto w=webm::Width(b.bytes[0],8);check(b.size>w+3,"unsupported-stream-webm-block");uint64_t id=b.bytes[0]&((0x80u>>(w-1))-1);for(unsigned i=1;i<w;++i)id=id*256+b.bytes[i];check(id==video.track || id==audio.track,"unsupported-stream-webm-track");auto flags=b.bytes[w+2];check(!(flags&0x7f),"unsupported-stream-webm-block");auto ticks=AddTime(int64_t(clock),int16_t(uint16_t(b.bytes[w])<<8|b.bytes[w+1]));check(ticks>=0 && uint64_t(ticks)<=((videoClock.explicitRate || videoClock.variable)?3600000u:2000000u),"unsupported-stream-webm-clock");const auto* p=b.bytes+w+3;auto n=b.size-w-3;
        if(id==video.track){
            check(!videoClock.derivedAudio || videoCount+audioCount<50000,"stream-webm-packet-limit");
            bool key=flags&0x80;
            if(videoClock.variable){
                check(group && flags==0 && !padding && duration>0 && duration<=1000 && videoCount<videoExpected,"unsupported-stream-webm-video-duration");
                check(uint64_t(ticks)==videoEndMs && uint64_t(ticks)+duration<=videoClock.expectedEnd100ns/10000,"unsupported-stream-webm-clock");
                videoEndMs=uint64_t(ticks)+duration;key=!reference;
            }else check(!group && !padding && videoCount<videoExpected && videoClock.Packet(uint64_t(ticks),videoCount),"unsupported-stream-webm-clock");
            check(key==((p[0]&4)==0),"unsupported-stream-webm-vp9-key");if(!configured){check(n>=5 && key,"unsupported-stream-vp9-frame");config.profile=((p[0]>>5)&1)|(((p[0]>>4)&1)<<1);check(config.profile==0 || config.profile==2,"unsupported-stream-vp9-configuration");config.depth=config.profile==2?10:8;config.chroma=1;config.range=(p[4]>>(config.profile==2?3:4))&1;check((video.vp9Profile<0 || video.vp9Profile==int(config.profile)) && (video.vp9Depth<0 || video.vp9Depth==int(config.depth)),"unsupported-stream-vp9-configuration");configured=true;}VP9ContinuousSuperframePacket(p,n,config,video.width,video.height,videoCount==0,check);++videoCount;}
        else if(videoClock.derivedAudio){check(!reference && videoCount+audioCount<50000,"stream-webm-packet-limit");derivedAudio.Add(ticks,p,n,padding);++audioCount;}
        else{check(!reference,"unsupported-stream-webm-block");check(audioCount<audioExpected && oggopus::Duration(Bytes(p,p+n))==960,"unsupported-stream-opus-packet");const auto sampleClock=int64_t(audioCount*960);check(std::abs(ticks*48-sampleClock)<=48 && (audioCount || ticks==0),"unsupported-stream-opus-clock");const auto trim=audioExpected*960-312-audioSamples;const auto ns=trim*1000000000ULL/48000;check(padding>=0 && (audioCount+1==audioExpected?(uint64_t(padding)==ns || uint64_t(padding)==ns+(trim*1000000000ULL%48000!=0)):padding==0),"unsupported-stream-opus-end-trim");++audioCount;}
    }
    void cluster(Element e){
        check(++clusters<=8192,"stream-webm-cluster-limit");Bytes b;uint64_t clock=0;bool hasClock=false;unsigned count=0,blocks=0;
        while(at<length && (e.value.unknown || at<e.value.end)){
            auto c=header();if(e.value.unknown && top(c.value.id)){look=c;hasLook=true;break;}
            check(++count<=65536 && !c.value.unknown && c.value.end-c.value.start<=16777216 && c.value.end-e.value.start<=16777216 && (e.value.unknown || c.value.end<=e.value.end),"stream-webm-cluster-limit");auto bytes=body(c,16777216);webm::Data d{bytes.data(),bytes.size()};
            if(c.value.id==0xe7){check(!hasClock,"unsupported-stream-webm-clock");clock=d.Number();check(clock<=INT64_MAX,"unsupported-stream-webm-clock");hasClock=true;}
            else if(c.value.id==0xa3 || c.value.id==0xa0){
                check(hasClock,"unsupported-stream-webm-clock");int64_t pad=0;uint64_t duration=0;unsigned references=0;bool unknownReference=false;
                if(c.value.id==0xa0){
                    bool found=false,padded=false,hasDuration=false;
                    for(const auto& field:d.Children()){
                        check(field.id==0xa1 || field.id==0x75a2 || field.id==0x9b || field.id==0xbf || (videoClock.variable && field.id==0xfb),"unsupported-stream-webm-block");
                        if(field.id==0xa1){check(!found,"unsupported-stream-webm-block");found=true;}
                        if(field.id==0x75a2){check(!padded,"unsupported-stream-webm-block");padded=true;pad=field.body.Signed();}
                        if(field.id==0x9b){check(!videoClock.variable || !hasDuration,"unsupported-stream-webm-video-duration");hasDuration=true;duration=field.body.Number();}
                        if(field.id==0xfb){const auto ref=field.body.Signed();check(++references<=2 && ref<=0 && !unknownReference && (ref!=0 || references==1),"unsupported-stream-webm-video-reference");unknownReference=ref==0;}
                    }
                    check(found,"unsupported-stream-webm-block");block(d.One(0xa1),clock,true,pad,duration,references!=0);
                }else block(d,clock,false,0);
                ++blocks;
            }
            else check(c.value.id==0xec || c.value.id==0xbf || c.value.id==0xa7 || c.value.id==0xab,"unsupported-stream-webm-block");join(b,c.header);join(b,bytes);
        }
        check(hasClock && blocks && (e.value.unknown || at==e.value.end),"truncated-stream-webm-cluster");
        if(at==length && !hasLook){uint8_t byte;check(input(&byte,1)==0,"unsupported-stream-webm-tail");check(videoFinished() && (videoClock.derivedAudio?audioCount==derivedAudio.Packets():audioCount==audioExpected) && (!videoClock.variable || videoEndMs*10000==videoClock.expectedEnd100ns),"incomplete-stream-webm-opus-samples");ended=true;}
        seal(e,b.size());join(pending,e.header);join(pending,b);
    }
    // Separate source profile: no DefaultDuration or video BlockGroups. Only
    // one video payload awaits next-PTS; already closed Clusters can drain while
    // the next Cluster is still incomplete. No final-output-size oracle is used.
    struct SimpleCluster {
        Bytes bytes;uint64_t start=0,end=0,clock=0;bool unknown=false,closed=false,hasClock=false;
        unsigned children=0,blocks=0;
    };
    std::deque<std::shared_ptr<SimpleCluster>> simpleQueue;
    std::shared_ptr<SimpleCluster> simpleCurrent,unresolvedOwner,audioOwner;
    Bytes unresolved;size_t unresolvedAt=0;uint64_t unresolvedClock=0,outputTotal=0;
    static void element(Bytes& dst,uint32_t id,const Bytes& data){
        unsigned w=id>0xffffff?4:id>0xffff?3:id>0xff?2:1;
        for(unsigned n=w;n;--n)dst.push_back(uint8_t(id>>(8*(n-1))));
        unsigned size=1;while(size<8 && data.size()>=(uint64_t(1)<<(7*size))-1)++size;
        for(unsigned n=size;n;--n)dst.push_back(uint8_t((uint64_t(data.size())>>(8*(n-1))) | (n==size?(0x80u>>(size-1)):0)));
        join(dst,data);
    }
    static void number(Bytes& dst,uint32_t id,uint64_t value){Bytes b;do{b.insert(b.begin(),uint8_t(value));value>>=8;}while(value);element(dst,id,b);}
    bool videoFinished()const{return videoClock.derivedCount?(videoCount>0 && videoCount<=videoExpected):videoCount==videoExpected;}
    void simpleBudget(){
        uint64_t bytes=unresolved.size();check(simpleQueue.size()<=32,"stream-webm-pending-limit");
        for(const auto& c:simpleQueue){check(c->bytes.size()<=16777216,"stream-webm-cluster-limit");bytes+=c->bytes.size();}
        check(bytes<=33554432,"stream-webm-pending-limit");
    }
    void simpleFlush(){
        while(!simpleQueue.empty() && simpleQueue.front()->closed && simpleQueue.front()!=unresolvedOwner && simpleQueue.front()!=audioOwner){
            auto c=simpleQueue.front();Bytes output;element(output,0x1f43b675,c->bytes);
            check(output.size()<=67108864-outputTotal,"backend-output-limit");outputTotal+=output.size();join(pending,output);simpleQueue.pop_front();
        }
    }
    void simpleResolve(uint64_t end){
        check(unresolvedOwner && end>unresolvedClock && end-unresolvedClock<=1000 && end<=videoClock.expectedEnd100ns/10000,"unsupported-stream-webm-clock");
        Bytes value=unresolved;const auto w=webm::Width(value[0],8);const bool key=(value[w+2]&0x80)!=0;value[w+2]=0;
        block({value.data(),value.size()},unresolvedOwner->clock,true,0,end-unresolvedClock,!key);
        Bytes group; element(group,0xa1,value);number(group,0x9b,end-unresolvedClock);if(!key)element(group,0xfb,Bytes{0});
        Bytes encoded;element(encoded,0xa0,group);auto& bytes=unresolvedOwner->bytes;bytes.insert(bytes.begin()+unresolvedAt,encoded.begin(),encoded.end());
        unresolved.clear();unresolvedOwner.reset();simpleBudget();simpleFlush();
    }
    void simpleClose(){
        check(simpleCurrent && simpleCurrent->hasClock && simpleCurrent->blocks,"truncated-stream-webm-cluster");
        simpleCurrent->closed=true;simpleCurrent.reset();simpleFlush();
    }
    void simpleBlock(uint32_t tag,const Bytes& bytes,const Bytes& raw){
        check(simpleCurrent->hasClock && bytes.size()>=5,"unsupported-stream-webm-block");const auto w=webm::Width(bytes[0],8);
        check(bytes.size()>w+3,"unsupported-stream-webm-block");uint64_t id=bytes[0]&((0x80u>>(w-1))-1);for(unsigned n=1;n<w;++n)id=id*256+bytes[n];
        if(id==video.track){
            check(tag==0xa3 && !(bytes[w+2]&0x7f) && videoCount+bool(unresolvedOwner)<videoExpected,"unsupported-stream-webm-video-duration");
            const auto ticks=AddTime(int64_t(simpleCurrent->clock),int16_t(uint16_t(bytes[w])<<8|bytes[w+1]));
            check(ticks>=0 && uint64_t(ticks)<videoClock.expectedEnd100ns/10000,"unsupported-stream-webm-clock");
            if(!videoCount && !unresolvedOwner)check(ticks==0 && (bytes[w+2]&0x80),"unsupported-stream-webm-vp9-key");
            if(unresolvedOwner)simpleResolve(uint64_t(ticks));
            unresolved=bytes;unresolvedOwner=simpleCurrent;unresolvedAt=simpleCurrent->bytes.size();unresolvedClock=uint64_t(ticks);
        }else{
            check(id==audio.track,"unsupported-stream-webm-track");int64_t pad=0;uint64_t duration=0;
            if(tag==0xa0){
                webm::Data d{raw.data(),raw.size()};bool padded=false,hasDuration=false;
                for(const auto& f:d.Children()){
                    check(f.id==0xa1 || f.id==0x75a2 || f.id==0x9b || f.id==0xbf,"unsupported-stream-webm-block");
                    if(f.id==0x75a2){check(!padded,"unsupported-stream-webm-block");padded=true;pad=f.body.Signed();}
                    if(f.id==0x9b){check(!hasDuration,"unsupported-stream-webm-block");hasDuration=true;duration=f.body.Number();}
                }
            }
            block({bytes.data(),bytes.size()},simpleCurrent->clock,tag==0xa0,pad,duration);
            if(videoClock.derivedAudio)audioOwner=derivedAudio.Terminal()?nullptr:simpleCurrent;
        }
        ++simpleCurrent->blocks;simpleBudget();
    }
    bool nextSimple(){
        if(ended)return false;
        while(pending.empty()){
            if(simpleCurrent && !simpleCurrent->unknown && at==simpleCurrent->end){simpleClose();if(!pending.empty())break;}
            if(at==length && !hasLook){
                uint8_t byte;check(input(&byte,1)==0,"unsupported-stream-webm-tail");
                if(simpleCurrent){check(simpleCurrent->unknown,"truncated-stream-webm-cluster");simpleClose();}
                check(unresolvedOwner && (videoClock.derivedCount?videoCount+1<=videoExpected:videoCount+1==videoExpected),"incomplete-stream-webm-opus-samples");
                if(videoClock.derivedAudio){derivedAudio.Finish();audioOwner.reset();}
                simpleResolve(videoClock.expectedEnd100ns/10000);simpleFlush();
                check(simpleQueue.empty() && videoFinished() && (videoClock.derivedAudio?audioCount==derivedAudio.Packets():audioCount==audioExpected) && videoEndMs*10000==videoClock.expectedEnd100ns,"incomplete-stream-webm-opus-samples");ended=true;break;
            }
            auto e=hasLook?look:header();hasLook=false;
            if(simpleCurrent && simpleCurrent->unknown && top(e.value.id)){look=e;hasLook=true;simpleClose();continue;}
            if(!simpleCurrent){
                check(++elements<=32768,"stream-webm-element-limit");
                if(e.value.id==0x1f43b675){
                    check(++clusters<=8192 && (e.value.unknown || e.value.size<=16777216),"stream-webm-cluster-limit");
                    simpleCurrent=std::make_shared<SimpleCluster>();simpleCurrent->start=e.value.start;simpleCurrent->end=e.value.end;simpleCurrent->unknown=e.value.unknown;simpleQueue.push_back(simpleCurrent);simpleBudget();continue;
                }
                // Offset-bearing metadata is stale after structural conversion.
                check(e.value.id==0x114d9b74 || e.value.id==0x1c53bb6b || e.value.id==0xbf || e.value.id==0xec,"unsupported-stream-webm-metadata");
                body(e,1048576);continue;
            }
            auto c=simpleCurrent;
            check(++c->children<=65536 && !e.value.unknown && e.value.end-c->start<=16777216 && (c->unknown || e.value.end<=c->end),"stream-webm-cluster-limit");
            auto bytes=body(e,16777216);webm::Data data{bytes.data(),bytes.size()};bool keep=true;
            if(e.value.id==0xe7){check(!c->hasClock,"unsupported-stream-webm-clock");c->clock=data.Number();check(c->clock<=3600000,"unsupported-stream-webm-clock");c->hasClock=true;}
            else if(e.value.id==0xa3 || e.value.id==0xa0){
                if(e.value.id==0xa0){
                    unsigned count=0;Bytes blockBytes;for(const auto& f:data.Children())if(f.id==0xa1){++count;blockBytes.assign(f.body.bytes,f.body.bytes+f.body.size);}
                    check(count==1,"unsupported-stream-webm-block");simpleBlock(e.value.id,blockBytes,bytes);
                }else{
                    simpleBlock(e.value.id,bytes,bytes);const auto w=webm::Width(bytes[0],8);uint64_t id=bytes[0]&((0x80u>>(w-1))-1);for(unsigned n=1;n<w;++n)id=id*256+bytes[n];keep=id!=video.track;
                }
            }else if(e.value.id==0xbf || e.value.id==0xa7 || e.value.id==0xab)keep=false;
            else check(e.value.id==0xec,"unsupported-stream-webm-block");
            if(keep){join(c->bytes,e.header);join(c->bytes,bytes);}simpleBudget();simpleFlush();
        }
        return !pending.empty();
    }
    bool next(){if(simpleVfr)return nextSimple();if(ended)return false;if(at==length && !hasLook){uint8_t byte;check(input(&byte,1)==0,"unsupported-stream-webm-tail");check(videoFinished() && (videoClock.derivedAudio?audioCount==derivedAudio.Packets():audioCount==audioExpected) && (!videoClock.variable || videoEndMs*10000==videoClock.expectedEnd100ns),"incomplete-stream-webm-opus-samples");ended=true;return false;}auto e=hasLook?look:header();hasLook=false;check(++elements<=32768,"stream-webm-element-limit");if(e.value.id==0x1f43b675){cluster(e);return true;}check(e.value.id==0x1c53bb6b || e.value.id==0x1254c367 || e.value.id==0xec || e.value.id==0xbf,"unsupported-stream-webm-metadata");auto b=body(e,1048576);join(pending,e.header);join(pending,b);return true;}
public:
    ContinuousWebmOpus(Read read,uint64_t bytes,uint64_t frames,uint64_t samples,bool ntsc=false,WebmContinuousClock clock={},bool simple=false):videoClock((clock.explicitRate || clock.variable)?clock:WebmContinuousClock(ntsc)),derivedAudio(clock.packetAudio),simpleVfr(simple),input(std::move(read)),length(bytes),videoExpected(clock.derivedAudio?50000:clock.derivedCount && samples<=48000000?50000-(samples+1271)/960:frames),audioSamples(samples),audioExpected((samples+312+959)/960){check((!simpleVfr || videoClock.variable) && (!videoClock.derivedCount || simpleVfr) && videoClock.Valid() && (!videoClock.variable || frames==videoClock.expectedFrames) && bool(input) && bytes && bytes<=134217728 && (videoClock.derivedCount?frames==0 && videoExpected>0:frames>0) && frames<=50000 && (clock.derivedAudio?samples==0:samples>648 && samples<=48000000 && frames+audioExpected<=50000),"unsupported-stream-webm-opus-source");initialize();}
    size_t read(uint8_t* out,size_t n){check(out && n,"invalid-backend-read");while(delivered==pending.size()){pending.clear();delivered=0;if(!next())return 0;}auto count=(std::min)(n,pending.size()-delivered);std::memcpy(out,pending.data()+delivered,count);delivered+=count;return count;}
    uint64_t videoPackets()const{return videoCount;}uint64_t audioPackets()const{return audioCount;}
    uint64_t derivedAudioSamples()const{return derivedAudio.Samples();}
    unsigned audioPreSkip()const{return derivedAudio.PreSkip();}
    void finish()const{check(ended && videoFinished() && (videoClock.derivedAudio?audioCount==derivedAudio.Packets():audioCount==audioExpected) && (!videoClock.variable || videoEndMs*10000==videoClock.expectedEnd100ns),"incomplete-stream-webm-opus-samples");}
};
}
