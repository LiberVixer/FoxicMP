// Explicit decoder-sample window. GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

namespace harvester { namespace pcm {
struct Slice {
    uint64_t skip=0, count=0;
    int64_t start=0;
};
class Window {
    uint64_t begin=0, end=0, cursor=0;
    uint32_t rate=0;
public:
    bool Configure(uint64_t first, uint64_t stop, uint32_t sampleRate, uint64_t decodeFirst) {
        // A caller must supply the exact first decoded sample after preroll.
        // Keep ticks representable; reject an invalid replacement atomically.
        if ((sampleRate!=44100 && sampleRate!=48000) || first>=stop || decodeFirst>first ||
            stop>uint64_t((std::numeric_limits<int64_t>::max)()/10000000)) return false;
        begin=first;end=stop;rate=sampleRate;cursor=decodeFirst;return true;
    }
    bool Active()const{return rate!=0;}
    bool Complete()const{return Active() && cursor>=end;}
    uint32_t Rate()const{return rate;}
    void Clear(){*this=Window();}
    bool Take(uint64_t samples,uint32_t sampleRate,Slice& out) {
        if (!Active() || sampleRate!=rate || samples>(std::numeric_limits<uint64_t>::max)()-cursor) return false;
        const auto next=cursor+samples,first=(std::max)(cursor,begin),last=(std::min)(next,end);
        out={};
        if(first<last){out.skip=first-cursor;out.count=last-first;out.start=int64_t((first-begin)*10000000/rate);}
        cursor=next;return true;
    }
};
}}
