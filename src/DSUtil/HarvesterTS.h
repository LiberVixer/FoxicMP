// Bounded transport-layer staging for segmented MPEG-TS. GPL-3.0-or-later.
// Internal preparation only: no decoder, player capability or IPC activation.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace harvester { namespace ts {
constexpr size_t PacketBytes=188,MaxInitBytes=1024*1024,MaxSegmentBytes=16*1024*1024;
constexpr size_t MaxPesBytes=4*1024*1024,MaxPesPackets=65536;
struct Error:std::runtime_error {
    std::string code;
    Error(const char* c,const char* why):std::runtime_error(why),code(c){}
};
inline void Check(bool ok,const char* why,const char* code="unsupported-ts") {if(!ok)throw Error(code,why);}
struct Data {const uint8_t* bytes=nullptr;size_t size=0;};
struct Range {size_t offset=0,size=0;};
struct Pes {
    size_t transportOffset=0,transportEnd=0,size=0;
    uint64_t pts=0,dts=0;
    std::vector<Range> payload;
};
struct Program {
    uint16_t number=0,pmtPid=0,streamPid=0,pcrPid=0;
    uint8_t streamType=0;
    std::vector<uint8_t> pat,pmt;
};
struct Segment {std::vector<Pes> packets;uint64_t firstPcr=0,lastPcr=0;size_t pcrCount=0;};
inline uint16_t U16(const uint8_t* p){return uint16_t(uint16_t(p[0])<<8|p[1]);}
inline uint32_t Crc(Data d) {
    uint32_t crc=0xffffffff;
    for(size_t i=0;i<d.size;++i){crc^=uint32_t(d.bytes[i])<<24;for(unsigned n=0;n<8;++n)crc=(crc<<1)^((crc&0x80000000)?0x04c11db7:0);}
    return crc;
}
inline void Stuffing(Data d) {for(size_t i=0;i<d.size;++i)Check(d.bytes[i]==0xff,"Invalid TS stuffing");}
inline void Descriptors(Data d) {
    size_t count=0;
    for(size_t at=0;at<d.size;) {
        Check(d.size-at>=2 && count++<128,"Invalid/excessive TS descriptors");
        const auto tag=d.bytes[at],length=d.bytes[at+1];at+=2;
        Check(length<=d.size-at && tag!=9,"Truncated or conditional-access TS descriptor");at+=length;
    }
}
// One section may cross packets; a packet may finish a section then start others.
class Sections {
    std::vector<uint8_t> pending;
    size_t expected=0;
    using Accept=std::function<void(Data)>;
    void feed(Data data,bool allowStart,const Accept& accept) {
        for(size_t at=0;at<data.size;) {
            if(pending.empty()) {
                if(data.bytes[at]==0xff){Stuffing({data.bytes+at,data.size-at});return;}
                Check(allowStart,"PSI continuation has no preceding section");
            }
            const auto count=(std::min)(data.size-at,(expected?expected:size_t(3))-pending.size());
            pending.insert(pending.end(),data.bytes+at,data.bytes+at+count);at+=count;
            if(!expected && pending.size()==3) {
                Check((pending[1]&0xf0)==0xb0,"Unsupported PSI section syntax");
                expected=3+size_t((pending[1]&15)<<8|pending[2]);
                Check(expected>=12 && expected<=1024,"PSI section exceeds its bound","ts-limit");
            }
            if(expected && pending.size()==expected) {
                Check(Crc({pending.data(),pending.size()})==0,"PSI CRC mismatch","ts-crc");
                accept({pending.data(),pending.size()});pending.clear();expected=0;
                // Non-start packets may only finish their existing section.
                if(!allowStart && at<data.size){Stuffing({data.bytes+at,data.size-at});return;}
            }
        }
    }
public:
    void append(Data data,bool start,const Accept& accept) {
        if(start) {
            Check(data.size>0,"Missing PSI pointer field");const auto pointer=data.bytes[0];
            Check(size_t(pointer)<data.size,"Invalid PSI pointer field");
            if(pending.empty())Stuffing({data.bytes+1,pointer});
            else {feed({data.bytes+1,pointer},false,accept);Check(pending.empty(),"PSI pointer did not finish its section");}
            feed({data.bytes+1+pointer,data.size-1-pointer},true,accept);
        }else feed(data,false,accept);
    }
    void finish() const {Check(pending.empty(),"Incomplete final PSI section","ts-truncated");}
};
class Tables {
    bool media=false;
    size_t pats=0,pmts=0;
    void common(Data d,uint8_t id) const {
        Check(d.size>=12 && d.bytes[0]==id && (d.bytes[5]&0xc1)==0xc1 && d.bytes[6]==0 && d.bytes[7]==0,
            "Only current single-section PAT/PMT is supported");
    }
public:
    Program program;
    Tables()=default;
    explicit Tables(const Program& p):media(true),program(p){}
    void pat(Data d) {
        common(d,0);++pats;
        if(media){Check(std::vector<uint8_t>(d.bytes,d.bytes+d.size)==program.pat,"PAT changed within the session","ts-config-change");return;}
        Check(pats==1 && d.size>=16 && (d.size-12)%4==0,"Initialization needs one PAT");
        unsigned programs=0;
        for(size_t at=8;at<d.size-4;at+=4) {
            const auto number=U16(d.bytes+at);Check((d.bytes[at+2]&0xe0)==0xe0,"Invalid PAT PID reserved bits");
            // This initial bounded profile also excludes a separate network PID.
            Check(number!=0 && ++programs==1,"Only one TS program without NIT is supported");
            program.number=number;program.pmtPid=U16(d.bytes+at+2)&8191;
            Check(program.pmtPid>=32 && program.pmtPid<8191,"Invalid PMT PID");
        }
        Check(programs==1,"PAT has no program");program.pat.assign(d.bytes,d.bytes+d.size);
    }
    void pmt(Data d) {
        common(d,2);++pmts;
        if(media){Check(std::vector<uint8_t>(d.bytes,d.bytes+d.size)==program.pmt,"PMT changed within the session","ts-config-change");return;}
        Check(!program.pat.empty() && pmts==1 && U16(d.bytes+3)==program.number && d.size>=21,"Invalid/misplaced PMT");
        Check((d.bytes[8]&0xe0)==0xe0 && (d.bytes[10]&0xf0)==0xf0,"Invalid PMT reserved bits");
        program.pcrPid=U16(d.bytes+8)&8191;
        size_t at=12;const auto info=size_t(U16(d.bytes+10)&4095);
        Check(info<=d.size-4-at,"Truncated program descriptors");Descriptors({d.bytes+at,info});at+=info;
        unsigned streams=0;
        while(at<d.size-4) {
            Check(d.size-4-at>=5 && ++streams==1,"Only one elementary stream per TS resource is enabled");
            Check((d.bytes[at+1]&0xe0)==0xe0 && (d.bytes[at+3]&0xf0)==0xf0,"Invalid elementary stream reserved bits");
            program.streamType=d.bytes[at];program.streamPid=U16(d.bytes+at+1)&8191;
            Check(program.streamType==0x1b || program.streamType==0x0f,"Only AVC or ADTS AAC stream types are staged");
            Check(program.streamPid>=32 && program.streamPid<8191 && program.streamPid!=program.pmtPid,"Invalid elementary PID");
            const auto length=size_t(U16(d.bytes+at+3)&4095);at+=5;
            Check(length<=d.size-4-at,"Truncated stream descriptors");Descriptors({d.bytes+at,length});at+=length;
        }
        Check(streams==1 && program.pcrPid==program.streamPid,"PCR must use the single elementary PID");
        program.pmt.assign(d.bytes,d.bytes+d.size);
    }
    void finish() const {Check(!program.pat.empty() && !program.pmt.empty() && (media || (pats==1 && pmts==1)),"Missing initialization PAT/PMT","ts-truncated");}
};
struct Packet {
    uint16_t pid=0;uint8_t counter=0;bool start=false,payload=false,hasPcr=false;
    uint64_t pcr=0;Data data;
};
inline Packet Transport(Data d) {
    Check(d.size==188 && d.bytes[0]==0x47,"Expected whole 188-byte TS packets","ts-truncated");
    const auto* b=d.bytes;
    Check(!(b[1]&128) && !(b[3]&192),"Errored or scrambled TS packet");
    Packet p;p.pid=U16(b+1)&8191;p.start=(b[1]&64)!=0;p.counter=b[3]&15;
    const auto control=(b[3]>>4)&3;Check(control!=0,"Reserved TS adaptation control");p.payload=(control&1)!=0;
    size_t at=4;
    if(control&2) {
        const auto length=size_t(b[at++]);Check(length<=188-at && (p.payload?length<=182:length==183),"Invalid adaptation field size");
        const auto end=at+length;
        if(length) {
            const auto flags=b[at++];Check(!(flags&0x8f),"TS discontinuity or unsupported adaptation extension");
            if(flags&16) {
                Check(end-at>=6 && (b[at+4]&126)==126,"Truncated/invalid PCR");
                const auto base=uint64_t(b[at])<<25|uint64_t(b[at+1])<<17|uint64_t(b[at+2])<<9|uint64_t(b[at+3])<<1|uint64_t(b[at+4]>>7);
                const auto extension=unsigned(b[at+4]&1)<<8|b[at+5];Check(extension<300,"Invalid PCR extension");
                p.hasPcr=true;p.pcr=base*300+extension;at+=6;
            }
            Stuffing({b+at,end-at});
        }
        at=end;
    }
    Check(p.payload?at<188:at==188,"Empty/malformed TS payload");
    Check(!p.start || p.payload,"Payload start without payload");p.data={b+at,p.payload?188-at:0};return p;
}
class Continuity {
    struct Last {bool payload=false;uint8_t counter=0;};
    std::map<uint16_t,Last> last;
public:
    void append(const Packet& p) {
        if(p.pid==8191)return;
        auto& before=last[p.pid];
        if(before.payload)Check(p.counter==((before.counter+(p.payload?1:0))&15),"TS continuity counter gap/duplicate","ts-continuity");
        if(p.payload){before.payload=true;before.counter=p.counter;}
    }
};
inline uint64_t Timestamp(const uint8_t* b,unsigned prefix) {
    Check((b[0]>>4)==prefix && (b[0]&1) && (b[2]&1) && (b[4]&1),"Invalid PTS/DTS marker bits");
    return uint64_t((b[0]>>1)&7)<<30|uint64_t(b[1])<<22|uint64_t(b[2]>>1)<<15|uint64_t(b[3])<<7|uint64_t(b[4]>>1);
}
class PesBuilder {
    const Program& program;
    std::vector<uint8_t> bytes;
    std::vector<Range> ranges;
    size_t begin=0,end=0,expected=0;
    int streamId=-1;
public:
    explicit PesBuilder(const Program& p):program(p){}
    bool empty() const {return bytes.empty();}
    void append(const Packet& packet,size_t at) {
        if(bytes.empty())begin=at;
        Check(packet.data.size<=MaxPesBytes-bytes.size(),"PES exceeds its staging bound","ts-limit");
        // Offsets are relative to the media resource, including TS headers.
        ranges.push_back({at+188-packet.data.size,packet.data.size});
        bytes.insert(bytes.end(),packet.data.bytes,packet.data.bytes+packet.data.size);end=at+188;
        if(bytes.size()>=6) {
            Check(bytes[0]==0 && bytes[1]==0 && bytes[2]==1,"Invalid PES start code");
            const auto id=bytes[3];Check(program.streamType==0x1b?(id>=0xe0 && id<=0xef):(id>=0xc0 && id<=0xdf),"PES stream ID does not match PMT");
            Check(streamId<0 || id==streamId,"PES stream ID changed","ts-config-change");streamId=id;
            const auto length=U16(bytes.data()+4);Check(length || program.streamType==0x1b,"Unbounded audio PES is unsupported");expected=length?6+size_t(length):0;
            Check(!expected || bytes.size()<=expected,"PES contains bytes beyond its declared length");
        }
    }
    void finish(Segment& out) {
        if(bytes.empty())return;
        Check(bytes.size()>=9 && (!expected || bytes.size()==expected),"Incomplete final PES packet","ts-truncated");
        Check((bytes[6]&0xf0)==0x80 && (bytes[7]==0x80 || bytes[7]==0xc0),"Missing PTS or unsupported PES optional fields");
        const auto timing=bytes[7]==0x80?size_t(5):size_t(10),header=9+size_t(bytes[8]);
        Check(bytes[8]>=timing && bytes[8]<=64 && header<bytes.size(),"Truncated/empty PES header or payload","ts-truncated");
        Pes pes;pes.pts=Timestamp(bytes.data()+9,bytes[7]==0x80?2:3);pes.dts=timing==5?pes.pts:Timestamp(bytes.data()+14,1);
        Check(pes.pts>=pes.dts,"PTS/DTS rollover requires a later playback profile");
        Check(out.packets.empty() || pes.dts>=out.packets.back().dts,"DTS regressed or rolled over","ts-timestamp");
        Stuffing({bytes.data()+9+timing,header-9-timing});
        size_t skip=header;
        for(const auto& range:ranges) {
            const auto n=(std::min)(skip,range.size);skip-=n;
            if(n<range.size){pes.payload.push_back({range.offset+n,range.size-n});pes.size+=range.size-n;}
        }
        Check(!skip && pes.size==bytes.size()-header,"Invalid elementary payload layout");
        Check(out.packets.size()<MaxPesPackets,"Too many PES packets","ts-limit");
        pes.transportOffset=begin;pes.transportEnd=end;out.packets.push_back(std::move(pes));
        bytes.clear();ranges.clear();expected=0;
    }
};
inline Program Initialization(Data bytes,int kind) {
    Check((kind==0 || kind==1) && bytes.size>0 && bytes.size<=MaxInitBytes,"Invalid TS initialization bound","ts-limit");
    Check(bytes.size%PacketBytes==0,"Partial TS initialization packet","ts-truncated");
    Tables tables;Sections pat,pmt;Continuity continuity;
    for(size_t at=0;at<bytes.size;at+=188) {
        const auto p=Transport({bytes.bytes+at,188});continuity.append(p);
        Check(!p.hasPcr,"Initialization cannot contain a media clock");
        if(p.pid==8191)continue;
        Check(p.pid==0 || (tables.program.pmtPid && p.pid==tables.program.pmtPid),"Unexpected initialization PID");
        if(!p.payload)continue;
        if(p.pid==0)pat.append(p.data,p.start,[&](Data d){tables.pat(d);});
        else pmt.append(p.data,p.start,[&](Data d){tables.pmt(d);});
    }
    pat.finish();pmt.finish();tables.finish();
    Check(tables.program.streamType==(kind?0x0f:0x1b),"TS initialization kind differs from stream type");return tables.program;
}
inline Segment Media(const Program& program,Data bytes) {
    Check(!program.pat.empty() && !program.pmt.empty(),"Missing validated TS initialization");
    Check(bytes.size>0 && bytes.size<=MaxSegmentBytes,"TS segment exceeds its staging bound","ts-limit");
    Check(bytes.size%PacketBytes==0,"Partial TS media packet","ts-truncated");
    Tables tables(program);Sections pat,pmt;Continuity continuity;PesBuilder pes(program);Segment out;
    for(size_t at=0;at<bytes.size;at+=188) {
        const auto p=Transport({bytes.bytes+at,188});continuity.append(p);
        if(p.pid==8191)continue;
        Check(p.pid==0 || p.pid==program.pmtPid || p.pid==program.streamPid,"Undeclared media PID");
        if(p.hasPcr) {
            Check(p.pid==program.pcrPid,"PCR occurs on the wrong PID");
            Check(!out.pcrCount || p.pcr>=out.lastPcr,"PCR regressed or rolled over","ts-timestamp");
            if(!out.pcrCount)out.firstPcr=p.pcr;out.lastPcr=p.pcr;++out.pcrCount;
        }
        if(!p.payload)continue;
        if(p.pid==0)pat.append(p.data,p.start,[&](Data d){tables.pat(d);});
        else if(p.pid==program.pmtPid)pmt.append(p.data,p.start,[&](Data d){tables.pmt(d);});
        else {
            Check(out.pcrCount>0,"PCR must precede media payload");
            if(p.start)pes.finish(out);else Check(!pes.empty(),"Media begins inside an incomplete PES","ts-truncated");
            pes.append(p,at);
        }
    }
    pat.finish();pmt.finish();tables.finish();pes.finish(out);
    Check(!out.packets.empty(),"TS segment has no PES packets");return out;
}
}} // namespace harvester::ts
