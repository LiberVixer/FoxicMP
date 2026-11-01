// Bounded fixed-block FLAC framing and lossless fMP4 view. GPL-3.0-or-later.
// RFC 9639 / Xiph isoflac: compressed frames are referenced, never re-encoded.
#pragma once
#include "HarvesterAudioProjection.h"

namespace harvester { namespace rawflac {
using Bytes=muxmp4::Bytes;
constexpr size_t MaxFrame=1048576,MaxInit=1048576,MaxFrames=250000;
inline void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Waiting {};
struct Bits {
    const Bytes& data;size_t bit=0;
    uint32_t get(unsigned n){Check(n<=32,"FLAC bit width");uint32_t v=0;for(unsigned i=0;i<n;++i){Check(bit<MaxFrame*8,"FLAC frame exceeds limit");if(bit/8>=data.size())throw Waiting{};v=(v<<1)|((data[bit/8]>>(7-bit%8))&1);++bit;}return v;}
    void skip(uint64_t n){Check(n<=MaxFrame*8 && bit<=MaxFrame*8-n,"FLAC subframe exceeds limit");if(n>data.size()*8-bit)throw Waiting{};bit+=size_t(n);}
    unsigned unary(unsigned bound){unsigned n=0;while(!get(1)){Check(n<bound,"FLAC unary bound exceeded");++n;}return n;}
};
inline uint32_t Crc(const uint8_t* p,size_t n,unsigned width,uint32_t polynomial){uint32_t c=0,mask=(uint32_t(1)<<width)-1;for(size_t i=0;i<n;++i){c^=uint32_t(p[i])<<(width-8);for(unsigned k=0;k<8;++k)c=((c<<1)^((c&(uint32_t(1)<<(width-1)))?polynomial:0))&mask;}return c;}
inline uint64_t Number(Bits& b){const unsigned first=b.get(8);if(first<128)return first;unsigned length=0;for(unsigned mask=128;first&mask;mask>>=1)++length;Check(length>=2 && length<=6,"Invalid FLAC frame number");uint64_t v=first&((1u<<(7-length))-1);for(unsigned i=1;i<length;++i){const auto c=b.get(8);Check((c&192)==128,"Invalid FLAC UTF-8 continuation");v=(v<<6)|(c&63);}const uint64_t lower[]={0,0,128,2048,65536,2097152,67108864};Check(v>=lower[length] && v<=0x7fffffff,"Noncanonical FLAC frame number");return v;}
struct Info {
    Bytes stream;unsigned rate=0,channels=0,bits=0,block=0;uint32_t minFrame=0,maxFrame=0;uint64_t total=0;
    void parse(const Bytes& bytes){Check(bytes.size()==34,"Invalid raw FLAC STREAMINFO size");stream=bytes;BoxData b{bytes.data(),bytes.size()};block=(unsigned(bytes[0])<<8)|bytes[1];Check(block>=16 && block==((unsigned(bytes[2])<<8)|bytes[3]),"Only fixed equal FLAC block bounds are enabled");minFrame=(unsigned(bytes[4])<<16)|(unsigned(bytes[5])<<8)|bytes[6];maxFrame=(unsigned(bytes[7])<<16)|(unsigned(bytes[8])<<8)|bytes[9];Check((!minFrame || !maxFrame || minFrame<=maxFrame) && maxFrame<=MaxFrame,"Raw FLAC frame-size bounds");const auto fields=b.U64(10);rate=unsigned(fields>>44);channels=unsigned((fields>>41)&7)+1;bits=unsigned((fields>>36)&31)+1;total=fields&0xfffffffffull;Check((rate==44100 || rate==48000) && channels<=2 && (bits==16 || bits==24),"Raw FLAC rate/channels/depth outside profile");}
};
inline void Residual(Bits& b,unsigned samples,unsigned order){const auto method=b.get(2);Check(method<=1,"Reserved FLAC residual method");const auto partitions=1u<<b.get(4);Check(samples%partitions==0 && samples/partitions>=order,"Invalid FLAC residual partition");const unsigned bits=method?5:4;for(unsigned p=0;p<partitions;++p){const unsigned count=samples/partitions-(p?0:order),rice=b.get(bits);if(rice==((1u<<bits)-1)){b.skip(uint64_t(count)*b.get(5));}else for(unsigned i=0;i<count;++i){b.unary(unsigned(MaxFrame*8));b.skip(rice);}}}
inline size_t Frame(const Bytes& bytes,const Info& info,uint64_t frameNumber,unsigned& samples){Bits b{bytes};Check(b.get(14)==0x3ffe && b.get(1)==0 && b.get(1)==0,"Expected fixed-block raw FLAC frame");const auto blockCode=b.get(4),rateCode=b.get(4),assignment=b.get(4),depthCode=b.get(3);Check(b.get(1)==0,"Reserved FLAC frame bit");Check(Number(b)==frameNumber,"Discontinuous FLAC frame number");
    Check(blockCode!=0,"Reserved FLAC block size");samples=blockCode==1?192:blockCode<=5?(576u<<(blockCode-2)):blockCode==6?b.get(8)+1:blockCode==7?b.get(16)+1:1u<<blockCode;
    const unsigned rates[]={0,88200,176400,192000,8000,16000,22050,24000,32000,44100,48000,96000};Check(rateCode!=15,"Forbidden FLAC sample rate");const auto rate=rateCode<12?(rateCode?rates[rateCode]:info.rate):rateCode==12?b.get(8)*1000:rateCode==13?b.get(16):b.get(16)*10;
    const unsigned depths[]={0,8,12,0,16,20,24,32};Check(depthCode!=3 && (depthCode?depths[depthCode]:info.bits)==info.bits && rate==info.rate && samples<=info.block,"FLAC frame/config mismatch");Check((assignment<=1 && assignment+1==info.channels) || (info.channels==2 && assignment>=8 && assignment<=10),"FLAC channel assignment mismatch");
    const auto header=b.bit/8;b.get(8);Check(Crc(bytes.data(),header+1,8,7)==0,"FLAC header CRC mismatch");
    for(unsigned c=0;c<info.channels;++c){unsigned bits=info.bits+((assignment==8 || assignment==10)?c==1:assignment==9?c==0:false);Check(b.get(1)==0,"FLAC subframe padding bit");const auto type=b.get(6);if(b.get(1)){const auto wasted=b.unary(bits)+1;Check(wasted<bits,"FLAC wasted bits exceed depth");bits-=wasted;}
        if(type==0)b.skip(bits);else if(type==1)b.skip(uint64_t(samples)*bits);else if(type>=8 && type<=12){const auto order=type-8;Check(order<=samples,"FLAC fixed order exceeds block");b.skip(uint64_t(order)*bits);Residual(b,samples,order);}else if(type>=32){const auto order=(type&31)+1;Check(order<=samples,"FLAC LPC order exceeds block");b.skip(uint64_t(order)*bits);const auto precision=b.get(4);Check(precision!=15,"Reserved FLAC LPC precision");Check(b.get(5)<16,"Negative FLAC LPC shift is forbidden");b.skip(uint64_t(order)*(precision+1));Residual(b,samples,order);}else Check(false,"Reserved FLAC subframe type");}
    while(b.bit%8)Check(b.get(1)==0,"Nonzero FLAC alignment padding");b.get(16);const auto end=b.bit/8;Check((!info.minFrame || end>=info.minFrame) && (!info.maxFrame || end<=info.maxFrame) && Crc(bytes.data(),end,16,0x8005)==0,"FLAC frame size/CRC mismatch");return end;
}
inline Bytes Init(const Info& i){Bytes sample=audiomp4::Z(28);sample[7]=1;audiomp4::P(sample,16,(i.channels<<16)|i.bits);audiomp4::P(sample,24,i.rate<<16);Bytes cfg=audiomp4::Z(8);audiomp4::P(cfg,4,0x80000022);audiomp4::J(cfg,i.stream);audiomp4::J(sample,audiomp4::B("dfLa",cfg));return audiomp4::Init(i.rate,audiomp4::B("fLaC",sample));}
class Layout:public audiomp4::Projection {
    uint64_t physical=0,loaded=0,frames=0,samples=0;bool initialized=false,shortBlock=false;Bytes pending;

public:
    Info info;
    bool Initialized()const{return initialized;}uint64_t Frames()const{return frames;}uint64_t Samples()const{return samples;}uint64_t Consumed()const{return physical;}
    void Update(const FragmentIndex::Read& read,uint64_t prefix,bool complete){if(!initialized){uint8_t magic[4];if(prefix<4)throw Waiting{};Check(read(0,magic,4),"Raw FLAC initialization read failed");Check(!std::memcmp(magic,"fLaC",4),"Expected native FLAC marker");uint64_t at=4;bool last=false;unsigned blocks=0;while(!last){if(!RangeReady(at,4,prefix))throw Waiting{};uint8_t h[4];Check(read(at,h,4),"Raw FLAC metadata read failed");last=(h[0]&128)!=0;const auto type=h[0]&127;const size_t n=(size_t(h[1])<<16)|(size_t(h[2])<<8)|h[3];Check(++blocks<=128 && type<=6 && at+4+n<=MaxInit,"Raw FLAC metadata exceeds profile");if(!RangeReady(at+4,n,prefix))throw Waiting{};if(blocks==1){Check(type==0 && n==34,"Raw FLAC requires initial STREAMINFO");Bytes b(n);Check(read(at+4,b.data(),n),"Raw FLAC STREAMINFO read failed");info.parse(b);}else Check(type!=0,"Duplicate FLAC STREAMINFO");at+=4+n;}physical=loaded=at;memory(Init(info));initialized=true;}
        Check(prefix>=loaded,"Raw FLAC prefix regressed");for(;;){unsigned count=0;size_t n=0;if(!pending.empty()){try{n=Frame(pending,info,frames,count);}catch(const Waiting&){}if(n){Check(!shortBlock && frames<MaxFrames,"Raw FLAC follows short block or exceeds frame count");Check(!info.total || samples+count<=info.total,"Raw FLAC exceeds total samples");append(frames+1,samples,count,physical,n);shortBlock=count<info.block;samples+=count;++frames;physical+=n;pending.erase(pending.begin(),pending.begin()+n);continue;}}
            if(loaded==prefix)break;const auto countBytes=size_t((std::min)(uint64_t(65536),prefix-loaded));Check(pending.size()+countBytes<=MaxFrame+65536,"Raw FLAC pending frame exceeds limit");const auto old=pending.size();pending.resize(old+countBytes);Check(read(loaded,pending.data()+old,countBytes),"Raw FLAC frame read failed");loaded+=countBytes;}
        if(complete)Check(pending.empty() && frames>0 && (!info.total || samples==info.total),"Truncated raw FLAC or sample-count mismatch");}

};
}} // namespace harvester::rawflac
