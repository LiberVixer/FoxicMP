// Explicit bounded WebM/VP9 input for continuous copy-remux. GPL-3.0-or-later.
// Native WebMIndex keeps its independent, finite-Cluster admission unchanged.
#pragma once
#include "HarvesterWebM.h"
#include "HarvesterVP9.h"
#include "../../shared/progressivesha256.h"
#include <deque>

namespace harvester {
class ContinuousWebmVp9 {
public:
    using Read=std::function<size_t(uint8_t*,size_t)>;
    struct Packet {uint64_t milliseconds=0,offset=0;size_t bytes=0;bool key=false;std::string sha256;};
private:
    Read rawRead;
    const uint64_t length,expected;
    uint64_t at=0,clusterStart=0,clusterEnd=0,clusterTime=0,validated=0;
    unsigned topCount=0,clusterCount=0,children=0,clusterPackets=0;
    bool inCluster=false,clock=false,eof=false,configured=false;
    WebMInfo info;
    Vp9Configuration config;
    std::vector<uint8_t> pending;
    size_t delivered=0;
    std::deque<Packet> packets;
    struct Element {webm::Element value;std::vector<uint8_t> header;};
    static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
    static std::string digest(const uint8_t* p,size_t n){foxicmp::progressive::Sha256 hash;hash.update(p,n);return hash.hex();}
    void exact(uint8_t* p,size_t n){
        check(at<=length && n<=length-at,"truncated-stream-webm-element");
        size_t done=0;while(done<n){const auto got=rawRead(p+done,(std::min)(n-done,size_t(65536)));check(got && got<=n-done,"truncated-stream-webm-element");done+=got;at+=got;}
    }
    Element header(){
        Element e;e.value.start=at;e.header.resize(1);exact(e.header.data(),1);
        const auto ids=webm::Width(e.header[0],4);e.header.resize(ids+1);exact(e.header.data()+1,ids);
        const auto sizes=webm::Width(e.header[ids],8);const auto old=e.header.size();e.header.resize(ids+sizes);
        if(e.header.size()>old)exact(e.header.data()+old,e.header.size()-old);
        const auto start=e.value.start;check(webm::Header(e.header.data(),e.header.size(),e.value),"unsupported-stream-webm-header");
        e.value.start=start;e.value.data+=start;
        if(!e.value.unknown){check(e.value.size<=length-at,"truncated-stream-webm-element");e.value.end+=start;}
        return e;
    }
    std::vector<uint8_t> body(const Element& e,uint64_t limit){
        check(!e.value.unknown && e.value.size<=limit,"unsupported-stream-webm-element-size");
        std::vector<uint8_t> bytes(size_t(e.value.size));exact(bytes.data(),bytes.size());return bytes;
    }
    static bool master(uint32_t id){switch(id){
        case 0x1a45dfa3:case 0x1549a966:case 0x1654ae6b:case 0xae:case 0xe0:case 0x55b0:
        case 0x114d9b74:case 0x4dbb:case 0x1254c367:case 0x7373:case 0x63c0:case 0x67c8:
        case 0x1c53bb6b:case 0xbb:case 0xb7:return true;default:return false;}}
    static bool leaf(uint32_t id){switch(id){
        case 0xec:case 0xbf:case 0x4282:case 0x4285:case 0x4286:case 0x4287:case 0x42f2:case 0x42f3:case 0x42f7:
        case 0x2ad7b1:case 0x4489:case 0x4d80:case 0x5741:case 0xd7:case 0x73c5:case 0x83:case 0x86:case 0x88:
        case 0xb9:case 0x9c:case 0x9a:case 0xb0:case 0xba:case 0x23e383:case 0x23314f:case 0x56aa:case 0x56bb:
        case 0x63a2:case 0x22b59c:case 0x536e:case 0x53b8:case 0x53c0:case 0x53b9:case 0x53ba:case 0x54aa:case 0x54bb:case 0x54cc:case 0x54dd:
        case 0x54b0:case 0x54ba:case 0x54b2:case 0x55b1:case 0x55b2:case 0x55b9:case 0x55ba:case 0x55bb:case 0x55bc:
        case 0x53ab:case 0x53ac:case 0x63c5:case 0x68ca:case 0x63ca:case 0x45a3:case 0x447a:case 0x4484:case 0x4487:case 0x4485:
        case 0xb3:case 0xf7:case 0xf1:case 0xf0:case 0xb2:case 0xea:return true;default:return false;}}
    static void metadata(uint32_t id,const std::vector<uint8_t>& bytes,unsigned depth=1){
        check(depth<=8,"stream-webm-depth-limit");
        for(const auto& child:webm::Data{bytes.data(),bytes.size(),0}.Children()){
            check(master(child.id) || leaf(child.id),"unsupported-stream-webm-metadata");
            if(master(child.id))metadata(child.id,{child.body.bytes,child.body.bytes+child.body.size},depth+1);
        }
        (void)id;
    }
    void append(const Element& e,const std::vector<uint8_t>& bytes={}){
        pending.insert(pending.end(),e.header.begin(),e.header.end());pending.insert(pending.end(),bytes.begin(),bytes.end());
    }
    void initialization(){
        auto e=header();check(e.value.id==0x1a45dfa3,"unsupported-stream-webm-doctype");auto b=body(e,4096);metadata(e.value.id,b);check(webm::Data{b.data(),b.size(),0}.One(0x4282).Text()=="webm","unsupported-stream-webm-doctype");append(e,b);
        e=header();check(e.value.id==0x18538067 && e.value.unknown,"unsupported-stream-webm-segment");append(e);
        for(unsigned n=0;n<32;++n){
            e=header();check(at<=1048576,"stream-webm-head-limit");
            if(e.value.id==0x1f43b675){
                // The initialization parser stops at the Cluster header, including unknown size.
                append(e);info=WebMInitialization([&](uint64_t pos,void* out,size_t count){if(pos>pending.size() || count>pending.size()-pos)return false;std::memcpy(out,pending.data()+pos,count);return true;},pending.size(),0,true);
                check(info.codec=="vp9","unsupported-stream-codec");
                check(info.scale==1000000 && info.frameDuration==40000000,"unsupported-stream-webm-clock");
                // Fixed geometry: reject container cropping/display transformations in this profile.
                webm::Element trackElement;auto read=[&](uint64_t pos,void* out,size_t count){if(pos>pending.size() || count>pending.size()-pos)return false;std::memcpy(out,pending.data()+pos,count);return true;};
                webm::Element root;check(webm::Header(read,0,pending.size(),root),"unsupported-stream-webm-header");check(webm::Header(read,root.end,pending.size(),root),"unsupported-stream-webm-header");
                for(uint64_t pos=root.data;pos<info.headerEnd;){webm::Element x;check(webm::Header(read,pos,pending.size(),x),"unsupported-stream-webm-header");if(x.id==0x1654ae6b)trackElement=x;
                    if(x.id==0x1549a966){auto duration=webm::Data{pending.data()+x.data,size_t(x.size),0}.One(0x4489,true);check(!duration.bytes || duration.Float()==double(expected*40),"unsupported-stream-webm-clock");}pos=x.end;}
                check(trackElement.id==0x1654ae6b,"unsupported-stream-webm-track");auto entry=webm::Data{pending.data()+trackElement.data,size_t(trackElement.size),0}.One(0xae);auto v=entry.One(0xe0);
                for(auto id:{0x54aau,0x54bbu,0x54ccu,0x54ddu,0x53c0u,0x53b8u})check(webm::OptionalNumber(v,id,0)==0,"unsupported-stream-webm-geometry");
                check(webm::OptionalNumber(v,0x54b2,0)==0 && webm::OptionalNumber(v,0x54b0,info.width)==info.width && webm::OptionalNumber(v,0x54ba,info.height)==info.height,"unsupported-stream-webm-geometry");
                check(webm::OptionalNumber(entry,0x56bb,0)==0,"unsupported-stream-webm-track");
                startCluster(e);return;
            }
            check(e.value.id==0x1549a966 || e.value.id==0x1654ae6b || e.value.id==0x114d9b74 || e.value.id==0x1254c367 || e.value.id==0xec || e.value.id==0xbf,"unsupported-stream-webm-metadata");
            check(!e.value.unknown && e.value.size<=1048576-at,"stream-webm-head-limit");b=body(e,1048576);
            if(master(e.value.id))metadata(e.value.id,b);append(e,b);
        }
        throw std::runtime_error("stream-webm-head-limit");
    }
    void startCluster(const Element& e){
        check(++topCount<=32768 && ++clusterCount<=8192,"stream-webm-element-limit");
        check(e.value.unknown || e.value.end-e.value.start<=16777216,"stream-webm-cluster-limit");
        clusterStart=e.value.start;clusterEnd=e.value.end;inCluster=true;clock=false;clusterPackets=0;children=0;
    }
    void endCluster(){check(clock && clusterPackets,"unsupported-stream-webm-cluster");inCluster=false;}
    void block(const std::vector<uint8_t>& b){
        check(clock && b.size()>=5,"unsupported-stream-webm-block");const auto w=webm::Width(b[0],8);check(b.size()>w+3,"unsupported-stream-webm-block");
        uint64_t track=b[0]&((0x80u>>(w-1))-1);for(unsigned i=1;i<w;++i)track=(track<<8)|b[i];
        check(track==info.track,"unsupported-stream-webm-track");const auto flags=b[w+2];check(!(flags&0x7f),"unsupported-stream-webm-block");
        const auto relative=int16_t(uint16_t(b[w])<<8|b[w+1]);check(clusterTime<=uint64_t(INT64_MAX),"unsupported-stream-webm-clock");
        const auto ticks=AddTime(int64_t(clusterTime),relative);check(ticks>=0 && uint64_t(ticks)==validated*40,"unsupported-stream-webm-clock");
        check(validated<expected && validated<50000,"incomplete-stream-webm-vp9-samples");const auto* p=b.data()+w+3;const auto size=b.size()-w-3;
        const bool key=(flags&0x80)!=0;check(key==((p[0]&4)==0),"unsupported-stream-webm-vp9-key");
        if(!configured){
            check(size>=5 && key,"unsupported-stream-vp9-frame");config.profile=((p[0]>>5)&1)|(((p[0]>>4)&1)<<1);
            check(config.profile==0 || config.profile==2,"unsupported-stream-vp9-configuration");config.depth=config.profile==2?10:8;config.chroma=1;
            config.range=(p[4]>>(config.profile==2?3:4))&1;
            check((info.vp9Profile<0 || info.vp9Profile==int(config.profile)) && (info.vp9Depth<0 || info.vp9Depth==int(config.depth)),"unsupported-stream-vp9-configuration");
            configured=true;
        }
        VP9ContinuousPacket(p,size,config,info.width,info.height,validated==0,check);
        packets.push_back({uint64_t(ticks),at-b.size()+w+3,size,key,digest(p,size)});++validated;++clusterPackets;
    }
    bool next(){
        if(eof)return false;
        if(inCluster && at==clusterEnd)endCluster();
        if(at==length){
            // Physical end is not EOF until the parent confirms writer completion.
            uint8_t byte;check(rawRead(&byte,1)==0,"unsupported-stream-webm-tail");
            if(inCluster){check(clusterEnd==UINT64_MAX,"truncated-stream-webm-cluster");endCluster();}
            check(validated==expected,"incomplete-stream-webm-vp9-samples");eof=true;return false;
        }
        auto e=header();
        const bool top=e.value.id==0x1f43b675 || e.value.id==0x1c53bb6b || e.value.id==0x1254c367;
        if(inCluster && clusterEnd==UINT64_MAX && top){check(e.value.start-clusterStart<=16777216,"stream-webm-cluster-limit");endCluster();}
        if(!inCluster){
            if(e.value.id==0x1f43b675){startCluster(e);append(e);return true;}
            check(++topCount<=32768 && (e.value.id==0x1c53bb6b || e.value.id==0x1254c367 || e.value.id==0xec || e.value.id==0xbf),"unsupported-stream-webm-metadata");
            auto b=body(e,1048576);if(master(e.value.id))metadata(e.value.id,b);append(e,b);return true;
        }
        check(++children<=65536,"stream-webm-element-limit");
        check(e.value.unknown==false && e.value.size<=16777216 && e.value.end<=clusterEnd && e.value.end-clusterStart<=16777216,"stream-webm-cluster-limit");
        check(e.value.id==0xe7 || e.value.id==0xa3 || e.value.id==0xec || e.value.id==0xbf,"unsupported-stream-webm-block");auto b=body(e,16777216);
        if(e.value.id==0xe7){check(!clock,"unsupported-stream-webm-clock");clusterTime=webm::Data{b.data(),b.size(),0}.Number();clock=true;}
        else if(e.value.id==0xa3)block(b);
        append(e,b);return true;
    }
public:
    static void remap(const std::exception& error){
        const std::string code=error.what();if(code.rfind("unsupported-",0)==0 || code.rfind("stream-",0)==0 || code.rfind("truncated-",0)==0 || code.rfind("incomplete-",0)==0)throw std::runtime_error(code);
        throw std::runtime_error("unsupported-stream-webm-header");
    }
    ContinuousWebmVp9(Read read,uint64_t bytes,uint64_t count):rawRead(std::move(read)),length(bytes),expected(count){try{
        check(bool(rawRead) && bytes>0 && bytes<=134217728 && count>0 && count<=50000,"unsupported-stream-webm-source");
        initialization();while(!configured)check(next(),"unsupported-stream-vp9-frame");
    }catch(const std::exception& e){remap(e);}}
    size_t read(uint8_t* out,size_t count){try{
        check(count>0,"invalid-backend-read");
        while(delivered==pending.size()){pending.clear();delivered=0;if(!next())return 0;}
        const auto n=(std::min)(count,pending.size()-delivered);std::memcpy(out,pending.data()+delivered,n);delivered+=n;return n;
    }catch(const std::exception& e){remap(e);return 0;}}
    const Vp9Configuration& configuration()const{return config;}
    unsigned width()const{return info.width;}unsigned height()const{return info.height;}
    void consume(const uint8_t* data,size_t size,uint64_t milliseconds,bool key){
        check(!packets.empty(),"unsupported-stream-webm-packet-map");const auto& p=packets.front();
        check(p.bytes==size && p.milliseconds==milliseconds && p.key==key && p.sha256==digest(data,size),"unsupported-stream-webm-packet-map");packets.pop_front();
    }
    const std::deque<Packet>& pendingPackets()const{return packets;}
    void finish(){check(eof && packets.empty() && validated==expected,"incomplete-stream-webm-vp9-samples");}
};
} // namespace harvester
