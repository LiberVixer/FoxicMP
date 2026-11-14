// Explicit fixed frame clocks for bounded continuous WebM profiles.
// Container PTS remain in milliseconds; no timestamp rewriting. GPL-3.0-or-later.
#pragma once
#include <cstdint>
namespace harvester {
struct WebmContinuousClock {
    const bool ntsc;
    explicit WebmContinuousClock(bool fractional=false):ntsc(fractional){}
    bool DefaultDuration(uint64_t ns)const {
        return ntsc?(ns==33366666 || ns==33366667):ns==40000000;
    }
    bool Packet(uint64_t ms,uint64_t frame)const {
        if(frame>=50000 || ms>2000000)return false;
        if(!ntsc)return ms==frame*40;
        // Round-to-nearest at the WebM 1ms scale, including either tie rule.
        const auto actual=ms*30,expected=frame*1001;
        return actual>expected?actual-expected<=15:expected-actual<=15;
    }
    uint64_t End100ns(uint64_t frames)const {
        return ntsc?frames*10010000000ULL/30000:frames*400000;
    }
    uint64_t Frames(int64_t duration)const {
        if(duration<=0 || duration>36000000000LL)return 0;
        if(!ntsc)return duration%400000==0 && duration/400000<=50000?uint64_t(duration/400000):0;
        const auto frames=(uint64_t(duration)*30000+5005000000ULL)/10010000000ULL;
        if(!frames || frames>50000)return 0;
        const auto scaled=frames*10010000000ULL,lower=scaled/30000,upper=(scaled+29999)/30000;
        return uint64_t(duration)==lower || uint64_t(duration)==upper?frames:0;
    }
};
}
