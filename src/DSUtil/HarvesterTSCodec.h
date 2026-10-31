// Bounded AVC/AAC framing and clock index over staged TS. GPL-3.0-or-later.
// Internal preparation; decoding and player integration remain separate.
#pragma once
#include "HarvesterTS.h"
#include "HarvesterState.h"
#include <memory>
#include <iterator>
#include <type_traits>

namespace harvester { namespace ts {
constexpr size_t MaxAccessUnits=65536,MaxIndexUnits=2000000,MaxIndexRanges=2000000;
constexpr int64_t ClockTolerance=112; // ceil(10,000,000 / 90,000), one TS tick.
inline void CodecCheck(bool value,const char* why,const char* code="ts-codec") {Check(value,why,code);}
struct Bits {
    Data data;size_t at=0;
    uint32_t read(unsigned n) {
        CodecCheck(n<=32 && at<=data.size*8 && n<=data.size*8-at,"Truncated AVC header","ts-codec-truncated");
        uint32_t value=0;for(unsigned i=0;i<n;++i,++at)value=(value<<1)|((data.bytes[at/8]>>(7-at%8))&1);return value;
    }
    uint32_t ue(uint32_t limit=0x7ffffffe) {
        unsigned zero=0;while(!read(1))CodecCheck(++zero<=30,"AVC Golomb value exceeds its bound");
        const auto value=((uint32_t(1)<<zero)-1)+read(zero);CodecCheck(value<=limit,"AVC field exceeds its bound");return value;
    }
    int32_t se(int32_t lo,int32_t hi) {
        const auto value=ue();const auto n=(value&1)?int32_t((value+1)/2):-int32_t(value/2);
        CodecCheck(n>=lo && n<=hi,"AVC signed field exceeds its bound");return n;
    }
    bool more() const {
        const auto left=data.size*8-at;if(left>8)return true;if(!left)return false;
        for(size_t i=0;i<left;++i)if(((data.bytes[(at+i)/8]>>(7-(at+i)%8))&1)!=(i==0?1:0))return true;
        return false;
    }
    void end() {CodecCheck(read(1)==1 && data.size*8-at<8,"Invalid AVC RBSP trailing bits");while(at<data.size*8)CodecCheck(read(1)==0,"Invalid AVC RBSP padding");}
};
inline std::vector<uint8_t> Rbsp(Data nal) {
    CodecCheck(nal.size>=2 && nal.size<=MaxPesBytes && !(nal.bytes[0]&128),"Invalid AVC NAL header");
    std::vector<uint8_t> out;out.reserve(nal.size-1);unsigned zeros=0;
    for(size_t at=1;at<nal.size;++at) {
        const auto byte=nal.bytes[at];
        if(zeros>=2) {
            if(byte==3){CodecCheck(at+1<nal.size && nal.bytes[at+1]<=3,"Invalid AVC emulation prevention");zeros=0;continue;}
            CodecCheck(byte>2,"Unescaped AVC start-code pattern");
        }
        out.push_back(byte);zeros=byte==0?zeros+1:0;
    }
    return out;
}
inline void Scaling(Bits& b,unsigned count) {
    int last=8,next=8;
    for(unsigned i=0;i<count;++i){if(next)next=(last+b.se(-128,127)+256)%256;if(next)last=next;}
}
struct Avc {
    uint32_t width=0,height=0,mbCount=0,spsId=0,ppsId=0,frameBits=0,pocType=0,pocBits=0;
    uint32_t units=0,scale=0;bool bottomPoc=false;uint8_t profile=0,level=0;
};
inline void Hrd(Bits& b) {
    const auto count=b.ue(31)+1;b.read(4);b.read(4);
    for(uint32_t i=0;i<count;++i){b.ue();b.ue();b.read(1);}b.read(5);b.read(5);b.read(5);b.read(5);
}
inline Avc Sps(Data nal) {
    CodecCheck(nal.size>=2 && nal.size<=65536 && (nal.bytes[0]&31)==7 && (nal.bytes[0]&96),"Invalid AVC SPS");
    const auto rbsp=Rbsp(nal);Bits b{{rbsp.data(),rbsp.size()}};Avc out;
    out.profile=uint8_t(b.read(8));CodecCheck(out.profile==66 || out.profile==77 || out.profile==100,"Only AVC Baseline/Main/High 8-bit is enabled");
    CodecCheck(!(b.read(8)&3),"Invalid SPS reserved bits");out.level=uint8_t(b.read(8));
    const unsigned levels[]={10,11,12,13,20,21,22,30,31,32,40,41,42,50,51,52,60,61,62};
    CodecCheck(std::find(std::begin(levels),std::end(levels),out.level)!=std::end(levels),"Unsupported AVC level");
    out.spsId=b.ue(31);
    if(out.profile==100) {
        CodecCheck(b.ue(3)==1 && b.ue(6)==0 && b.ue(6)==0 && b.read(1)==0,"Only 8-bit 4:2:0 AVC is enabled");
        if(b.read(1))for(unsigned i=0;i<8;++i)if(b.read(1))Scaling(b,i<6?16:64);
    }
    out.frameBits=b.ue(12)+4;out.pocType=b.ue(2);
    CodecCheck(out.pocType!=1,"AVC POC type 1 needs another profile");if(out.pocType==0)out.pocBits=b.ue(12)+4;
    const auto references=b.ue(16);b.read(1);
    const auto width=(b.ue(1023)+1)*16,height=(b.ue(1023)+1)*16;out.mbCount=(width/16)*(height/16);
    CodecCheck(b.read(1)==1,"Interlaced AVC is outside this profile");b.read(1);
    uint32_t left=0,right=0,top=0,bottom=0;
    if(b.read(1)){left=b.ue(8192);right=b.ue(8192);top=b.ue(8192);bottom=b.ue(8192);}
    CodecCheck((left+right)*2<width && (top+bottom)*2<height,"Invalid AVC cropping");out.width=width-(left+right)*2;out.height=height-(top+bottom)*2;
    CodecCheck(b.read(1)==1,"AVC VUI timing is required");
    if(b.read(1)){const auto aspect=b.read(8);CodecCheck(aspect<=16 || aspect==255,"Reserved AVC aspect ratio");if(aspect==255)CodecCheck(b.read(16)>0 && b.read(16)>0,"Invalid AVC SAR");}
    if(b.read(1))b.read(1);
    if(b.read(1)){CodecCheck(b.read(3)<=5,"Reserved AVC video format");b.read(1);if(b.read(1)){b.read(8);b.read(8);b.read(8);}}
    if(b.read(1)){b.ue(5);b.ue(5);}
    CodecCheck(b.read(1)==1,"AVC timing_info is required");out.units=b.read(32);out.scale=b.read(32);
    CodecCheck(out.units>0 && out.scale>0 && uint64_t(out.scale)>=uint64_t(out.units)*2 && uint64_t(out.scale)<=uint64_t(out.units)*480 && b.read(1)==1,
        "This AVC index requires explicit fixed frame timing from 1 to 240 fps");
    const bool nalHrd=b.read(1)!=0;if(nalHrd)Hrd(b);const bool vclHrd=b.read(1)!=0;if(vclHrd)Hrd(b);if(nalHrd || vclHrd)b.read(1);
    CodecCheck(b.read(1)==0,"AVC pic_struct timing is outside this profile");
    if(b.read(1)){b.read(1);b.ue(16);b.ue(16);b.ue(16);b.ue(16);const auto reorder=b.ue(16),buffer=b.ue(16);CodecCheck(reorder<=buffer && references<=buffer,"Invalid AVC reorder/buffer limits");}
    b.end();return out;
}
inline void Pps(Data nal,Avc& avc) {
    CodecCheck(nal.size>=2 && nal.size<=65536 && (nal.bytes[0]&31)==8 && (nal.bytes[0]&96),"Invalid AVC PPS");
    const auto rbsp=Rbsp(nal);Bits b{{rbsp.data(),rbsp.size()}};avc.ppsId=b.ue(255);
    CodecCheck(b.ue(31)==avc.spsId,"PPS references another SPS");const auto cabac=b.read(1);CodecCheck(!cabac || avc.profile!=66,"CABAC in Baseline AVC");avc.bottomPoc=b.read(1)!=0;
    CodecCheck(b.ue(7)==0,"AVC slice groups need another profile");b.ue(31);b.ue(31);b.read(1);CodecCheck(b.read(2)<=2,"Reserved AVC weighted prediction");
    b.se(-26,25);b.se(-26,25);b.se(-12,12);b.read(1);b.read(1);CodecCheck(b.read(1)==0,"AVC redundant slices need another profile");
    if(b.more()){const bool transform=b.read(1)!=0,scaling=b.read(1)!=0;CodecCheck((!transform && !scaling) || avc.profile==100,"AVC 8x8 transform/scaling requires High profile");if(scaling)for(unsigned i=0;i<(transform?8u:6u);++i)if(b.read(1))Scaling(b,i<6?16:64);b.se(-12,12);}
    b.end();
}
struct Nal {Data bytes;unsigned type=0;};
inline std::vector<Nal> AnnexB(Data data) {
    std::vector<Nal> out;size_t begin=0;bool found=false;
    for(size_t at=0;at+2<data.size;) {
        size_t prefix=0;
        if(data.bytes[at]==0 && data.bytes[at+1]==0){if(data.bytes[at+2]==1)prefix=3;else if(at+3<data.size && data.bytes[at+2]==0 && data.bytes[at+3]==1)prefix=4;}
        if(!prefix){++at;continue;}
        if(found) {
            size_t end=at;while(end>begin && data.bytes[end-1]==0)--end;
            CodecCheck(end>begin && out.size()<128,"Empty/excessive AVC NAL units");out.push_back({{data.bytes+begin,end-begin},unsigned(data.bytes[begin]&31)});
        }else {for(size_t i=0;i<at;++i)CodecCheck(data.bytes[i]==0,"Bytes before AVC Annex-B start");found=true;}
        begin=at+prefix;at=begin;
    }
    CodecCheck(found,"AVC Annex-B start code is required");size_t end=data.size;while(end>begin && data.bytes[end-1]==0)--end;
    CodecCheck(end>begin && out.size()<128,"Empty/excessive final AVC NAL");out.push_back({{data.bytes+begin,end-begin},unsigned(data.bytes[begin]&31)});
    return out;
}
struct Slice {uint32_t first=0,type=0,frame=0,poc=0,idr=0;int32_t delta=0;bool key=false;};
inline Slice SliceHeader(Data nal,const Avc& avc) {
    const auto rbsp=Rbsp(nal);Bits b{{rbsp.data(),rbsp.size()}};Slice s;s.key=(nal.bytes[0]&31)==5;
    s.first=b.ue(1048575);s.type=b.ue(9)%5;CodecCheck(s.type<=2 && b.ue(255)==avc.ppsId,"Unsupported AVC slice type or PPS");
    CodecCheck(avc.profile!=66 || s.type!=1,"B slices are outside Baseline AVC");
    CodecCheck(!s.key || ((nal.bytes[0]&96) && s.type==2),"IDR requires reference I slices");
    s.frame=b.read(avc.frameBits);if(s.key){CodecCheck(s.frame==0,"AVC IDR frame_num must be zero");s.idr=b.ue(65535);}
    if(avc.pocType==0){s.poc=b.read(avc.pocBits);if(avc.bottomPoc)s.delta=b.se(-0x3fffffff,0x3fffffff);}
    CodecCheck(rbsp.size()*8-b.at>=8,"Truncated AVC slice payload","ts-codec-truncated");
    return s;
}
inline void Sei(Data nal) {
    const auto rbsp=Rbsp(nal);size_t at=0;unsigned count=0;
    while(at<rbsp.size() && !(at+1==rbsp.size() && rbsp[at]==128)) {
        CodecCheck(++count<=128,"Too many AVC SEI messages");
        auto field=[&](){uint64_t value=0;uint8_t byte;do{CodecCheck(at<rbsp.size(),"Truncated AVC SEI","ts-codec-truncated");byte=rbsp[at++];value+=byte;CodecCheck(value<=1048576,"AVC SEI exceeds its bound");}while(byte==255);return size_t(value);};
        const auto type=field(),size=field();CodecCheck(size<=rbsp.size()-at && (type!=5 || size>=16),"Invalid AVC SEI payload","ts-codec-truncated");at+=size;
    }
    CodecCheck(at+1==rbsp.size() && rbsp[at]==128,"Invalid AVC SEI trailing bits");
}
struct CodecConfig {
    Avc avc;std::vector<uint8_t> sps,pps;unsigned rate=0,channels=0,samplingIndex=0;
    bool video=false;
};
inline int64_t Cadence(const CodecConfig& config,uint64_t count) {
    CodecCheck(count<=MaxIndexUnits,"TS cadence exceeds index bound","ts-limit");
    const uint64_t numerator=config.video?uint64_t(config.avc.units)*20000000:uint64_t(1024)*10000000;
    const uint64_t denominator=config.video?config.avc.scale:config.rate;
    CodecCheck(denominator>0,"Missing codec clock");
    // Split the rational before multiplying: malicious but proportionate VUI
    // values can use all 32 bits even at an ordinary frame rate.
    return int64_t((numerator/denominator)*count+((numerator%denominator)*count)/denominator);
}
struct AccessUnit {Sample sample;std::vector<Range> encoded,payload;};
static_assert(std::is_nothrow_move_constructible<AccessUnit>::value,"TS publication requires no-throw access-unit moves");
struct AccessSegment {
    CodecConfig config;std::vector<AccessUnit> units;
    int64_t start=0,end=0,decodeStart=0,decodeEnd=0;size_t ranges=0;
};
inline std::vector<Range> Subranges(const Pes& pes,size_t offset,size_t size) {
    CodecCheck(offset<=pes.size && size<=pes.size-offset,"Access unit exceeds PES payload");std::vector<Range> out;
    for(const auto& range:pes.payload){const auto skip=(std::min)(offset,range.size);offset-=skip;const auto count=(std::min)(size,range.size-skip);if(count){out.push_back({range.offset+skip,count});size-=count;}if(!size)break;}
    CodecCheck(!offset && !size,"Incomplete access unit ranges");return out;
}
inline int64_t Clock(uint64_t raw,uint64_t origin) {CodecCheck(raw<(uint64_t(1)<<33) && origin<(uint64_t(1)<<33),"TS timestamp exceeds 33-bit clock");return AddTime(Time100ns(int64_t(raw),90000),-Time100ns(int64_t(origin),90000));}
inline bool Close(int64_t a,int64_t b){return a>=b?uint64_t(a)-uint64_t(b)<=uint64_t(ClockTolerance):uint64_t(b)-uint64_t(a)<=uint64_t(ClockTolerance);}
template<class Load> inline AccessSegment AccessPes(const Program& program,const Segment& transport,Load&& load,uint64_t origin=0,const CodecConfig* previous=nullptr) {
    AccessSegment out;out.config.video=program.streamType==0x1b;
    if(previous)CodecCheck(previous->video==out.config.video,"Codec kind changed","ts-frame-config");
    bool seenSps=false,seenPps=false;int64_t previousDecodeEnd=0;bool first=true;
    for(const auto& pes:transport.packets) {
        const auto bytes=load(pes);
        CodecCheck(bytes.size()==pes.size,"PES size/ranges mismatch");
        auto append=[&](AccessUnit unit) {
            CodecCheck(out.units.size()<MaxAccessUnits,"Too many access units","ts-limit");
            unit.sample.offset=pes.transportOffset;unit.sample.size=pes.transportEnd-pes.transportOffset;
            out.ranges+=unit.encoded.size()+unit.payload.size();CodecCheck(out.ranges<=MaxIndexRanges,"Access ranges exceed index bound","ts-limit");
            const auto decodeEnd=AddTime(unit.sample.decode,unit.sample.end-unit.sample.start);
            CodecCheck(first || Close(unit.sample.decode,previousDecodeEnd),"Frame decode cadence has a gap or overlap","ts-frame-timestamp");
            CodecCheck(first || Close(unit.sample.decode,AddTime(out.decodeStart,Cadence(out.config,out.units.size()))),"Frame decode cadence accumulated drift","ts-frame-timestamp");
            if(first){out.start=unit.sample.start;out.end=unit.sample.end;out.decodeStart=unit.sample.decode;first=false;}
            out.start=(std::min)(out.start,unit.sample.start);out.end=(std::max)(out.end,unit.sample.end);previousDecodeEnd=decodeEnd;out.decodeEnd=decodeEnd;
            out.units.push_back(std::move(unit));
        };
        if(out.config.video) {
            bool aud=false,vcl=false,anyNal=false;unsigned audType=0;Slice initial;uint32_t lastMb=0;
            for(const auto& nal:AnnexB({bytes.data(),bytes.size()})) {
                CodecCheck(nal.bytes.size>=2 && !(nal.bytes.bytes[0]&128),"Invalid AVC NAL framing");
                if(nal.type==9) {
                    CodecCheck(!aud && !vcl && !anyNal,"Invalid AVC AUD placement");
                }
                anyNal=true;
                if(nal.type==6 || nal.type==9 || nal.type==12)CodecCheck(!(nal.bytes.bytes[0]&96),"Invalid non-reference AVC NAL");
                if(nal.type==9) {
                    const auto rbsp=Rbsp(nal.bytes);Bits bits{{rbsp.data(),rbsp.size()}};audType=bits.read(3);bits.end();aud=true;
                }else if(nal.type==7 || nal.type==8) {
                    CodecCheck(aud && !vcl,"AVC configuration must precede slices");
                    auto value=std::vector<uint8_t>(nal.bytes.bytes,nal.bytes.bytes+nal.bytes.size);
                    auto& stored=nal.type==7?out.config.sps:out.config.pps;
                    CodecCheck(stored.empty() || stored==value,"AVC parameter set changed","ts-frame-config");
                    if(previous)CodecCheck((nal.type==7?previous->sps:previous->pps)==value,"AVC configuration changed across segments","ts-frame-config");
                    if(nal.type==7){auto parsed=Sps(nal.bytes);if(!seenSps)out.config.avc=parsed;seenSps=true;}
                    else {CodecCheck(seenSps,"AVC PPS before SPS");Pps(nal.bytes,out.config.avc);seenPps=true;}
                    stored=std::move(value);
                }else if(nal.type==1 || nal.type==5) {
                    CodecCheck(aud && seenSps && seenPps,"AVC frame lacks AUD/SPS/PPS");
                    const auto slice=SliceHeader(nal.bytes,out.config.avc);
                    CodecCheck(slice.first<out.config.avc.mbCount,"AVC slice exceeds coded picture");
                    CodecCheck(slice.type==2?(audType==0 || audType==1 || audType==2 || audType>=5):slice.type==0?(audType==1 || audType==2 || audType>=6):(audType==2 || audType==7),"AUD and AVC slice types disagree");
                    if(!vcl){CodecCheck(slice.first==0,"AVC frame must start with its first slice");initial=slice;vcl=true;}
                    else CodecCheck(slice.first>lastMb && slice.key==initial.key && slice.frame==initial.frame && slice.poc==initial.poc && slice.delta==initial.delta && slice.idr==initial.idr,"Multiple pictures or reordered slices in PES");
                    lastMb=slice.first;
                }else if(nal.type==6){CodecCheck(aud && !vcl,"SEI must precede AVC slices");Sei(nal.bytes);}
                else if(nal.type==12){const auto rbsp=Rbsp(nal.bytes);CodecCheck(!rbsp.empty() && rbsp.back()==128,"Invalid AVC filler");for(size_t i=0;i+1<rbsp.size();++i)CodecCheck(rbsp[i]==255,"Invalid AVC filler bytes");}
                else CodecCheck(false,"Unsupported AVC NAL type");
            }
            CodecCheck(aud && vcl && (!out.units.empty() || initial.key),"Each AVC segment must start with an IDR picture");
            const auto& avc=out.config.avc;AccessUnit unit;unit.encoded=unit.payload=pes.payload;
            unit.sample.start=Clock(pes.pts,origin);unit.sample.decode=Clock(pes.dts,origin);unit.sample.end=AddTime(unit.sample.start,int64_t(uint64_t(avc.units)*20000000/avc.scale));unit.sample.sync=initial.key;append(std::move(unit));
        }else {
            CodecCheck(pes.pts==pes.dts,"Distinct AAC PTS/DTS is outside this profile");size_t at=0;uint64_t samples=0;
            while(at<bytes.size()) {
                CodecCheck(bytes.size()-at>=7,"Truncated ADTS header","ts-codec-truncated");const auto* b=bytes.data()+at;
                CodecCheck(b[0]==255 && b[1]==0xf1 && (b[2]>>6)==1 && (b[6]&3)==0,"Only MPEG-4 AAC-LC ADTS without CRC/multiple raw blocks is enabled");
                const unsigned sampling=(b[2]>>2)&15,channels=unsigned((b[2]&1)<<2|(b[3]>>6));
                const unsigned rates[]={96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};
                CodecCheck(sampling>=3 && sampling<=5 && (channels==1 || channels==2),"Only AAC 32/44.1/48 kHz mono/stereo is enabled");
                if(!out.config.rate){out.config.rate=rates[sampling];out.config.channels=channels;out.config.samplingIndex=sampling;}
                CodecCheck(out.config.rate==rates[sampling] && out.config.channels==channels,"AAC configuration changed","ts-frame-config");
                if(previous)CodecCheck(previous->rate==out.config.rate && previous->channels==channels,"AAC configuration changed across segments","ts-frame-config");
                const auto length=size_t((b[3]&3)<<11|b[4]<<3|b[5]>>5);CodecCheck(length>7 && length<=bytes.size()-at,"Truncated/empty ADTS frame","ts-codec-truncated");
                AccessUnit unit;unit.encoded=Subranges(pes,at,length);unit.payload=Subranges(pes,at+7,length-7);
                unit.sample.start=AddTime(Clock(pes.pts,origin),int64_t(samples*10000000/out.config.rate));unit.sample.decode=unit.sample.start;
                samples+=1024;unit.sample.end=AddTime(Clock(pes.pts,origin),int64_t(samples*10000000/out.config.rate));unit.sample.sync=true;append(std::move(unit));at+=length;
            }
        }
    }
    CodecCheck(!out.units.empty(),"No indexed access units");
    std::vector<const AccessUnit*> presentation;presentation.reserve(out.units.size());for(const auto& unit:out.units)presentation.push_back(&unit);
    std::sort(presentation.begin(),presentation.end(),[](const AccessUnit* a,const AccessUnit* b){return a->sample.start<b->sample.start;});
    for(size_t i=1;i<presentation.size();++i) {
        CodecCheck(Close(presentation[i]->sample.start,presentation[i-1]->sample.end),"Frame presentation cadence has a gap/overlap","ts-frame-timestamp");
        CodecCheck(Close(presentation[i]->sample.start,AddTime(presentation.front()->sample.start,Cadence(out.config,i))),"Frame presentation cadence accumulated drift","ts-frame-timestamp");
    }
    return out;
}
inline AccessSegment AccessUnits(const Program& program,Data media,uint64_t origin=0,const CodecConfig* previous=nullptr) {
    const auto transport=Media(program,media);
    return AccessPes(program,transport,[&](const Pes& pes){
        std::vector<uint8_t> bytes;bytes.reserve(pes.size);
        for(const auto& range:pes.payload){CodecCheck(range.offset<=media.size && range.size<=media.size-range.offset,"PES range exceeds media");bytes.insert(bytes.end(),media.bytes+range.offset,media.bytes+range.offset+range.size);}
        return bytes;
    },origin,previous);
}
class AccessIndex {
    Program program;uint64_t origin=0,nextOffset=0;size_t ranges=0;
    std::shared_ptr<const CodecConfig> config;
    std::vector<AccessUnit> units;
    int64_t start=0,decodeStart=0,end=0,decodeEnd=0;
public:
    AccessIndex(const Program& p,size_t initBytes,uint64_t origin90k=0):program(p),origin(origin90k),nextOffset(initBytes) {CodecCheck(initBytes>0 && initBytes<=MaxInitBytes && initBytes%PacketBytes==0 && origin<(uint64_t(1)<<33),"Invalid TS initialization/origin");}
    void append(Data media,int64_t expectedStart,int64_t expectedDuration) {
        CodecCheck(expectedDuration>0 && expectedStart<=INT64_MAX-expectedDuration,"Invalid declared TS envelope");
        auto next=AccessUnits(program,media,origin,config.get());
        CodecCheck(Close(next.start,expectedStart) && Close(next.end,expectedStart+expectedDuration),"Declared TS envelope differs from access units","ts-frame-envelope");
        CodecCheck(media.size<=uint64_t(INT64_MAX)-nextOffset && nextOffset<=uint64_t(SIZE_MAX)-media.size,"TS virtual stream overflow","ts-limit");
        accept(std::move(next),nextOffset+media.size,nextOffset);
    }
    void appendParsed(AccessSegment next,uint64_t parsedPrefix) {CodecCheck(!next.units.empty(),"No streaming access units");accept(std::move(next),parsedPrefix,0);}
private:
    void accept(AccessSegment next,uint64_t position,uint64_t offset) {
        CodecCheck(units.empty() || (Close(next.start,end) && Close(next.decodeStart,decodeEnd)),"TS segment clock gap/overlap","ts-frame-timestamp");
        CodecCheck(next.units.size()<=MaxIndexUnits-units.size() && next.ranges<=MaxIndexRanges-ranges,"TS index exceeds its bound","ts-limit");
        const auto presentationOrigin=units.empty()?next.start:start,decodeOrigin=units.empty()?next.decodeStart:decodeStart;
        std::vector<const AccessUnit*> presentation;presentation.reserve(next.units.size());
        for(size_t i=0;i<next.units.size();++i) {
            CodecCheck(Close(next.units[i].sample.decode,AddTime(decodeOrigin,Cadence(next.config,units.size()+i))),"Cross-segment decode cadence accumulated drift","ts-frame-timestamp");
            presentation.push_back(&next.units[i]);
        }
        std::sort(presentation.begin(),presentation.end(),[](const AccessUnit* a,const AccessUnit* b){return a->sample.start<b->sample.start;});
        for(size_t i=0;i<presentation.size();++i)CodecCheck(Close(presentation[i]->sample.start,AddTime(presentationOrigin,Cadence(next.config,units.size()+i))),"Cross-segment presentation cadence accumulated drift","ts-frame-timestamp");
        CodecCheck(position>=nextOffset && position<=uint64_t(INT64_MAX) && position<=uint64_t(SIZE_MAX),"TS virtual stream overflow","ts-limit");
        for(auto& unit:next.units){unit.sample.offset+=offset;for(auto* list:{&unit.encoded,&unit.payload})for(auto& range:*list)range.offset+=size_t(offset);CodecCheck(RangeReady(unit.sample.offset,unit.sample.size,position),"Access unit exceeds parsed prefix");}
        auto nextConfig=std::make_shared<const CodecConfig>(std::move(next.config));
        units.insert(units.end(),std::make_move_iterator(next.units.begin()),std::make_move_iterator(next.units.end()));
        config=std::move(nextConfig);ranges+=next.ranges;nextOffset=position;start=presentationOrigin;decodeStart=decodeOrigin;end=next.end;decodeEnd=next.decodeEnd;
    }
public:
    const std::vector<AccessUnit>& samples() const {return units;}
    const CodecConfig* configuration() const {return config.get();}
    uint64_t position() const {return nextOffset;}
    size_t rangeCount() const {return ranges;}
    int64_t readyEnd() const {return end;}
    int64_t availableEnd(uint64_t prefix) const {
        int64_t ready=0;
        for(const auto& unit:units) {
            const auto& sample=unit.sample;
            if(!RangeReady(sample.offset,sample.size,prefix))return (std::max)(int64_t(0),(std::min)(ready,(std::min)(sample.start,sample.decode)));
            ready=(std::max)(ready,sample.end);
        }
        return (std::max)(int64_t(0),ready);
    }
    size_t seekSample(int64_t position) const {
        size_t found=0;
        if(program.streamType==0x1b){for(size_t i=0;i<units.size();++i)if(units[i].sample.sync && units[i].sample.start<=position)found=i;}
        else {while(found<units.size() && units[found].sample.end<=position)++found;}
        return found;
    }
};
}} // namespace harvester::ts
