// Bounded, single-track WebM index for explicitly validated schema-2 sources.
// GPL-3.0-or-later. Decoder bytes remain in the pinned ResourceSource.
#pragma once
#include "HarvesterState.h"
#include "HarvesterAV1.h"
#include "HarvesterVorbis.h"
#include <functional>
#include <cstring>
#include <set>

namespace harvester {
struct WebMWaiting:std::runtime_error {WebMWaiting():std::runtime_error("Wait for complete WebM initialization") {}};
using WebMRead=std::function<bool(uint64_t,void*,size_t)>;
namespace webm {
inline void Check(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
struct Element {uint32_t id=0;uint64_t start=0,data=0,end=0,size=0;bool unknown=false;};
inline unsigned Width(uint8_t first,unsigned maximum) {
    for(unsigned n=1;n<=maximum;++n)if(first&(0x80u>>(n-1)))return n;
    throw std::runtime_error("Invalid EBML variable integer");
}
inline bool Header(const uint8_t* bytes,size_t size,Element& e) {
    if(!size)return false;const auto ids=Width(bytes[0],4);
    if(size<=ids)return false;const auto sizes=Width(bytes[ids],8);
    if(size<ids+sizes)return false;
    e.id=0;for(unsigned i=0;i<ids;++i)e.id=(e.id<<8)|bytes[i];
    e.size=bytes[ids]&((0x80u>>(sizes-1))-1);
    for(unsigned i=1;i<sizes;++i)e.size=(e.size<<8)|bytes[ids+i];
    e.unknown=e.size==((uint64_t(1)<<(7*sizes))-1);e.data=ids+sizes;
    Check(e.unknown || e.size<=uint64_t(INT64_MAX)-e.data,"EBML element size overflow");
    e.end=e.unknown?UINT64_MAX:e.data+e.size;return true;
}
inline bool Header(const WebMRead& read,uint64_t at,uint64_t prefix,Element& e) {
    if(at>=prefix)return false;uint8_t bytes[12]{};const auto size=size_t((std::min)(prefix-at,uint64_t(12)));
    Check(read(at,bytes,size),"WebM header I/O failed");if(!Header(bytes,size,e))return false;
    Check(e.unknown || e.end<=uint64_t(INT64_MAX)-at,"EBML offset overflow");
    e.start=at;e.data+=at;if(!e.unknown)e.end+=at;return true;
}
struct Data {
    const uint8_t* bytes=nullptr;size_t size=0;uint64_t base=0;
    uint64_t Number() const {Check(size>0 && size<=8,"Invalid EBML integer width");uint64_t n=0;for(size_t i=0;i<size;++i)n=(n<<8)|bytes[i];return n;}
    int64_t Signed() const {auto n=Number();if(size<8 && (bytes[0]&128))n|=UINT64_MAX<<(size*8);return int64_t(n);}
    double Float() const {
        Check(size==4 || size==8,"Invalid EBML float width");const auto n=Number();
        if(size==4){const uint32_t bits32=static_cast<uint32_t>(n);float f=0.0f;std::memcpy(&f,&bits32,4);return f;}
        double d=0.0;std::memcpy(&d,&n,8);return d;
    }
    std::string Text() const {return std::string(reinterpret_cast<const char*>(bytes),size);}
    struct Item;
    std::vector<Item> Children(size_t limit=128) const;
    Data One(uint32_t id,bool optional=false) const;
};
struct Data::Item {uint32_t id;Data body;};
inline std::vector<Data::Item> Data::Children(size_t limit) const {
    std::vector<Item> items;
    for(size_t at=0;at<size;) {
        Element e;Check(Header(bytes+at,size-at,e) && !e.unknown && e.end<=size-at,"Truncated/unknown-size nested EBML element");
        Check(items.size()<limit,"Too many EBML elements");items.push_back({e.id,{bytes+at+size_t(e.data),size_t(e.size),base+at+e.data}});at+=size_t(e.end);
    }
    return items;
}
inline Data Data::One(uint32_t id,bool optional) const {
    Data found;unsigned count=0;for(const auto& e:Children())if(e.id==id){found=e.body;++count;}
    Check(count<=1 && (count || optional),"Missing/duplicate EBML field");return found;
}
inline std::vector<uint8_t> Body(const WebMRead& read,const Element& e,uint64_t prefix,uint64_t limit) {
    Check(!e.unknown && e.size<=limit,"Oversized/unknown-size WebM metadata or cluster");
    if(e.end>prefix)throw WebMWaiting();std::vector<uint8_t> bytes(size_t(e.size));
    Check(bytes.empty() || read(e.data,bytes.data(),bytes.size()),"WebM body I/O failed");return bytes;
}
inline uint64_t OptionalNumber(Data parent,uint32_t id,uint64_t fallback) {const auto d=parent.One(id,true);return d.bytes?d.Number():fallback;}
} // namespace webm
struct WebMInfo {
    uint64_t headerEnd=0,segmentEnd=UINT64_MAX,track=0,scale=1000000,frameDuration=0,delay=0;
    unsigned channels=0,width=0,height=0;VorbisInfo vorbis;int vp9Profile=-1,vp9Depth=-1;
    std::string codec;
    std::vector<uint8_t> av1Sequence;
};
inline WebMInfo WebMInitialization(const WebMRead& read,uint64_t prefix,int kind,bool headerOnly=false) {
    using namespace webm;WebMInfo result;Element e;
    if(!Header(read,0,prefix,e))throw WebMWaiting();Check(e.id==0x1a45dfa3,"Expected EBML header");
    auto bytes=Body(read,e,prefix,4096);Data header{bytes.data(),bytes.size(),e.data};
    Check(header.One(0x4282).Text()=="webm","Only the WebM DocType is enabled");
    Check(OptionalNumber(header,0x4286,1)==1 && OptionalNumber(header,0x42f7,1)==1
          && OptionalNumber(header,0x42f2,4)>=1 && OptionalNumber(header,0x42f2,4)<=4
          && OptionalNumber(header,0x42f3,8)>=1 && OptionalNumber(header,0x42f3,8)<=8
          && OptionalNumber(header,0x4285,1)>=1 && OptionalNumber(header,0x4285,1)<=4,"Unsupported EBML/WebM reader version");
    const auto next=e.end;if(!Header(read,next,prefix,e))throw WebMWaiting();Check(e.id==0x18538067,"Expected one WebM Segment");
    result.segmentEnd=e.end;uint64_t at=e.data;bool info=false,tracks=false;
    while(at<1048576) {
        if(headerOnly && at==prefix && info && tracks){result.headerEnd=at;return result;}
        if(!Header(read,at,prefix,e))throw WebMWaiting();
        Check(e.end<=result.segmentEnd,"WebM element exceeds Segment");
        if(e.id==0x1f43b675){Check(info && tracks,"WebM Info/Tracks must precede clusters");result.headerEnd=at;return result;}
        bytes=Body(read,e,prefix,1048576);Data data{bytes.data(),bytes.size(),e.data};
        if(e.id==0x1549a966) {
            Check(!info,"Repeated WebM Info");info=true;result.scale=OptionalNumber(data,0x2ad7b1,1000000);
            Check(result.scale>0 && result.scale<=1000000000 && result.scale%100==0,"Unsupported WebM timestamp scale");
        }else if(e.id==0x1654ae6b) {
            Check(!headerOnly || info,"Segmented WebM requires Info before Tracks");
            Check(!tracks,"Repeated WebM Tracks");tracks=true;const auto entry=data.One(0xae);
            result.track=entry.One(0xd7).Number();Check(result.track>0 && result.track<=UINT32_MAX,"Invalid WebM track number");
            Check(entry.One(0x83).Number()==uint64_t(kind?2:1),"Wrong WebM track kind");
            Check(!entry.One(0x6d80,true).bytes,"Compressed/encrypted WebM tracks are unsupported");
            auto factor=entry.One(0x23314f,true);Check(!factor.bytes || factor.Float()==1.0,"Unsupported WebM track timestamp scale");
            const auto id=entry.One(0x86).Text();
            result.delay=OptionalNumber(entry,0x56aa,0);result.frameDuration=OptionalNumber(entry,0x23e383,0);
            if(!kind) {
                Check((id=="V_VP8" || id=="V_VP9" || id=="V_AV1") && result.delay==0,"Only VP8/VP9/AV1 video is enabled in this WebM profile");result.codec=id=="V_AV1"?"av1":id=="V_VP8"?"vp8":"vp9";
                const auto video=entry.One(0xe0);const auto width=video.One(0xb0).Number(),height=video.One(0xba).Number();
                Check(width>0 && width<=16384 && height>0 && height<=16384,"Invalid WebM dimensions");result.width=unsigned(width);result.height=unsigned(height);
                Check(result.frameDuration>0 && result.frameDuration<=1000000000 && result.frameDuration%100==0,"WebM video requires a bounded DefaultDuration");
                const auto config=entry.One(0x63a2,true);std::set<uint8_t> features;
                if(result.codec=="av1")result.av1Sequence=AV1Configuration(config.bytes,config.size,Check);
                else if(result.codec=="vp8")Check(!config.bytes,"VP8 CodecPrivate is unsupported");
                else for(size_t i=0;i<config.size;) {
                    Check(config.size-i>=3 && config.bytes[i+1]==1 && features.insert(config.bytes[i]).second,"Invalid VP9 CodecPrivate feature framing");
                    const auto field=config.bytes[i],value=config.bytes[i+2];
                    Check(field>=1 && field<=4,"Unsupported VP9 CodecPrivate feature");
                    if(field==1){Check(value==0 || value==2,"Unsupported VP9 profile");result.vp9Profile=value;}
                    if(field==3){Check(value==8 || value==10,"Unsupported VP9 depth");result.vp9Depth=value;}
                    if(field==4)Check(value<=1,"Only VP9 4:2:0 is enabled");i+=3;
                }
            }else {
                Check(id=="A_OPUS" || id=="A_VORBIS","Only Opus/Vorbis audio is enabled in this WebM profile");result.codec=id=="A_OPUS"?"opus":"vorbis";
                if(result.codec=="vorbis"){
                    const auto config=entry.One(0x63a2);result.vorbis=VorbisConfiguration(config.bytes,config.size);result.channels=result.vorbis.channels;
                    const auto audio=entry.One(0xe1);Check(OptionalNumber(audio,0x9f,1)==result.channels && audio.One(0xb5).Float()==result.vorbis.rate,"Vorbis channels/rate mismatch");
                    Check(result.delay==0 && OptionalNumber(entry,0x56bb,0)==0,"Vorbis delay/preroll is unsupported");
                }else {
                const auto config=entry.One(0x63a2);
                Check(config.size==19 && !std::memcmp(config.bytes,"OpusHead",8) && config.bytes[8]==1 && config.bytes[18]==0,"Invalid/unsupported OpusHead");
                result.channels=config.bytes[9];Check(result.channels==1 || result.channels==2,"Only Opus mono/stereo is enabled");
                const auto skip=uint64_t(config.bytes[10])|(uint64_t(config.bytes[11])<<8);
                Check(result.delay<=2000000000 && result.delay*48000==skip*1000000000,"Opus CodecDelay/pre-skip mismatch");
                Check(OptionalNumber(entry,0x56bb,0)==80000000,"Opus requires the 80 ms seek preroll profile");
                const auto audio=entry.One(0xe1);Check(OptionalNumber(audio,0x9f,1)==result.channels && audio.One(0xb5).Float()==48000.0,"Opus channels/rate mismatch");
                }
            }
        }else Check(e.id==0x114d9b74 || e.id==0x1254c367 || e.id==0xec || e.id==0xbf,"Unsupported WebM initialization element");
        at=e.end;
    }
    throw std::runtime_error("WebM initialization exceeds 1 MiB");
}
class WebMIndex {
    WebMInfo info;uint64_t position=0,scannedPrefix=0;bool initialized=false;int kind=0;int64_t origin=0;
    std::vector<Sample> samples;
    void InitialAv1(webm::Data packet,bool sync) const {
        using namespace webm;Check(sync,"Expected an initial AV1 keyframe");
        bool sequence=false,frame=false;unsigned units=0;
        for(size_t at=0;at<packet.size;) {
            Check(++units<=128,"Too many initial AV1 packet OBUs");
            const auto header=packet.bytes[at++];const unsigned type=(header>>3)&15;
            Check(!(header&0x81),"Invalid initial AV1 OBU header");
            Check(type==1 || type==2 || type==5 || type==6 || type==15,"Unsupported initial AV1 OBU type");
            if(header&4)Check(at<packet.size && packet.bytes[at++]==0,"Unsupported initial AV1 OBU layer");
            uint64_t length=packet.size-at;
            if(header&2) {
                length=0;bool ended=false;
                for(unsigned i=0;i<8;++i) {
                    Check(at<packet.size,"Truncated initial AV1 OBU size");const auto part=packet.bytes[at++];
                    length|=uint64_t(part&127)<<(7*i);if(!(part&128)){ended=true;break;}
                }
                Check(ended,"Invalid initial AV1 OBU size");
            }
            Check(length<=packet.size-at,"Initial AV1 OBU exceeds packet");
            if(type==1) {
                Check(!sequence && !frame && length==info.av1Sequence.size()
                    && !std::memcmp(packet.bytes+at,info.av1Sequence.data(),size_t(length)),"AV1 packet/configuration sequence mismatch");sequence=true;
            }else if(type==6) {
                Check(sequence && !frame && length>0,"Initial AV1 frame lacks sequence or payload");
                const bool reduced=(info.av1Sequence[0]&8)!=0;
                Check(reduced || !(packet.bytes[at]&0xe0),"Initial AV1 frame is not a keyframe");frame=true;
            }else if(type==2)Check(length==0 && !sequence && !frame,"Invalid initial AV1 temporal delimiter");
            at+=size_t(length);
        }
        Check(sequence && frame,"Initial AV1 packet requires sequence and frame OBUs");
    }
    static int64_t OpusDuration(webm::Data packet) {
        using namespace webm;Check(packet.size>0,"Empty Opus packet");const auto toc=packet.bytes[0];const unsigned config=toc>>3;
        const int64_t durations[]={10000000,20000000,40000000,60000000};
        const auto frame=config>=16?int64_t(2500000)<<(config&3):config>=12?int64_t(10000000)<<(config&1):durations[config&3];
        unsigned count=(toc&3)==0?1:(toc&3)==3?0:2;
        if(!count){Check(packet.size>=2,"Truncated Opus frame count");count=packet.bytes[1]&63;}
        Check(count>0 && count<=48 && frame*count<=120000000,"Invalid Opus packet duration");return frame*count;
    }
    void Block(webm::Data data,uint64_t cluster,bool sync,uint64_t duration,int64_t padding,std::vector<Sample>& append) {
        using namespace webm;Check(data.size>=4,"Truncated WebM block");const auto width=Width(data.bytes[0],8);
        Check(data.size>width+3,"Empty/truncated WebM packet");uint64_t track=data.bytes[0]&((0x80u>>(width-1))-1);
        for(unsigned i=1;i<width;++i)track=(track<<8)|data.bytes[i];Check(track==info.track,"WebM block references another track");
        const auto relative=int16_t(uint16_t(data.bytes[width])<<8|data.bytes[width+1]);const auto flags=data.bytes[width+2];
        Check(!(flags&0x76),"WebM lacing/reserved block flags are unsupported");
        const auto skip=width+3;Data packet{data.bytes+skip,data.size-skip,data.base+skip};
        if(!kind && samples.empty() && append.empty() && info.codec=="av1")InitialAv1(packet,sync);
        else if(!kind && samples.empty() && append.empty() && info.codec=="vp8"){
            Check(sync && packet.size>=10 && !(packet.bytes[0]&1) && ((packet.bytes[0]>>1)&7)<=3 && (packet.bytes[0]&16)
                && packet.bytes[3]==0x9d && packet.bytes[4]==1 && packet.bytes[5]==0x2a,"Expected an initial displayed VP8 keyframe");
            const auto partition=(uint32_t(packet.bytes[0])|(uint32_t(packet.bytes[1])<<8)|(uint32_t(packet.bytes[2])<<16))>>5;
            Check(partition>0 && partition<=packet.size-10,"Invalid VP8 first partition size");
            Check((unsigned(packet.bytes[6])|(unsigned(packet.bytes[7]&63)<<8))==info.width
                && (unsigned(packet.bytes[8])|(unsigned(packet.bytes[9]&63)<<8))==info.height
                && !(packet.bytes[7]&192) && !(packet.bytes[9]&192),"VP8 dimensions/scaling mismatch");
        }else if(!kind && samples.empty() && append.empty()) {
            Check(sync && packet.size>=5 && (packet.bytes[0]>>6)==2 && !(packet.bytes[0]&12)
                && packet.bytes[1]==0x49 && packet.bytes[2]==0x83 && packet.bytes[3]==0x42,"Expected an initial VP9 keyframe");
            const int profile=((packet.bytes[0]>>5)&1)|(((packet.bytes[0]>>4)&1)<<1);
            Check(profile==0 || (profile==2 && !(packet.bytes[4]&128)),"Only VP9 Profile 0/8-bit or 2/10-bit is enabled");
            const int depth=profile==2?10:8;
            Check((info.vp9Profile<0 || info.vp9Profile==profile) && (info.vp9Depth<0 || info.vp9Depth==depth),"VP9 header/CodecPrivate mismatch");
        }
        Check(cluster<=uint64_t(INT64_MAX),"WebM clock overflow");const auto ticks=AddTime(int64_t(cluster),relative);
        Check(ticks>=0 && uint64_t(ticks)<=uint64_t(INT64_MAX)/info.scale,"WebM timestamp overflow");
        const auto raw=uint64_t(ticks)*info.scale;const auto start=AddTime(AddTime(int64_t(raw/100),-int64_t(info.delay/100)),-origin);
        int64_t length=kind?(info.codec=="vorbis"?info.vorbis.Duration(packet.bytes,packet.size):OpusDuration(packet)):int64_t(info.frameDuration);
        if(duration){Check(duration<=uint64_t(INT64_MAX)/info.scale,"WebM duration overflow");const auto ns=duration*info.scale;Check(ns>0 && ns<=1000000000,"Unsupported WebM block duration");if(kind)Check(std::abs(int64_t(ns)-length)<=(info.codec=="vorbis"?int64_t(info.scale):0),"Audio BlockDuration mismatch");length=int64_t(ns);}
        Check(padding>=0 && padding<=length,"Unsupported WebM discard padding");
        Sample sample;sample.offset=packet.base;sample.size=packet.size;sample.start=sample.decode=start;sample.end=AddTime(start,(length-padding)/100);sample.sync=kind || sync;
        const auto previous=!append.empty()?append.back().decode:!samples.empty()?samples.back().decode:INT64_MIN;
        Check(start>=previous,"WebM timestamps regressed");Check(samples.size()+append.size()<2000000,"Too many WebM packets");append.push_back(sample);
    }
    void Cluster(webm::Data data) {
        using namespace webm;const auto children=data.Children(65536);bool clock=false;uint64_t timestamp=0;std::vector<Sample> append;
        for(const auto& child:children) {
            if(child.id==0xe7){Check(!clock,"Repeated Cluster timestamp");clock=true;timestamp=child.body.Number();}
            else if(child.id==0xa3){Check(clock,"Block before Cluster timestamp");Check(child.body.size>0,"Empty SimpleBlock");const auto w=Width(child.body.bytes[0],8);Check(child.body.size>w+2,"Truncated SimpleBlock");Block(child.body,timestamp,(child.body.bytes[w+2]&128)!=0,0,0,append);}
            else if(child.id==0xa0) {
                Check(clock,"BlockGroup before Cluster timestamp");auto block=child.body.One(0xa1);uint64_t duration=0;int64_t padding=0;bool reference=false,hasDuration=false,hasPadding=false;unsigned references=0;
                for(const auto& field:child.body.Children()) {
                    if(field.id==0x9b){Check(!hasDuration,"Repeated BlockDuration");hasDuration=true;duration=field.body.Number();Check(duration>0,"Zero BlockDuration");}else if(field.id==0x75a2){Check(!hasPadding,"Repeated DiscardPadding");hasPadding=true;padding=field.body.Signed();}else if(field.id==0xfb){Check(++references<=2,"Too many block references");field.body.Signed();reference=true;}
                    else Check(field.id==0xa1 || field.id==0xec || field.id==0xbf,"Unsupported WebM BlockGroup field");
                }
                Block(block,timestamp,!reference,duration,padding,append);
            }else Check(child.id==0xa7 || child.id==0xab || child.id==0xec || child.id==0xbf,"Unsupported/encrypted WebM Cluster field");
        }
        Check(clock && !append.empty(),"WebM Cluster lacks timestamp/packets");samples.insert(samples.end(),append.begin(),append.end());
    }
public:
    uint64_t Position() const {return position;}
    bool Initialized() const {return initialized;}
    const std::vector<Sample>& Samples() const {return samples;}
    void Update(const WebMRead& read,uint64_t prefix,int trackKind,bool complete=false,int64_t timestampOffset=0) {
        using namespace webm;Check(prefix>=scannedPrefix,"WebM prefix regressed");scannedPrefix=prefix;kind=trackKind;origin=timestampOffset;
        if(!initialized){try{info=WebMInitialization(read,prefix,kind);}catch(const WebMWaiting&){if(complete)throw;return;}position=info.headerEnd;initialized=true;}
        while(position<prefix) {
            Element e;if(!Header(read,position,prefix,e))break;
            Check(e.end<=info.segmentEnd,"Element exceeds WebM Segment");
            Check(!e.unknown && e.size<=16777216,"Unsupported/oversized WebM cluster or metadata");
            if(e.end>prefix)break;
            if(e.id==0x1f43b675){auto bytes=Body(read,e,prefix,16777216);Cluster({bytes.data(),bytes.size(),e.data});}
            else Check(e.id==0x1c53bb6b || e.id==0x1254c367 || e.id==0xec || e.id==0xbf,"Repeated/unsupported WebM top-level element");
            position=e.end;
        }
        if(complete)Check(position==prefix && (info.segmentEnd==UINT64_MAX || info.segmentEnd==prefix),"Truncated/invalid final WebM Segment");
    }
};
} // namespace harvester
