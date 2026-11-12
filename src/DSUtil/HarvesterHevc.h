// Bounded HEVC initialization/packet framing shared by managed media paths.
// NAL decoding remains in the existing decoder. GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace harvester {
using HevcParameterSets=std::vector<std::vector<uint8_t>>;
template<class Check>
void HEVCConfiguration(const uint8_t* b,size_t size,bool completeArrays,Check check,HevcParameterSets* saved=nullptr) {
    check(b && size>=23 && b[0]==1,"Truncated/invalid HEVC decoder configuration");
    const unsigned profile=b[1]&31;
    check((b[1]>>6)==0 && (profile==1 || profile==2),"Only HEVC Main/Main10 is enabled");
    check((b[13]&0xf0)==0xf0 && (b[15]&0xfc)==0xfc && (b[16]&0xfc)==0xfc
          && (b[17]&0xf8)==0xf8 && (b[18]&0xf8)==0xf8,"Invalid HEVC configuration reserved bits");
    const unsigned luma=b[17]&7,chroma=b[18]&7;
    check((b[16]&3)==1 && luma==chroma && (luma==0 || (profile==2 && luma==2)),"Only 8/10-bit HEVC 4:2:0 is enabled");
    check((b[21]&3)==3,"Only four-byte HEVC NAL lengths are enabled");
    check(b[22]>0 && b[22]<=64,"Invalid HEVC NAL array count");
    size_t at=23;bool parameterSets[3]={false,false,false};HevcParameterSets pending;
    for(unsigned array=0;array<b[22];++array) {
        check(at<=size && size-at>=3,"Truncated HEVC NAL array");
        const auto flags=b[at++];const unsigned type=flags&63;
        check(!(flags&64) && (type==32 || type==33 || type==34 || type==39 || type==40),"Unsupported HEVC NAL array type");
        const auto count=(unsigned(b[at])<<8)|b[at+1];at+=2;
        check(count>0 && count<=1024,"Invalid HEVC parameter-set count");
        if(type>=32 && type<=34) {
            check(!parameterSets[type-32],"Duplicate HEVC parameter-set array");
            check(!completeArrays || (flags&128),"hvc1 requires complete parameter-set arrays");
            parameterSets[type-32]=true;
        }
        for(unsigned unit=0;unit<count;++unit) {
            check(at<=size && size-at>=2,"Truncated HEVC NAL length");
            const auto length=(unsigned(b[at])<<8)|b[at+1];at+=2;
            check(length>=2 && length<=size-at,"HEVC NAL exceeds configuration");
            check(!(b[at]&128) && ((b[at]>>1)&63)==type && !(b[at]&1)
                  && !(b[at+1]&0xf8) && (b[at+1]&7),"Invalid/multilayer HEVC NAL header");
            if(saved && type>=32 && type<=34)pending.emplace_back(b+at,b+at+length);
            at+=length;
        }
    }
    check(at==size && parameterSets[0] && parameterSets[1] && parameterSets[2],"HEVC requires complete VPS/SPS/PPS initialization");
    if(saved)*saved=std::move(pending);
}
// Parameter sets embedded in hev1 packets may repeat initialization, but may
// not silently reconfigure the decoder behind an immutable public profile.
template<class Check>
void HEVCPacket(const uint8_t* bytes,size_t size,const HevcParameterSets& parameters,Check check,bool initial=false) {
    size_t at=0;unsigned units=0;bool picture=false;
    while(at<size) {
        check(size-at>=4 && ++units<=65536,"unsupported-stream-hevc-packet");
        const uint32_t length=uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];at+=4;
        check(length>=2 && length<=size-at,"unsupported-stream-hevc-packet");
        const auto* nal=bytes+at;const unsigned type=(nal[0]>>1)&63;
        check(!(nal[0]&129) && !(nal[1]&0xf8) && (nal[1]&7),"unsupported-stream-hevc-packet");
        check(type<=9 || (type>=16 && type<=21) || (type>=32 && type<=40),"unsupported-stream-hevc-packet");
        if(type<=21 && initial)check(type>=16,"unsupported-stream-hevc-packet");
        picture|=type<=21;
        if(type>=32 && type<=34) {
            bool same=false;
            for(const auto& original:parameters)if(original.size()==length && !std::memcmp(original.data(),nal,length)){same=true;break;}
            check(same,"unsupported-stream-configuration-change");
        }
        at+=length;
    }
    check(picture,"unsupported-stream-hevc-packet");
}
}
