// Explicit ordinary AAC M4A -> confirmed single audio view. GPL-3.0-or-later.
#pragma once
#include "HarvesterMP4IndexedMux.h"
#include "HarvesterAacWindow.h"
namespace harvester { namespace m4a {
inline void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Waiting {};
struct Bits {
    BoxData bytes;size_t at=0;
    unsigned get(unsigned n){Check(n<=24 && at+n<=bytes.size*8,"Truncated AAC AudioSpecificConfig");unsigned v=0;while(n--){v=(v<<1)|((bytes.data[at/8]>>(7-at%8))&1);++at;}return v;}
    unsigned rate(){static const unsigned rates[]={96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};const auto i=get(4);Check(i<13 || i==15,"Reserved AAC frequency index");return i==15?get(24):rates[i];}
};
inline BoxData Descriptor(BoxData bytes,unsigned tag){
    Check(bytes.size>=2 && bytes.data[0]==tag,"Invalid M4A ES descriptor");size_t at=1,n=0;bool end=false;
    for(unsigned i=0;i<4;++i){Check(at<bytes.size,"Truncated M4A ES length");const auto b=bytes.data[at++];n=(n<<7)|(b&127);if(!(b&128)){end=true;break;}}
    Check(end && n<=bytes.size-at,"Invalid M4A ES descriptor length");return bytes.Slice(at,n);
}
struct Info {
    unsigned rate=0,channels=0,frame=1024,skip=0;uint64_t total=0,packets=0;
    std::string codec;
    void Parse(BoxData movie){
        const auto track=movie.One("trak"),media=track.One("mdia"),mdhd=media.One("mdhd");
        const auto entry=media.One("minf").One("stbl").One("stsd").Slice(8,media.One("minf").One("stbl").One("stsd").size-8).One("mp4a");
        Check(entry.size>=28 && entry.U32(8)==0,"M4A requires a version-zero AAC sample entry");
        const auto extra=entry.Slice(28,entry.size-28);Check(!extra.One("sinf",true).data,"Encrypted M4A is not supported");
        const auto esds=extra.One("esds");Check(esds.U32(0)==0,"Invalid M4A ESDS version");
        const auto es=Descriptor(esds.Slice(4,esds.size-4),3);Check(es.size>=3 && es.data[2]==0,"External/dependent M4A descriptors forbidden");
        const auto dec=Descriptor(es.Slice(3,es.size-3),4);Check(dec.size>=13 && dec.data[0]==0x40 && (dec.data[1]>>2)==5,"Expected MPEG4 AAC");
        Bits bits{Descriptor(dec.Slice(13,dec.size-13),5)};unsigned object=bits.get(5),core=bits.rate(),configured=bits.get(4);bool sbr=object==5 || object==29,ps=object==29,psKnown=object==29;rate=core;
        if(sbr){rate=bits.rate();object=bits.get(5);}
        Check(object==2 && configured>=1 && configured<=2,"M4A enables AAC-LC/HE-AAC mono/stereo only");
        Check(bits.get(1)==0 && bits.get(1)==0 && bits.get(1)==0,"Unsupported AAC frame/core/extension configuration");
        if(!sbr && bits.bytes.size*8-bits.at>=16){
            const auto sync=bits.get(11);if(sync==0x2b7){Check(bits.get(5)==5,"Unsupported AAC sync extension");sbr=bits.get(1)!=0;if(sbr)rate=bits.rate();
                if(bits.bytes.size*8-bits.at>=12 && bits.get(11)==0x548){ps=bits.get(1)!=0;psKnown=true;}}
            else Check(sync==0,"Unknown AAC trailing configuration");
        }
        Check((rate==44100 || rate==48000) && (sbr?core*2==rate:core==rate),"Unsupported M4A AAC sample rate");
        // AAC decoders auto-enable PS for unspecified mono SBR. An explicit
        // sync extension with PS=false is needed to promise one output channel.
        channels=ps || (sbr && configured==1 && !psKnown)?2:configured;frame=sbr?2048:1024;codec=sbr?"he-aac":"aac-lc";
        Check(!ps || sbr,"AAC PS requires SBR");
        const auto scale=mdhd.U32(mdhd.data[0]?20:12);Check(scale==rate,"M4A media clock must equal decoded sample rate");
        const uint64_t duration=mdhd.data[0]?mdhd.U64(24):mdhd.U32(16);uint64_t end=duration;
        const auto edits=track.One("edts",true);
        if(edits.data){const auto e=edits.One("elst");Check(e.U32(0)==0 && e.U32(4)==1 && e.size==20 && e.U32(16)==0x10000,"M4A requires one unit-rate edit");skip=e.U32(12);
            const auto mvhd=movie.One("mvhd");const auto movieScale=mvhd.U32(mvhd.data[0]?20:12);Check(movieScale && e.U32(8)>0,"Invalid M4A edit duration");
            end=(std::min)(duration,uint64_t(skip)+(uint64_t(e.U32(8))*rate+movieScale-1)/movieScale);
        }
        Check(skip<=16384 && end>skip && end-skip<=uint64_t(rate)*3600,"Invalid M4A presentation window");total=end-skip;
        const auto table=media.One("minf").One("stbl");Check(!table.One("ctts",true).data,"M4A audio composition offsets are not enabled");
        const auto times=table.One("stts");const auto runs=times.U32(4);packets=0;
        Check(times.U32(0)==0 && runs && times.size==8+size_t(runs)*8,"Invalid M4A packet clock");
        for(uint32_t i=0;i<runs;++i){const auto count=times.U32(8+i*8),duration=times.U32(12+i*8);Check(count && duration && duration<=frame && (duration==frame || (i+1==runs && count==1)),"M4A requires a consecutive decoded AAC packet clock");packets+=count;}
        const auto minimum=(uint64_t(skip)+total+frame-1)/frame;
        Check(packets>=minimum && packets<=minimum+4,"M4A packet count disagrees with presentation window");
    }
};
class Layout {
    indexedmux::Layout indexed{true};
public:
    Info info;indexedmux::View view;
    void Configure(uint64_t total,const std::vector<std::pair<uint64_t,uint64_t>>& ranges){indexed.SetLength(total);indexed.SetTailRanges(ranges);}
    bool Initialized()const{return indexed.Initialized();}
    uint64_t Frames()const{return view.index.Samples().size();}
    uint64_t Samples()const{if(!Initialized() || view.index.Samples().empty())return 0;const auto t=view.index.Samples().back().end;return t>0?(std::min)(info.total,(uint64_t(t)*info.rate+9999999)/10000000):0;}
    uint64_t DecoderBytes()const{return indexed.Total(1,false);}
    void Update(const FragmentIndex::Read& read,uint64_t prefix,bool complete){
        try{indexed.Update(read,prefix,complete,[&](unsigned k,BoxData movie){Check(k==1,"M4A must not create a video track");info.Parse(movie);});}
        catch(const mp4index::NeedRange&){if(complete)throw std::runtime_error("Complete M4A lacks confirmed index");throw Waiting{};}
        Check(!complete || indexed.Initialized(),"Complete M4A lacks initialization");
        if(!Initialized())throw Waiting{};
        view=indexed.tracks[1];
    }
    bool ReadAt(const FragmentIndex::Read& read,uint64_t at,void* out,size_t n)const{return indexed.ReadAt(read,1,at,out,n);}
    bool ReadDecoderAt(const FragmentIndex::Read& read,uint64_t at,void* out,size_t n)const{return indexed.ReadDecoderAt(read,1,at,out,n);}
};
}}
