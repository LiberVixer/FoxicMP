// Private continuous AVC/AAC cache index. Full payload coverage, two clocks.
// GPL-3.0-or-later.
#pragma once
#include "progressivestreamaudio.h"
#include "DSUtil/HarvesterMP4Muxed.h"
#include "DSUtil/HarvesterHevc.h"
#include "DSUtil/HarvesterAV1.h"
namespace foxicmp { namespace progressive {
class StreamAvIndex {
    StreamAudioWindow window;
    const bool hevc,av1;
    harvester::muxmp4::Layout layout=harvester::muxmp4::Layout::ContinuousAv();
    harvester::muxmp4::Layout pending=harvester::muxmp4::Layout::ContinuousAv();
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
        if(!kind){
            check(entry.size>=78);const auto children=entry.Slice(78,entry.size-78);check(!children.One("sinf",true).data);
            if(hevc){
                check(entries[0].first==harvester::FourCC("hvc1") || entries[0].first==harvester::FourCC("hev1"));
                const auto config=children.One("hvcC");harvester::HevcParameterSets parameters;
                harvester::HEVCConfiguration(config.data,config.size,entries[0].first==harvester::FourCC("hvc1"),[](bool ok,const char*){check(ok);},&parameters);
                check(parameters.size()==3);
            }else if(av1){
                check(entries[0].first==harvester::FourCC("av01"));const auto cfg=children.One("av1C");
                const auto sequence=harvester::AV1Configuration(cfg.data,cfg.size,[](bool ok,const char*){check(ok);});
                check(!(sequence[0]&0x18));
            }else{
                check(entries[0].first==harvester::FourCC("avc1"));const auto avcc=children.One("avcC");check(avcc.size>=7 && avcc.data[0]==1);
            }
            return;
        }
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
    explicit StreamAvIndex(StreamAudioWindow value,bool hevcVideo=false,bool av1Video=false):window(std::move(value)),hevc(hevcVideo),av1(av1Video){check(!(hevc && av1) && (!(hevc || av1) || window.pcm.RawBegin()==1024));}
    void update(const harvester::FragmentIndex::Read& read,uint64_t prefix,bool complete=false){
        pending.CopyFrom(layout);auto& next=pending;
        next.Update(read,prefix,complete,[&](unsigned k,harvester::BoxData movie){initialization(k,movie);});
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
        if(complete)check(!video.empty() && audio.size()==window.pcm.PacketCount());
        // Commit only after all validation. Retain the previous buffers for the
        // next transaction instead of leaving growing holes in native arenas.
        std::swap(layout,pending);
    }
    bool initialized()const{return layout.Initialized();}
    uint64_t position()const{return layout.Position();}
    uint64_t samples()const{return layout.tracks[0].index.Samples().size()+layout.tracks[1].index.Samples().size();}
    bool audioComplete()const{return layout.tracks[1].index.Samples().size()==window.pcm.PacketCount();}
    int64_t audioDuration100ns()const{return harvester::Time100ns(int64_t(window.pcm.Samples()),window.pcm.Rate());}
    const harvester::muxmp4::Layout& views()const{return layout;}
};
}}
