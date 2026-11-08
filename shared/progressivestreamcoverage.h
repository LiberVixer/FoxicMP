// Indexed presentation coverage, not a decoder/seek admission guarantee.
// GPL-3.0-or-later.
#pragma once
#include <iterator>
#include <map>
#include "DSUtil/HarvesterState.h"

namespace foxicmp { namespace progressive {
// Feed only samples from fully validated, published fragments. Presentation
// order may differ from decode order: a future interval cannot bridge a hole.
// Each sample is processed once; status queries never scan an index or do I/O.
class StreamIndexedCoverage {
    size_t seen=0;
    int64_t prefix=0;
    std::map<int64_t,int64_t> pending;
    void include(int64_t begin,int64_t end){
        begin=(std::max)(int64_t(0),begin);
        if(end<=begin || end<=prefix)return;
        auto i=pending.lower_bound(begin);
        if(i!=pending.begin()){
            auto previous=std::prev(i);
            if(previous->second>=begin){begin=previous->first;end=(std::max)(end,previous->second);i=pending.erase(previous);}
        }
        while(i!=pending.end() && i->first<=end){end=(std::max)(end,i->second);i=pending.erase(i);}
        if(begin<=prefix)prefix=end;
        else pending.emplace(begin,end);
    }
public:
    void update(const std::vector<harvester::Sample>& samples){
        if(samples.size()<seen)throw std::logic_error("Indexed coverage cannot discard samples");
        for(;seen<samples.size();++seen)include(samples[seen].start,samples[seen].end);
    }
    int64_t end100ns()const{return prefix;}
};
}}
