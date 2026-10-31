// Incremental transport parsing for a single confirmed-prefix TS file.
// GPL-3.0-or-later. Completed PES retain absolute offsets in the source file.
#pragma once
#include "HarvesterTSCodec.h"
#include <optional>

namespace harvester { namespace ts {
class GrowingTransport {
    Tables tables{true};Sections pat,pmt,sdt{0xf0};Continuity continuity;
    std::array<Program,2> programs;
    std::array<std::optional<PesBuilder>,2> builders;
    std::array<size_t,2> pesCount{};
    std::array<uint8_t,PacketBytes> partial{};size_t pending=0;
    uint64_t consumed=0,pcr=0;size_t pcrCount=0;
    bool initialized=false,finished=false;
    uint64_t initializationEnd=0;
    void setProgram() {
        programs[0]=tables.program;
        if(programs[0].streamType!=0x1b){std::swap(programs[0].streamType,programs[0].otherType);std::swap(programs[0].streamPid,programs[0].otherPid);}
        programs[1]=programs[0];std::swap(programs[1].streamType,programs[1].otherType);std::swap(programs[1].streamPid,programs[1].otherPid);
        for(size_t i=0;i<2;++i)builders[i].emplace(programs[i]);
        tables=Tables(programs[0]);initialized=true;initializationEnd=consumed;
    }
    using Emit=std::function<void(unsigned,Pes)>;
    void flush(unsigned kind,const Emit& onPes) {
        Segment out;builders[kind]->finish(out);
        for(auto& unit:out.packets){Check(++pesCount[kind]<=MaxIndexUnits,"Growing PES count exceeds bound","ts-limit");onPes(kind,std::move(unit));}
    }
    void packet(const Emit& onPes) {
        Check(initialized || consumed<=MaxInitBytes,"Growing TS initialization exceeds bound","ts-limit");
        const auto value=Transport({partial.data(),partial.size()});continuity.append(value);
        if(value.pid==8191)return;
        Check(value.pid==0 || value.pid==17 || (tables.program.pmtPid && value.pid==tables.program.pmtPid) || (initialized && (value.pid==programs[0].streamPid || value.pid==programs[1].streamPid)),"Undeclared growing TS PID");
        if(value.hasPcr){Check(initialized && value.pid==programs[0].pcrPid,"PCR before program or on wrong PID");Check(!pcrCount || value.pcr>=pcr,"Growing PCR regressed or rolled over","ts-timestamp");pcr=value.pcr;++pcrCount;}
        if(!value.payload)return;
        if(value.pid==0)pat.append(value.data,value.start,[&](Data d){tables.pat(d);});
        else if(value.pid==tables.program.pmtPid)pmt.append(value.data,value.start,[&](Data d){tables.pmt(d);if(!initialized)setProgram();});
        else if(value.pid==17)sdt.append(value.data,value.start,[](Data d){Check(d.size>=15 && (d.bytes[0]==0x42 || d.bytes[0]==0x46) && (d.bytes[5]&0xc1)==0xc1 && d.bytes[6]<=d.bytes[7],"Unsupported SDT envelope");});
        else {
            Check(initialized,"Elementary data before PAT/PMT");const auto kind=value.pid==programs[0].streamPid?0u:1u;auto& builder=*builders[kind];
            if(value.start)flush(kind,onPes);else Check(!builder.empty(),"Growing media begins inside a PES","ts-truncated");
            builder.append(value,size_t(consumed-PacketBytes));if(builder.complete())flush(kind,onPes);
        }
        Check(initialized || consumed<=MaxInitBytes,"Growing TS initialization exceeds bound","ts-limit");
    }
public:
    void append(Data bytes,const Emit& onPes) {
        Check(!finished && bytes.size<=65536 && bytes.size<=uint64_t(SIZE_MAX)-consumed-pending,"Invalid growing parser append","ts-limit");
        for(size_t at=0;at<bytes.size;) {
            const auto count=(std::min)(bytes.size-at,PacketBytes-pending);std::memcpy(partial.data()+pending,bytes.bytes+at,count);pending+=count;at+=count;
            if(pending==PacketBytes){consumed+=PacketBytes;pending=0;packet(onPes);}
        }
    }
    void finish(const Emit& onPes) {
        Check(!finished,"Growing parser already completed");Check(!pending,"Completed TS ends inside a packet","ts-truncated");
        pat.finish();pmt.finish();sdt.finish();tables.finish();Check(initialized && pcrCount,"Completed TS lacks program/PCR","ts-truncated");
        for(unsigned i=0;i<2;++i)flush(i,onPes);finished=true;
    }
    bool ready() const {return initialized;}
    bool hasClock() const {return pcrCount!=0;}
    bool complete() const {return finished;}
    uint64_t initEnd() const {return initializationEnd;}
    uint64_t parsedBytes() const {return consumed;}
    size_t pendingPacketBytes() const {return pending;}
    const Program& program(unsigned kind) const {Check(initialized && kind<2,"Growing program is not ready");return programs[kind];}
};
}} // namespace harvester::ts
