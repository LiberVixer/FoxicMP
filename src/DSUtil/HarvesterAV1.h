// Shared bounded AV1 configuration framing for MP4 and WebM. GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>
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
// Copy-remux admission for the explicit continuous AV1 video profile. Existing
// native MP4/WebM paths retain their own admission and decoder validation.
template<class Check>
inline void AV1ContinuousPacket(const uint8_t* bytes,size_t size,const std::vector<uint8_t>& sequence,bool initial,Check check) {
    check(bytes && size && !sequence.empty(),"unsupported-stream-av1-packet");
    bool frame=false,seenSequence=false;unsigned units=0;
    for(size_t at=0;at<size;) {
        check(++units<=128,"unsupported-stream-av1-packet");
        const auto header=bytes[at++];const unsigned type=(header>>3)&15;
        check(!(header&0x81) && (header&2),"unsupported-stream-av1-packet");
        check(type==1 || type==2 || type==5 || type==6 || type==15,"unsupported-stream-av1-packet");
        if(header&4)check(at<size && bytes[at++]==0,"unsupported-stream-av1-packet");
        uint64_t length=0;bool ended=false;
        for(unsigned i=0;i<8;++i){
            check(at<size,"unsupported-stream-av1-packet");const auto part=bytes[at++];
            length|=uint64_t(part&127)<<(i*7);if(!(part&128)){ended=true;break;}
        }
        check(ended && length<=size-at,"unsupported-stream-av1-packet");
        if(type==1){
            check(!seenSequence && !frame && length==sequence.size() && !std::memcmp(bytes+at,sequence.data(),sequence.size()),"unsupported-stream-configuration-change");
            seenSequence=true;
        }else if(type==6){
            check(!frame && length>0,"unsupported-stream-av1-packet");
            // This profile has one displayed frame OBU per MP4 sample. The
            // first frame is independently keyed; later frames may reference it.
            const bool reduced=(sequence[0]&8)!=0;
            if(!reduced){
                check(!(bytes[at]&128),"unsupported-stream-av1-frame"); // show_existing_frame
                check((bytes[at]&16)!=0,"unsupported-stream-av1-frame"); // show_frame
                if(initial)check((bytes[at]&0xe0)==0,"unsupported-stream-av1-frame");
            }
            frame=true;
        }else if(type==2)check(length==0 && !seenSequence && !frame,"unsupported-stream-av1-packet");
        else check(length>0,"unsupported-stream-av1-packet");
        at+=size_t(length);
    }
    check(frame && (!initial || seenSequence),"unsupported-stream-av1-packet");
}
} // namespace harvester
