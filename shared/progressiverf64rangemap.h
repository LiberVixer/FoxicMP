// RF64 packet geometry only. No I/O or decoder seek authorization.
// GPL-3.0-or-later.
#pragma once
#include "progressivesession.h"
namespace foxicmp { namespace progressive {
struct Rf64PacketRange {
    uint64_t first=0,end=0,offset=0,bytes=0;
    int64_t startTicks=0,endTicks=0;
    bool prefix=false;
};
template<class Layout> std::vector<Rf64PacketRange> Rf64PacketRanges(
    const Layout& layout,const std::vector<Range>& ranges,uint64_t prefix) {
    std::vector<Rf64PacketRange> result;
    if(!layout.Initialized())return result;
    const auto& info=layout.info;
    const uint64_t start=layout.DataOffset(),end=start+info.total*info.align;
    const uint64_t packetBytes=uint64_t(Layout::PacketSamples)*info.align;
    auto emitRange=[&](uint64_t at,uint64_t stop){
        at=(std::max)(at,start);stop=(std::min)(stop,end);
        if(at>=stop)return;
        const auto first=(at-start+packetBytes-1)/packetBytes*Layout::PacketSamples;
        const auto last=stop==end?info.total:(stop-start)/packetBytes*Layout::PacketSamples;
        if(first>=last)return;
        const auto physical=start+first*info.align,bytes=(last-first)*info.align;
        const auto ticks=[&](uint64_t n){return int64_t(n/info.rate*10000000+(n%info.rate)*10000000/info.rate);};
        result.push_back({first,last,physical,bytes,ticks(first),ticks(last),physical+bytes<=prefix});
    };
    // Schema validation already bounds/sorts/non-overlaps ranges. Merge adjacent
    // records before packet rounding: a packet may cross their shared boundary.
    uint64_t at=0,stop=0;bool active=false;
    for(const auto& range:ranges){
        if(active && range.offset==stop){stop+=range.length;continue;}
        if(active)emitRange(at,stop);
        at=range.offset;stop=at+range.length;active=true;
    }
    if(active)emitRange(at,stop);
    return result;
}
}} // namespace foxicmp::progressive
