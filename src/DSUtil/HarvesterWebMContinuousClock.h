// Explicit bounded fixed clocks. Original WebM PTS stay in milliseconds.
// Legacy constructors preserve the25fps/30000-over-1001 profiles. GPL-3.0-or-later.
#pragma once
#include <cstdint>
namespace harvester {
struct WebmContinuousClock {
    uint32_t numerator=25,denominator=1;
    bool explicitRate=false;
    bool variable=false;
    bool derivedCount=false;
    bool nominalDefault=false;
    uint64_t expectedFrames=0,expectedEnd100ns=0;
    WebmContinuousClock()=default;
    explicit WebmContinuousClock(bool ntsc):numerator(ntsc?30000:25),denominator(ntsc?1001:1){}
    WebmContinuousClock(uint32_t n,uint32_t d):numerator(n),denominator(d),explicitRate(true){}
    static WebmContinuousClock Vfr(uint64_t frames,uint64_t end){WebmContinuousClock c;c.variable=true;c.expectedFrames=frames;c.expectedEnd100ns=end;return c;}
    static WebmContinuousClock VfrAutoCount(uint64_t end){auto c=Vfr(0,end);c.derivedCount=true;return c;}
    static WebmContinuousClock VfrNominal(uint64_t end){auto c=VfrAutoCount(end);c.nominalDefault=true;return c;}
    bool Valid()const {
        if(nominalDefault && (!variable || !derivedCount))return false;
        if(variable)return !explicitRate && (derivedCount?expectedFrames==0:expectedFrames>0 && expectedFrames<=50000) && expectedEnd100ns>0 && expectedEnd100ns<=36000000000ULL && expectedEnd100ns%10000==0;
        if(derivedCount)return false;
        return (denominator==1 && (numerator==24 || numerator==25 || numerator==30 || numerator==50 || numerator==60))
            || (denominator==1001 && (numerator==24000 || numerator==30000 || numerator==60000));
    }
    bool DefaultDuration(uint64_t ns)const {
        if(!Valid())return false;
        if(nominalDefault)return ns>=1000000 && ns<=1000000000;
        if(variable)return ns==0 || (ns>=100 && ns<=1000000000);
        const auto scaled=uint64_t(1000000000)*denominator;
        const auto lower=scaled/numerator,upper=(scaled+numerator-1)/numerator;
        // FFmpeg may truncate an approximate double one nanosecond below the
        // exact rational floor. Packet clocks still have to match every frame.
        return ns==lower || ns==upper || (explicitRate && ns+1==lower);
    }
    bool Packet(uint64_t ms,uint64_t frame)const {
        if(variable)return false; // VFR admission needs explicit duration, not a nominal grid.
        if(!Valid() || frame>=50000 || ms>(explicitRate?3600000u:2000000u))return false;
        const auto actual=ms*numerator,expected=frame*uint64_t(denominator)*1000;
        return actual>expected?actual-expected<=numerator/2:expected-actual<=numerator/2;
    }
    uint64_t End100ns(uint64_t frames)const {
        if(variable)return 0; // Cache coverage comes from validated sample intervals.
        return Valid() && frames<=50000?frames*uint64_t(denominator)*10000000/numerator:0;
    }
    uint64_t Frames(int64_t duration)const {
        if(variable)return Valid() && duration>0 && uint64_t(duration)==expectedEnd100ns?expectedFrames:0;
        if(!Valid() || duration<=0 || duration>36000000000LL)return 0;
        const auto divisor=uint64_t(denominator)*10000000;
        const auto frames=(uint64_t(duration)*numerator+divisor/2)/divisor;
        if(!frames || frames>50000)return 0;
        const auto scaled=frames*divisor,lower=scaled/numerator,upper=(scaled+numerator-1)/numerator;
        return uint64_t(duration)==lower || uint64_t(duration)==upper?frames:0;
    }
};
}
