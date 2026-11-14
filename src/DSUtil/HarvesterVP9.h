// Bounded continuous VP9 MP4 admission. GPL-3.0-or-later.
// Native MP4/WebM admission retains its existing independent codec checks.
#pragma once
#include <cstdint>
#include <cstddef>
namespace harvester {
struct Vp9Configuration {unsigned profile=0,depth=0,chroma=0,range=0,level=0,primaries=0,transfer=0,matrix=0;};
template<class Check>
inline Vp9Configuration VP9Configuration(const uint8_t* b,size_t size,Check check){
    check(b && size==12,"unsupported-stream-vp9-configuration");
    check(b[0]==1 && b[1]==0 && b[2]==0 && b[3]==0,"unsupported-stream-vp9-configuration");
    Vp9Configuration c{b[4],unsigned(b[6]>>4),unsigned((b[6]>>1)&7),unsigned(b[6]&1),b[5],b[7],b[8],b[9]};
    check(((c.profile==0 && c.depth==8) || (c.profile==2 && c.depth==10)) && c.chroma<=1,"unsupported-stream-vp9-configuration");
    check(c.level==0 || c.level==10 || c.level==11 || c.level==20 || c.level==21 || c.level==30 || c.level==31 || c.level==40 || c.level==41 || c.level==50 || c.level==51 || c.level==52 || c.level==60 || c.level==61 || c.level==62,"unsupported-stream-vp9-configuration");
    check(c.primaries==1 || c.primaries==2 || (c.primaries>=4 && c.primaries<=12) || c.primaries==22,"unsupported-stream-vp9-configuration");
    check(c.transfer==1 || c.transfer==2 || (c.transfer>=4 && c.transfer<=18),"unsupported-stream-vp9-configuration");
    check((c.matrix==1 || c.matrix==2 || (c.matrix>=4 && c.matrix<=14)) && b[10]==0 && b[11]==0,"unsupported-stream-vp9-configuration");return c;
}
// Prefix admission only; compressed entropy is verified by the decoder/tests.
// Every accepted frame keeps the configured dimensions. The first shown keyframe
// initializes all reference slots; subsequent references therefore have that size.
template<class Check>
inline void VP9ContinuousFrame(const uint8_t* bytes,size_t size,const Vp9Configuration& config,unsigned width,unsigned height,bool initial,bool displayed,Check check){
    check(bytes && size && size<=16777216 && width && height,"unsupported-stream-vp9-packet");
    size_t bit=0;
    auto read=[&](unsigned count){check(count<=24 && bit/8<size && count<=size*8-bit,"unsupported-stream-vp9-packet");unsigned value=0;for(unsigned i=0;i<count;++i,++bit)value=(value<<1)|((bytes[bit/8]>>(7-bit%8))&1);return value;};
    check(read(2)==2,"unsupported-stream-vp9-packet");const auto low=read(1),high=read(1),profile=low|(high<<1);
    check(profile==config.profile && (profile==0 || profile==2),"unsupported-stream-vp9-configuration-change");
    check(read(1)==0,"unsupported-stream-vp9-frame"); // show_existing_frame
    const auto inter=read(1),shown=read(1),resilient=read(1);
    check(shown==displayed && (!initial || (!inter && shown)),"unsupported-stream-vp9-frame");
    auto dimensions=[&](){const auto w=read(16)+1,h=read(16)+1;check(w==width && h==height,"unsupported-stream-vp9-size-change");};
    if(!inter){
        check(shown,"unsupported-stream-vp9-frame");
        check(read(24)==0x498342,"unsupported-stream-vp9-frame");
        if(profile==2)check(read(1)==0,"unsupported-stream-vp9-configuration-change");
        check(read(3)<6,"unsupported-stream-vp9-frame");
        check(read(1)==config.range,"unsupported-stream-vp9-configuration-change");dimensions();
    }else{
        if(!shown)check(read(1)==0,"unsupported-stream-vp9-intra-only");
        if(!resilient)read(2); // reset_frame_context
        read(8);for(unsigned i=0;i<3;++i){read(3);read(1);} // reference indices/sign bias
        bool referenced=false;for(unsigned i=0;i<3;++i)if(read(1)){referenced=true;break;}
        if(!referenced)dimensions();
    }
    if(read(1))dimensions(); // render size must also remain fixed
}
template<class Check>
inline void VP9ContinuousPacket(const uint8_t* bytes,size_t size,const Vp9Configuration& config,unsigned width,unsigned height,bool initial,Check check){
    check(bytes && size && width && height,"unsupported-stream-vp9-packet");
    // A valid superframe index unambiguously denotes indexed frame storage.
    // Reject it even for one frame; do not treat random compressed tail bytes
    // as an index unless markers, sizes and total payload length all agree.
    const auto marker=bytes[size-1];
    if((marker&0xe0)==0xc0){
        const unsigned frames=(marker&7)+1,magnitude=((marker>>3)&3)+1,indexSize=2+frames*magnitude;
        if(size>=indexSize && bytes[size-indexSize]==marker){
            size_t total=0,at=size-indexSize+1;bool valid=true;
            for(unsigned f=0;f<frames;++f){uint32_t n=0;for(unsigned j=0;j<magnitude;++j)n|=uint32_t(bytes[at++])<<(8*j);
                if(!n || n>size-indexSize-total){valid=false;break;}total+=n;
            }
            check(!valid || total!=size-indexSize,"unsupported-stream-vp9-superframe");
        }
    }
    VP9ContinuousFrame(bytes,size,config,width,height,initial,true,check);
}
// Explicit indexed profile: at most seven hidden inter frames followed by exactly
// one displayed frame. No standalone hidden, show_existing, hidden key/intra-only,
// packet timestamp reorder, or dynamic coded/render size is admitted.
template<class Check>
inline void VP9ContinuousSuperframePacket(const uint8_t* bytes,size_t size,const Vp9Configuration& config,unsigned width,unsigned height,bool initial,Check check){
    check(bytes && size && size<=16777216,"unsupported-stream-vp9-packet");
    uint32_t lengths[8]{};unsigned frames=1;size_t payload=size;
    const unsigned marker=bytes[size-1];
    if((marker&0xe0)==0xc0){
        const unsigned count=(marker&7)+1,magnitude=((marker>>3)&3)+1,indexSize=2+count*magnitude;
        if(size>=indexSize && bytes[size-indexSize]==marker){
            payload=size-indexSize;size_t total=0,at=payload+1;
            for(unsigned f=0;f<count;++f){
                uint32_t length=0;for(unsigned j=0;j<magnitude;++j)length|=uint32_t(bytes[at++])<<(8*j);
                check(length && length<=payload-total,"unsupported-stream-vp9-superframe-index");
                lengths[f]=length;total+=length;
            }
            check(total==payload,"unsupported-stream-vp9-superframe-index");frames=count;
        }
    }
    if(payload==size)lengths[0]=uint32_t(size);
    check(!initial || frames==1,"unsupported-stream-vp9-frame");
    size_t at=0;for(unsigned f=0;f<frames;++f){
        VP9ContinuousFrame(bytes+at,lengths[f],config,width,height,initial,f+1==frames,check);at+=lengths[f];
    }
}

} // namespace harvester
