// First bounded schema-2 media profile. GPL-3.0-or-later.
// Reuses the proven fMP4 demuxers; ALL index and decoder bytes use ResourceSource.
#pragma once
#include "progressiveresourceio.h"
#include "DSUtil/HarvesterMP4.h"
#include <array>

namespace foxicmp { namespace progressive {
inline const char* Fmp4V2Capabilities() {
    return R"({"mode":"separate-fmp4-h264-aac-lc","trackCount":2,"separateResourcesRequired":true,"durationRequired":true,"timebase":{"numerator":1,"denominator":10000000},"negativeTimestampOffset":false,"muxed":false,"segments":false,"live":false,"metadataApplied":false,"initializationLimitBytes":1048576})";
}
class Fmp4V2Bridge {
    Snapshot model;
    const uint64_t identityGeneration;
    const std::string identityWriter;
    std::atomic<uint64_t> detailsRevision;
    harvester::Snapshot legacy;
    std::shared_ptr<ResourceSource> source;
    std::filesystem::path registry;
    std::array<std::string,2> resourceIds;
    std::array<std::vector<char>,2> initialization;
    std::array<uint64_t,2> initOffsets{};
    std::atomic<uint64_t> decoderBytes[2]{};
    std::string failure;
    mutable std::mutex mutex;
    static void supported(bool ok,const char* text) {Require(ok,"mediaProfile",text,"unsupported_media");}
    static harvester::State state(const std::string& text) {
        if(text=="paused")return harvester::State::Paused;
        if(text=="merging")return harvester::State::Merging;
        if(text=="complete")return harvester::State::Complete;
        if(text=="failed")return harvester::State::Failed;
        if(text=="cancelled")return harvester::State::Cancelled;
        return harvester::State::Downloading;
    }
    static uint64_t prefix(const Resource& resource) {
        uint64_t end=0;for(const auto& range:resource.ranges){if(range.offset>end)break;end=range.offset+range.length;}return end;
    }
    static harvester::Snapshot map(const Snapshot& snapshot,std::array<std::string,2>* resources=nullptr) {
        supported(!snapshot.live && !snapshot.hasWindow && snapshot.knownDuration && snapshot.numerator==1 && snapshot.denominator==10000000 && snapshot.offset>=0,
                  "Media v2 currently requires VOD, known duration, 100 ns timebase and nonnegative timestamp offset");
        supported(snapshot.tracks.size()==2 && snapshot.resources.size()==2,"Media v2 currently requires exactly two separate fMP4 resources/tracks");
        harvester::Snapshot result;result.sessionId=snapshot.id;result.generation=snapshot.generation;result.revision=snapshot.revision;
        result.duration=snapshot.duration;result.timestampOffset=snapshot.offset;result.state=state(snapshot.state);
        bool found[2]={false,false};std::array<std::string,2> ids;
        for(const auto& item:snapshot.tracks) {
            const auto& track=item.second;const int index=track.kind=="video"?0:track.kind=="audio"?1:-1;
            supported(index>=0 && !found[index] && track.required && !track.resource.empty() && track.segments.empty() && track.initialization.empty(),
                      "Media v2 currently supports one required video and one required audio track, without segments or external init");
            supported(track.container=="fmp4" && (track.codec.empty() || track.codec==(index?"aac-lc":"h264")),"Use the fmp4 container profile with H.264 and AAC-LC");
            const auto& resource=snapshot.resources.at(track.resource);supported(!resource.retired,"Cannot start media from a retired resource");
            auto& output=result.tracks[index];output.path=resource.name;output.container=index?"m4a":"mp4";
            output.fragmented=true;output.initializationReady=track.initializationReady;output.available=prefix(resource);output.total=resource.known?resource.length:0;
            output.state=state(track.state);ids[index]=resource.id;found[index]=true;
        }
        supported(found[0] && found[1] && ids[0]!=ids[1],"Audio and video must reference distinct resources");
        if(resources)*resources=ids;
        return result;
    }
    bool exact(int track,uint64_t offset,void* buffer,size_t count) {
        auto* out=static_cast<char*>(buffer);
        while(count) {
            const auto size=(std::min)(count,size_t(65536));const auto read=source->read(resourceIds[track],offset,out,size);
            if(!read.data() || read.bytes!=size)return false;
            out+=size;offset+=size;count-=size;
        }
        return true;
    }
    static harvester::BoxData descriptor(harvester::BoxData bytes,uint8_t tag) {
        supported(bytes.size>=2 && bytes.data[0]==tag,"Unsupported AAC descriptor layout");
        size_t at=1,length=0;bool ended=false;
        for(unsigned i=0;i<4 && at<bytes.size;++i) {const auto value=bytes.data[at++];length=(length<<7)|(value&127);if(!(value&128)){ended=true;break;}}
        supported(ended && length<=bytes.size-at,"Truncated AAC descriptor");return bytes.Slice(at,length);
    }
    static void codec(int track,harvester::BoxData movie) {
        auto media=movie.One("trak").One("mdia");auto minf=media.One("minf");
        auto dref=minf.One("dinf").One("dref");supported(dref.size>=8 && dref.U32(4)==1,"External media references are not supported");
        auto url=dref.Slice(8,dref.size-8).One("url ");supported(url.size==4 && url.U32(0)==1,"External media references are not supported");
        auto descriptions=minf.One("stbl").One("stsd");supported(descriptions.size>=8 && descriptions.U32(4)==1,"Only one codec configuration is supported");
        auto sample=descriptions.Slice(8,descriptions.size-8).One(track?"mp4a":"avc1");
        if(!track) {
            supported(sample.size>=78,"Truncated AVC sample entry");auto children=sample.Slice(78,sample.size-78);
            supported(!children.One("sinf",true).data,"Encrypted AVC is unsupported");auto config=children.One("avcC");
            supported(config.size>=7 && config.data[0]==1,"Invalid AVC initialization");
        }else {
            supported(sample.size>=28 && sample.data[8]==0 && sample.data[9]==0,"Only version-zero AAC sample entries are supported");
            auto children=sample.Slice(28,sample.size-28);supported(!children.One("sinf",true).data,"Encrypted AAC is unsupported");
            auto esds=children.One("esds");supported(esds.size>=4,"Invalid AAC initialization");auto es=descriptor(esds.Slice(4,esds.size-4),3);
            supported(es.size>=3 && es.data[2]==0,"External/dependent AAC descriptors are unsupported");auto config=descriptor(es.Slice(3,es.size-3),4);
            supported(config.size>=13 && config.data[0]==0x40 && (config.data[1]>>2)==5,"Expected MPEG-4 AAC");
            auto asc=descriptor(config.Slice(13,config.size-13),5);supported(asc.size>=2 && (asc.data[0]>>3)==2,"Only AAC-LC is enabled in this media profile");
        }
    }
    void initialize(int track) {
        Require(legacy.tracks[track].initializationReady,"initializationReady","Wait for MP4 initialization","waiting_initialization");
        const auto available=legacy.tracks[track].available;uint64_t at=0;
        while(harvester::RangeReady(at,8,available) && at<1048576) {
            unsigned char header[16]{};Require(exact(track,at,header,8),"initialization","Cannot read confirmed MP4 header","source_not_ready");
            uint64_t size=0;for(int i=0;i<4;++i)size=(size<<8)|header[i];uint64_t width=8;
            if(size==1) {Require(harvester::RangeReady(at,16,available) && exact(track,at+8,header+8,8),"initialization","Wait for extended MP4 header","waiting_initialization");size=0;for(int i=8;i<16;++i)size=(size<<8)|header[i];width=16;}
            supported(size>=width && size<=1048576,"MP4 initialization exceeds this profile's 1 MiB limit");
            Require(harvester::RangeReady(at,size,available),"initialization","Wait for complete MP4 initialization","waiting_initialization");
            if(!std::memcmp(header+4,"moov",4)) {
                auto& bytes=initialization[track];bytes.resize(size_t(size-width));initOffsets[track]=at+width;
                Require(exact(track,at+width,bytes.data(),bytes.size()),"initialization","Cannot read MP4 initialization","source_not_ready");
                try{codec(track,{reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size()});}
                catch(const Error&){throw;}
                catch(const std::exception&){throw Error("unsupported_media","initialization","Invalid or unsupported fMP4 codec initialization");}
                return;
            }
            supported(std::memcmp(header+4,"mdat",4)!=0 && std::memcmp(header+4,"moof",4)!=0,"Expected a complete head moov before fragments");at+=size;
        }
        throw Error("waiting_initialization","initialization","Wait for complete MP4 initialization");
    }
    void verifyInitialization() {
        for(int i=0;i<2;++i) {std::vector<char> bytes(initialization[i].size());Require(exact(i,initOffsets[i],bytes.data(),bytes.size()) && bytes==initialization[i],"initialization","Published codec initialization changed","source_rewritten");}
    }
public:
    static void CheckProfile(const std::string& json){map(Parse(json));}
    Fmp4V2Bridge(const std::filesystem::path& root,const std::filesystem::path& receipts,const std::string& json):model(Parse(json)),identityGeneration(model.generation),identityWriter(model.writer),detailsRevision(model.revision),registry(receipts) {
        legacy=map(model,&resourceIds);source=std::make_shared<ResourceSource>(root,receipts,json);
        try {initialize(0);initialize(1);}catch(...){cleanup();throw;}
    }
    ~Fmp4V2Bridge(){cleanup();}
    void cleanup() noexcept {
        if(!source)return;
        try {
            const auto ids=source->readerIds();source.reset();
            for(const auto& id:ids)ReaderRequest(registry,"{\"version\":1,\"action\":\"readerForget\",\"readerId\":\""+id+"\"}");
        }catch(...){source.reset();} // Failed acknowledgement retains a conservative receipt.
    }
    harvester::Snapshot snapshot() const {std::lock_guard<std::mutex> lock(mutex);return legacy;}
    std::array<BoundResource,2> bindings() const {std::lock_guard<std::mutex> lock(mutex);return {source->binding(resourceIds[0]),source->binding(resourceIds[1])};}
    bool readIndex(int track,uint64_t offset,void* buffer,size_t count) {std::lock_guard<std::mutex> lock(mutex);return failure.empty() && exact(track,offset,buffer,count);}
    int64_t readPayload(int track,uint64_t offset,void* buffer,size_t count) {
        std::lock_guard<std::mutex> lock(mutex);if(!failure.empty())return -1;
        const auto read=source->read(resourceIds[track],offset,buffer,(std::min)(count,size_t(65536)));
        if(read.data()){const auto size=uint64_t(read.bytes);const auto previous=decoderBytes[track].load();decoderBytes[track].store(size<=UINT64_MAX-previous?previous+size:UINT64_MAX);return int64_t(size);}
        return read.code=="eof"?0:-1;
    }
    void publish(const std::string& json) {
        const auto next=Parse(json);const auto mapped=map(next);std::lock_guard<std::mutex> lock(mutex);Advance(model,next);
        Require(failure.empty(),"mediaProfile","Media resource source previously failed","session_closed");
        try{verifyInitialization();source->publish(json);}catch(const Error& error){failure=error.code;throw;}
        model=next;legacy=mapped;detailsRevision.store(next.revision);
    }
    void verify(){std::lock_guard<std::mutex> lock(mutex);Require(failure.empty(),"mediaProfile","Media resource source previously failed","session_closed");source->verifyPinned();}
    void cancel(){if(source)source->cancel();}
    std::string details() const {
        rapidjson::Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("schemaVersion",2,a);
        d.AddMember("generation",Value(std::to_string(identityGeneration).c_str(),a),a);d.AddMember("revision",Value(std::to_string(detailsRevision.load()).c_str(),a),a);
        d.AddMember("writerInstanceId",Value(identityWriter.c_str(),a),a);
        d.AddMember("mediaMode","separate-fmp4-h264-aac-lc",a);Value bytes(rapidjson::kObjectType);
        bytes.AddMember("video",Value(std::to_string(decoderBytes[0].load()).c_str(),a),a);bytes.AddMember("audio",Value(std::to_string(decoderBytes[1].load()).c_str(),a),a);d.AddMember("decoderIoBytes",bytes,a);
        return lease_detail::Encode(d);
    }
};
}} // namespace foxicmp::progressive
