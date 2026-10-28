// Shared bounded AV1 configuration framing for MP4 and WebM. GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace harvester {
template<class Check>
inline std::vector<uint8_t> AV1Configuration(const uint8_t* bytes,size_t length,Check check) {
        // AV1CodecConfigurationRecord + bounded low-overhead config OBU framing.
        // Sequence/frame decoding belongs to libmpv/DirectShow, not this validator.
        check(length>=4 && bytes[0]==0x81,"Invalid AV1 configuration marker/version");
        const auto* b=bytes;
        check((b[1]>>5)==0 && (b[1]&31)<=23,"Only AV1 Main is enabled");
        check(!(b[2]&0xb0) && (b[2]&12)==12 && (b[2]&3)!=3,"Only Main-tier 8/10-bit AV1 4:2:0 is enabled");
        check(b[3]==0,"AV1 presentation-delay configuration is unsupported or reserved bits are invalid");
        size_t at=4;bool sequence=false;unsigned units=0;std::vector<uint8_t> sequenceBytes;
        while(at<length) {
            check(++units<=128,"Too many AV1 configuration OBUs");
            const auto header=b[at++];const unsigned type=(header>>3)&15;
            check(!(header&0x81) && (header&2),"Invalid AV1 configuration OBU header/size flag");
            check(type==1 || type==5,"Only AV1 sequence/metadata configuration OBUs are enabled");
            if(header&4) {check(at<length && b[at++]==0,"Unsupported AV1 configuration OBU layer or reserved bits");}
            uint64_t size=0;bool ended=false;
            for(unsigned i=0;i<8;++i) {
                check(at<length,"Truncated AV1 OBU size");const auto part=b[at++];
                size|=uint64_t(part&127)<<(i*7);
                if(!(part&128)){ended=true;break;}
            }
            check(ended && size>0 && size<=length-at,"Invalid AV1 OBU payload length");
            if(type==1) {
                check(!sequence && units==1,"AV1 requires one initial Sequence Header OBU");
                check((b[at]>>5)==(b[1]>>5),"AV1 sequence/configuration profile mismatch");
                sequence=true;sequenceBytes.assign(b+at,b+at+size_t(size));
            }else check(sequence,"AV1 sequence header must precede metadata");
            at+=size_t(size);
        }
        check(sequence,"This AV1 profile requires a Sequence Header OBU in av1C");
    return sequenceBytes;
}
} // namespace harvester
