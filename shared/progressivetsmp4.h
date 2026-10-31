// Lossless virtual fMP4 adapter over verified TS access units. GPL-3.0-or-later.
#pragma once
#include "progressivetsgrowing.h"

namespace foxicmp { namespace progressive {
namespace tsmp4 {
using Bytes=std::vector<char>;
inline void Check(bool ok,const char* why){Require(ok,"tsMp4",why,"unsupported_media");}
inline void U(Bytes& b,uint64_t v,unsigned n){for(unsigned i=n;i;--i)b.push_back(char(v>>((i-1)*8)));}
inline void At(Bytes& b,size_t at,uint64_t v,unsigned n){Check(at<=b.size() && n<=b.size()-at,"Invalid MP4 field");for(unsigned i=0;i<n;++i)b[at+i]=char(v>>((n-i-1)*8));}
inline void Join(Bytes& a,const Bytes& b){a.insert(a.end(),b.begin(),b.end());}
inline Bytes Box(const char* type,const Bytes& body){Check(body.size()<=UINT32_MAX-8,"MP4 box exceeds its bound");Bytes b;U(b,body.size()+8,4);b.insert(b.end(),type,type+4);Join(b,body);return b;}
inline Bytes Fields(std::initializer_list<uint32_t> values){Bytes b;for(auto v:values)U(b,v,4);return b;}
inline void Matrix(Bytes& b){Join(b,Fields({0x10000,0,0,0,0x10000,0,0,0,0x40000000}));}
inline Bytes Descriptor(unsigned tag,const Bytes& body){Bytes b;U(b,tag,1);unsigned groups=1;for(auto n=body.size();n>=128;n>>=7)++groups;Check(groups<=4,"MP4 descriptor exceeds its bound");for(unsigned i=groups;i;--i)U(b,((body.size()>>((i-1)*7))&127)|(i>1?128:0),1);Join(b,body);return b;}
inline Bytes Initialization(const harvester::ts::CodecConfig& c,int64_t duration,uint64_t bias) {
    Bytes ftyp;ftyp.insert(ftyp.end(),{'i','s','o','6'});U(ftyp,0,4);ftyp.insert(ftyp.end(),{'i','s','o','6','m','p','4','1'});auto out=Box("ftyp",ftyp);
    Bytes mvhd;U(mvhd,0x01000000,4);U(mvhd,0,8);U(mvhd,0,8);U(mvhd,10000000,4);U(mvhd,uint64_t(duration),8);U(mvhd,0x10000,4);U(mvhd,0x100,2);U(mvhd,0,2);U(mvhd,0,8);Matrix(mvhd);mvhd.resize(mvhd.size()+24);U(mvhd,2,4);
    Bytes tkhd;U(tkhd,0x01000007,4);U(tkhd,0,8);U(tkhd,0,8);U(tkhd,1,4);U(tkhd,0,4);U(tkhd,uint64_t(duration),8);U(tkhd,0,8);U(tkhd,0,2);U(tkhd,0,2);U(tkhd,c.video?0:0x100,2);U(tkhd,0,2);Matrix(tkhd);U(tkhd,uint64_t(c.avc.width)<<16,4);U(tkhd,uint64_t(c.avc.height)<<16,4);
    const uint32_t scale=c.video?10000000:c.rate;
    const auto mediaDuration=uint64_t(duration)/10000000*scale+(uint64_t(duration)%10000000)*scale/10000000;
    Bytes mdhd;U(mdhd,0x01000000,4);U(mdhd,0,8);U(mdhd,0,8);U(mdhd,scale,4);U(mdhd,mediaDuration,8);U(mdhd,0x55c4,2);U(mdhd,0,2);
    auto hdlr=Fields({0,0,c.video?harvester::FourCC("vide"):harvester::FourCC("soun"),0,0,0});hdlr.push_back(0);
    Bytes entry(c.video?78:28,0);At(entry,6,1,2);
    if(c.video) {
        Check(c.sps.size()>=4 && c.sps.size()<=65535 && !c.pps.empty() && c.pps.size()<=65535,"SPS/PPS exceeds avcC lengths");
        At(entry,24,c.avc.width,2);At(entry,26,c.avc.height,2);At(entry,28,0x480000,4);At(entry,32,0x480000,4);At(entry,40,1,2);At(entry,74,24,2);At(entry,76,65535,2);
        Bytes config{1,char(c.sps[1]),char(c.sps[2]),char(c.sps[3]),char(255),char(225)};U(config,c.sps.size(),2);config.insert(config.end(),c.sps.begin(),c.sps.end());U(config,1,1);U(config,c.pps.size(),2);config.insert(config.end(),c.pps.begin(),c.pps.end());
        if(c.avc.profile==100){U(config,253,1);U(config,248,1);U(config,248,1);U(config,0,1);}Join(entry,Box("avcC",config));
    }else {
        At(entry,16,c.channels,2);At(entry,18,16,2);At(entry,24,uint64_t(c.rate)<<16,4);
        Bytes asc;U(asc,(2u<<11)|(c.samplingIndex<<7)|(c.channels<<3),2);Bytes decoder{char(0x40),char(0x15),0,0,0};decoder.resize(13);Join(decoder,Descriptor(5,asc));
        Bytes es{0,1,0};Join(es,Descriptor(4,decoder));Join(es,Descriptor(6,Bytes{2}));Bytes esds(4,0);Join(esds,Descriptor(3,es));Join(entry,Box("esds",esds));
    }
    auto stsd=Fields({0,1});Join(stsd,Box(c.video?"avc1":"mp4a",entry));Bytes stbl=Box("stsd",stsd);Join(stbl,Box("stts",Fields({0,0})));Join(stbl,Box("stsc",Fields({0,0})));Join(stbl,Box("stsz",Fields({0,0,0})));Join(stbl,Box("stco",Fields({0,0})));
    auto dref=Fields({0,1});Join(dref,Box("url ",Fields({1})));Bytes minf=c.video?Box("vmhd",Fields({1,0,0})):Box("smhd",Fields({0,0}));Join(minf,Box("dinf",Box("dref",dref)));Join(minf,Box("stbl",stbl));
    Bytes mdia=Box("mdhd",mdhd);Join(mdia,Box("hdlr",hdlr));Join(mdia,Box("minf",minf));
    Bytes elst=Fields({0x01000000,1});U(elst,uint64_t(duration),8);U(elst,bias,8);U(elst,0x10000,4);
    Bytes trak=Box("tkhd",tkhd);Join(trak,Box("edts",Box("elst",elst)));Join(trak,Box("mdia",mdia));
    Bytes moov=Box("mvhd",mvhd);Join(moov,Box("trak",trak));Join(moov,Box("mvex",Box("trex",Fields({0,1,1,0,0,0}))));Join(out,Box("moov",moov));return out;
}
inline Bytes Payload(const TsResourceStream::Packet& packet,bool video) {
    Require(packet.result.data(),"tsMp4",packet.result.reason.c_str(),packet.result.code.c_str());
    if(!video)return packet.bytes;
    const auto nals=harvester::ts::AnnexB({reinterpret_cast<const uint8_t*>(packet.bytes.data()),packet.bytes.size()});Bytes out;out.reserve(packet.bytes.size()+nals.size()*4);
    for(const auto& nal:nals){U(out,nal.bytes.size,4);out.insert(out.end(),nal.bytes.bytes,nal.bytes.bytes+nal.bytes.size);}return out;
}
struct Entry {uint64_t offset=0;uint32_t size=0,duration=0;int64_t decode=0;int32_t composition=0;bool sync=false;};
constexpr size_t HeaderBytes=112;
inline Bytes Header(const Entry& e,size_t index,uint64_t bias) {
    auto tfhd=Box("tfhd",Fields({0x020000,1}));Bytes tfdt=Fields({0x01000000});U(tfdt,uint64_t(e.decode+int64_t(bias)),8);
    auto trun=Fields({0x01000f01,1,uint32_t(HeaderBytes),e.duration,e.size,e.sync?0x02000000u:0x01010000u,uint32_t(e.composition)});
    Bytes traf=tfhd;Join(traf,Box("tfdt",tfdt));Join(traf,Box("trun",trun));auto body=Box("mfhd",Fields({0,uint32_t(index+1)}));Join(body,Box("traf",traf));auto out=Box("moof",body);U(out,uint64_t(e.size)+8,4);out.insert(out.end(),{'m','d','a','t'});Check(out.size()==HeaderBytes,"Unexpected MP4 fragment header size");return out;
}
} // namespace tsmp4

class TsMp4Bridge {
    struct Track {std::string id,init;tsmp4::Bytes initialization;std::vector<tsmp4::Entry> entries;uint64_t bytes=0,bias=0;int64_t decodeOrigin=0;};
    struct Published {Snapshot model;std::array<Track,2> tracks;harvester::Snapshot legacy;};
    TsPlaybackInput input;
    std::shared_ptr<const Published> current;
    std::mutex publication;
    std::atomic<bool> fatal{false};
    std::atomic<uint64_t> decoderBytes[2]{};
    struct Cache {std::mutex mutex;size_t index=SIZE_MAX;tsmp4::Bytes bytes;};std::array<Cache,2> cache;
    static harvester::State State(const std::string& text){return text=="complete"?harvester::State::Complete:text=="paused"?harvester::State::Paused:text=="merging"?harvester::State::Merging:text=="failed"?harvester::State::Failed:text=="cancelled"?harvester::State::Cancelled:harvester::State::Downloading;}
    void check() const {Require(!fatal.load(),"tsMp4","TS remux failed; close and reopen","session_closed");input.ensureOpen();}
    void rebuild() {
        auto old=std::atomic_load(&current);auto next=std::make_shared<Published>();next->model=input.model();const auto ready=input.status();
        auto& legacy=next->legacy;legacy.sessionId=next->model.id;legacy.generation=next->model.generation;legacy.revision=next->model.revision;legacy.duration=next->model.duration;legacy.timestampOffset=0;legacy.state=State(next->model.state);
        for(const auto& item:next->model.tracks)next->tracks[item.second.kind=="video"?0:1].id=item.first;
        for(unsigned kind=0;kind<2;++kind) {
            auto& track=next->tracks[kind];const auto index=input.index(kind);if(old)track=old->tracks[kind];
            const auto& declared=next->model.tracks.at(track.id);track.init=input.isGrowing()?declared.resource:declared.initialization;if(!declared.segments.empty() && !declared.segments.front().initialization.empty())track.init=declared.segments.front().initialization;
            if(index && !index->samples().empty()) {
                const auto& config=*index->configuration();const auto& units=index->samples();
                if(track.initialization.empty()){
                    const auto first=units.front().sample.decode;
                    // MOV edit-list skip_samples uses the audio sample clock.
                    // Round once at the origin, then keep exact 1024-sample AUs.
                    track.decodeOrigin=kind?(first>=0?(first*config.rate+5000000)/10000000:-((-first*config.rate+5000000)/10000000)):first;
                    track.bias=uint64_t((std::max)(int64_t(0),-track.decodeOrigin));track.initialization=tsmp4::Initialization(config,legacy.duration,track.bias);track.bytes=track.initialization.size();
                }
                for(size_t i=track.entries.size();i<units.size();++i) {
                    const auto bytes=tsmp4::Payload(input.sample(kind,i),kind==0);const auto start=kind?int64_t(i*1024):harvester::ts::Cadence(config,i),end=kind?start+1024:harvester::ts::Cadence(config,i+1);
                    const auto decode=harvester::AddTime(track.decodeOrigin,start);const auto pts=kind?decode:units[i].sample.start;const auto composition=pts-decode;
                    tsmp4::Check(end>start && uint64_t(end-start)<=UINT32_MAX && composition>=INT32_MIN && composition<=INT32_MAX && bytes.size()<=UINT32_MAX && bytes.size() && track.bytes<=MaxByte-tsmp4::HeaderBytes-bytes.size(),"TS packet cannot be represented as bounded fMP4");
                    track.entries.push_back({track.bytes,uint32_t(bytes.size()),uint32_t(end-start),decode,int32_t(composition),units[i].sample.sync});track.bytes+=tsmp4::HeaderBytes+bytes.size();
                }
            }
            auto& mapped=legacy.tracks[kind];mapped.path=input.isMuxed()?(kind?"audio.fmp4":"video.fmp4"):next->model.resources.at(track.init).name;mapped.container=kind?"m4a":"mp4";mapped.initializationReady=!track.initialization.empty();mapped.fragmented=true;mapped.required=true;mapped.available=track.bytes;mapped.total=ready.trackComplete[kind]?track.bytes:0;mapped.state=State(declared.state);
        }
        check();std::shared_ptr<const Published> accepted=std::move(next);std::atomic_store(&current,std::move(accepted));
    }
    ResourceRead read(unsigned kind,uint64_t offset,void* data,size_t count) {
        check();Require(kind<2 && count<=65536 && offset<=MaxByte && count<=MaxByte-offset && (data || !count),"tsMp4","Invalid bounded remux read","invalid-read");const auto state=std::atomic_load(&current);const auto& track=state->tracks[kind];
        if(!count)return {"data","Empty remux read",0};
        if(offset>=track.bytes)return {state->legacy.tracks[kind].total?(offset==track.bytes?"eof":"out-of-range"):"waiting-range","Remux track tail",0};
        const auto wanted=size_t((std::min)(uint64_t(count),track.bytes-offset));tsmp4::Bytes out(wanted);size_t copied=0;
        if(offset<track.initialization.size()){const auto n=(std::min)(wanted,track.initialization.size()-size_t(offset));std::memcpy(out.data(),track.initialization.data()+size_t(offset),n);offset+=n;copied+=n;}
        auto at=std::upper_bound(track.entries.begin(),track.entries.end(),offset,[](uint64_t p,const tsmp4::Entry& e){return p<e.offset;});size_t i=at==track.entries.begin()?0:size_t(at-track.entries.begin()-1);
        for(;copied<wanted && i<track.entries.size();++i) {
            const auto& e=track.entries[i];Require(offset>=e.offset && offset<e.offset+tsmp4::HeaderBytes+e.size,"tsMp4","Remux layout gap","invalid-read");
            auto position=size_t(offset-e.offset);
            if(position<tsmp4::HeaderBytes){const auto header=tsmp4::Header(e,i,track.bias);const auto n=(std::min)(wanted-copied,header.size()-position);std::memcpy(out.data()+copied,header.data()+position,n);copied+=n;offset+=n;position+=n;}
            if(copied==wanted)break;
            std::lock_guard<std::mutex> lock(cache[kind].mutex);auto& cached=cache[kind];
            if(cached.index!=i){auto bytes=tsmp4::Payload(input.sample(kind,i),kind==0);Require(bytes.size()==e.size,"tsMp4","TS remux packet size changed","source-rewritten");cached.bytes=std::move(bytes);cached.index=i;}
            const auto payloadOffset=position-tsmp4::HeaderBytes,n=(std::min)(wanted-copied,cached.bytes.size()-payloadOffset);std::memcpy(out.data()+copied,cached.bytes.data()+payloadOffset,n);copied+=n;offset+=n;
        }
        check();Require(copied==wanted,"tsMp4","Incomplete remux read","source-truncated");if(wanted)std::memcpy(data,out.data(),wanted);return {"data","Verified TS remux bytes",wanted};
    }
public:
    TsMp4Bridge(const std::filesystem::path& root,const std::filesystem::path& registry,const std::string& json):input(root,registry,json){rebuild();const auto s=snapshot();Require(s.tracks[0].initializationReady && s.tracks[1].initializationReady,"tsMp4","Wait for the first complete AVC/AAC segments","waiting_initialization");}
    harvester::Snapshot snapshot() const {return std::atomic_load(&current)->legacy;}
    std::vector<BoundResource> bindings() const {return input.bindings();}
    bool usesCatalog() const {return input.usesCatalog();}
    bool isMuxed() const {return input.isMuxed();}
    bool isGrowing() const {return input.isGrowing();}
    bool readIndex(unsigned kind,uint64_t offset,void* data,size_t count) {
        auto* p=static_cast<char*>(data);size_t at=0;
        while(at<count){const auto n=(std::min)(count-at,size_t(65536));const auto r=read(kind,offset+at,p+at,n);if(!r.data() || r.bytes!=n)return false;at+=n;}return true;
    }
    int64_t readPayload(unsigned kind,uint64_t offset,void* data,size_t count) {
        try{const auto r=read(kind,offset,data,(std::min)(count,size_t(65536)));if(r.data()){decoderBytes[kind].fetch_add(r.bytes);return int64_t(r.bytes);}return r.code=="eof"?0:-1;}catch(...){fatal.store(true);return -1;}
    }
    void publish(const std::string& json) {
        std::lock_guard<std::mutex> lock(publication);check();input.publish(json);try{rebuild();}catch(...){fatal.store(true);throw;}
    }
    void verify(){check();input.verify();}
    void cancel(){input.cancel();}
    std::string details() const {
        const auto state=std::atomic_load(&current);rapidjson::Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("schemaVersion",2,a);d.AddMember("generation",Value(std::to_string(state->model.generation).c_str(),a),a);d.AddMember("revision",Value(std::to_string(state->model.revision).c_str(),a),a);d.AddMember("writerInstanceId",Value(state->model.writer.c_str(),a),a);
        d.AddMember("mediaMode",Value(isGrowing()?"growing-muxed-mpegts-h264-aac-lc":isMuxed()?"segmented-muxed-mpegts-h264-aac-lc":"segmented-mpegts-h264-aac-lc",a),a);d.AddMember("muxed",isMuxed(),a);d.AddMember("videoContainer","mpegts",a);d.AddMember("audioContainer","mpegts",a);d.AddMember("decoderContainer","fmp4",a);d.AddMember("videoCodec","h264",a);d.AddMember("audioCodec","aac-lc",a);d.AddMember("segments",!isGrowing(),a);d.AddMember("sourceTimestampOffset",Value(std::to_string(state->model.offset).c_str(),a),a);
        Value bytes(rapidjson::kObjectType);bytes.AddMember("video",Value(std::to_string(decoderBytes[0].load()).c_str(),a),a);bytes.AddMember("audio",Value(std::to_string(decoderBytes[1].load()).c_str(),a),a);d.AddMember("decoderIoBytes",bytes,a);
        if(usesCatalog()){const auto s=input.status();Value info(rapidjson::kObjectType);info.AddMember("mode",Value(isMuxed()?"paged-muxed-mpegts-v1":"paged-mpegts-v1",a),a);info.AddMember("pageCount",unsigned(s.pages),a);info.AddMember("resourceCount",unsigned(s.resources),a);info.AddMember("maxOpenResources",8,a);info.AddMember("integrityRequired",true,a);d.AddMember("segmentCatalog",info,a);}
        if(!state->model.metadata.empty()){rapidjson::Document m;m.Parse(state->model.metadata.c_str());Value v(m,a);d.AddMember("metadata",v,a);}
        Value tracks(rapidjson::kArrayType);for(unsigned i=0;i<2;++i){const auto& t=state->model.tracks.at(state->tracks[i].id);Value v(rapidjson::kObjectType);v.AddMember("index",i,a);v.AddMember("id",Value(t.id.c_str(),a),a);v.AddMember("kind",Value(t.kind.c_str(),a),a);v.AddMember("label",Value(t.label.c_str(),a),a);v.AddMember("language",Value(t.language.c_str(),a),a);v.AddMember("codec",Value(i?"aac-lc":"h264",a),a);v.AddMember("container","mpegts",a);v.AddMember("default",t.defaultTrack,a);v.AddMember("required",true,a);v.AddMember("ready",state->legacy.tracks[i].initializationReady,a);v.AddMember("state",Value(state->legacy.tracks[i].initializationReady?"ready":"waiting",a),a);v.AddMember("errorCode","",a);v.AddMember("error","",a);v.AddMember("decoderIoBytes",Value(std::to_string(decoderBytes[i].load()).c_str(),a),a);tracks.PushBack(v,a);}d.AddMember("mediaTracks",tracks,a);d.AddMember("subtitleTracks",Value(rapidjson::kArrayType),a);return lease_detail::Encode(d);
    }
};
}} // namespace foxicmp::progressive
