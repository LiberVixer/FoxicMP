// Private continuous AVC/AAC cache index. Full payload coverage, two clocks.
// GPL-3.0-or-later.
#pragma once
#include "progressivestreamaudio.h"
#include "DSUtil/HarvesterMP4Muxed.h"
namespace foxicmp { namespace progressive {
class StreamAvIndex {
    StreamAudioWindow window;
    harvester::muxmp4::Layout layout=harvester::muxmp4::Layout::ContinuousAv();
    static void check(bool ok){Require(ok,"remuxCache","Invalid continuous A/V output","cache-invalid-fragment");}
    static harvester::BoxData descriptor(harvester::BoxData bytes,uint8_t tag){
        check(bytes.size>=2 && bytes.data[0]==tag);size_t at=1,n=0;bool ended=false;
        for(unsigned i=0;i<4 && at<bytes.size;++i){const auto v=bytes.data[at++];n=(n<<7)|(v&127);if(!(v&128)){ended=true;break;}}
        check(ended && n<=bytes.size-at);return bytes.Slice(at,n);
    }
    void initialization(unsigned kind,harvester::BoxData movie)const{
        const auto track=movie.One("trak"),media=track.One("mdia"),mdhd=media.One("mdhd"),tkhd=track.One("tkhd");
        check((mdhd.U32(0)>>24)<=1 && (tkhd.U32(0)>>24)<=1);
        const auto stsd=media.One("minf").One("stbl").One("stsd");const auto entries=stsd.Slice(8,stsd.size-8).Children();check(entries.size()==1);
        const auto entry=entries[0].second;
        if(!kind){check(entries[0].first==harvester::FourCC("avc1") && entry.size>=78);const auto children=entry.Slice(78,entry.size-78);check(!children.One("sinf",true).data);const auto avcc=children.One("avcC");check(avcc.size>=7 && avcc.data[0]==1);return;}
        check(tkhd.U32(tkhd.data[0]?20:12)==window.trackId);
        check(mdhd.U32(mdhd.data[0]?20:12)==window.pcm.Rate());
        check(entries[0].first==harvester::FourCC("mp4a") && entry.size>=28 && entry.data[8]==0 && entry.data[9]==0 && entry.U32(24)==(window.pcm.Rate()<<16));
        const auto children=entry.Slice(28,entry.size-28);check(!children.One("sinf",true).data);
        auto esds=children.One("esds");check(esds.U32(0)==0);
        auto es=descriptor(esds.Slice(4,esds.size-4),3);check(es.size>=3 && es.data[2]==0);
        auto config=descriptor(es.Slice(3,es.size-3),4);check(config.size>=13 && config.data[0]==0x40 && (config.data[1]>>2)==5);
        auto asc=descriptor(config.Slice(13,config.size-13),5);check(asc.size>=2 && (asc.data[0]>>3)==2 && (asc.data[1]&7)==0);
        const auto rateIndex=((asc.data[0]&7)<<1)|(asc.data[1]>>7),channels=(asc.data[1]>>3)&15;
        check(rateIndex==(window.pcm.Rate()==48000?3:4) && channels==window.pcm.Channels());
    }
public:
    explicit StreamAvIndex(StreamAudioWindow value):window(std::move(value)){}
    void update(const harvester::FragmentIndex::Read& read,uint64_t prefix,bool complete=false){
        auto next=layout;next.Update(read,prefix,complete,[&](unsigned k,harvester::BoxData movie){initialization(k,movie);});
        const auto& audio=next.tracks[1].index.Samples();const auto& video=next.tracks[0].index.Samples();
        check(audio.size()<=window.pcm.PacketCount() && video.size()+audio.size()<=50000);
        // FragmentIndex clocks are 100 ns. Permit only its one-tick rounding,
        // never infer sample counts by truncating these rounded timestamps.
        for(size_t i=layout.tracks[1].index.Samples().size();i<audio.size();++i){
            const auto begin=harvester::Time100ns(int64_t(i*1024)-int64_t(window.pcm.RawBegin()),window.pcm.Rate());
            const auto end=harvester::Time100ns(int64_t((i+1)*1024)-int64_t(window.pcm.RawBegin()),window.pcm.Rate());
            const auto withinTick=[](int64_t a,int64_t b){return a>=b-1 && a<=b+1;};
            const bool shortenedLast=i+1==window.pcm.PacketCount() && withinTick(audio[i].end,harvester::Time100ns(int64_t(window.pcm.Samples()),window.pcm.Rate()));
            check(withinTick(audio[i].start,begin) && withinTick(audio[i].decode,begin) && (withinTick(audio[i].end,end) || shortenedLast));
        }
        if(complete)check(!video.empty() && audio.size()==window.pcm.PacketCount());layout=std::move(next);
    }
    bool initialized()const{return layout.Initialized();}
    uint64_t position()const{return layout.Position();}
    uint64_t samples()const{return layout.tracks[0].index.Samples().size()+layout.tracks[1].index.Samples().size();}
    bool audioComplete()const{return layout.tracks[1].index.Samples().size()==window.pcm.PacketCount();}
    int64_t audioDuration100ns()const{return harvester::Time100ns(int64_t(window.pcm.Samples()),window.pcm.Rate());}
    const harvester::muxmp4::Layout& views()const{return layout;}
};
}}
