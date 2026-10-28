// Bounded schema-2 separate fMP4/WebM tracks, including mixed containers. GPL-3.0-or-later.
// Reuses the existing MP4/Matroska demuxers; ALL index and decoder bytes use ResourceSource.
#pragma once
#include "progressiveresourceio.h"
#include "DSUtil/HarvesterMP4.h"
#include <array>

namespace foxicmp { namespace progressive {
inline const char* Fmp4V2Capabilities() {
    return R"({"mode":"separate-fmp4-h264-aac-lc","profiles":["separate-fmp4-h264-aac-lc","separate-fmp4-hevc-aac-lc","separate-fmp4-av1-aac-lc","separate-fmp4-vp9-aac-lc","separate-fmp4-h264-opus","separate-fmp4-hevc-opus","separate-fmp4-av1-opus","separate-fmp4-vp9-opus","separate-webm-vp9-opus","separate-webm-av1-opus","separate-fmp4-webm-h264-opus","separate-fmp4-webm-hevc-opus","separate-fmp4-webm-av1-opus","separate-fmp4-webm-vp9-opus","separate-webm-fmp4-vp9-aac-lc","separate-webm-fmp4-av1-aac-lc","separate-webm-fmp4-vp9-opus","separate-webm-fmp4-av1-opus","separate-webm-vp8-opus","separate-webm-vp8-vorbis","separate-webm-vp9-vorbis","separate-webm-av1-vorbis","separate-fmp4-webm-h264-vorbis","separate-fmp4-webm-hevc-vorbis","separate-fmp4-webm-av1-vorbis","separate-fmp4-webm-vp9-vorbis","separate-webm-fmp4-vp8-aac-lc","separate-webm-fmp4-vp8-opus"],"mixedContainers":true,"vorbisChannels":[1,2],"vorbisSampleRate":48000,"vorbisFloorTypes":[1],"vorbisCodebookEntryLimit":65536,"vorbisSeekPrerollMilliseconds":200,"containers":["fmp4","webm"],"webm":{"videoCodecs":["vp8","vp9","av1"],"audioCodecs":["opus","vorbis"],"knownClusterSizesRequired":true,"lacing":false,"defaultVideoDurationRequired":true,"clusterLimitBytes":16777216},"audioCodecs":["aac-lc","opus","vorbis"],"opusChannels":[1,2],"opusChannelMappingFamilies":[0],"opusSampleRate":48000,"opusPrerollMilliseconds":80,"opusEditListRequired":true,"videoCodecs":["h264","hevc","av1","vp9","vp8"],"vp9Profiles":[0,2],"vp9BitDepths":[8,10],"vp9SampleEntries":["vp09"],"av1Profiles":["main"],"av1BitDepths":[8,10],"av1SampleEntries":["av01"],"av1SequenceHeaderInConfigurationRequired":true,"hevcProfiles":["main","main10"],"hevcBitDepths":[8,10],"hevcSampleEntries":["hvc1","hev1"],"trackCount":2,"separateResourcesRequired":true,"durationRequired":true,"timebase":{"numerator":1,"denominator":10000000},"negativeTimestampOffset":false,"muxed":false,"segments":false,"live":false,"metadataApplied":false,"initializationLimitBytes":1048576})";
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
    std::string videoCodecName,audioCodecName;
    std::array<bool,2> webmMode{};
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
        supported(snapshot.tracks.size()==2 && snapshot.resources.size()==2,"Media v2 currently requires exactly two separate fMP4/WebM resources/tracks");
        harvester::Snapshot result;result.sessionId=snapshot.id;result.generation=snapshot.generation;result.revision=snapshot.revision;
        result.duration=snapshot.duration;result.timestampOffset=snapshot.offset;result.state=state(snapshot.state);
        bool found[2]={false,false};std::array<std::string,2> ids;
        for(const auto& item:snapshot.tracks) {
            const auto& track=item.second;const int index=track.kind=="video"?0:track.kind=="audio"?1:-1;
            supported(index>=0 && !found[index] && track.required && !track.resource.empty() && track.segments.empty() && track.initialization.empty(),
                      "Media v2 currently supports one required video and one required audio track, without segments or external init");
            const bool webm=track.container=="webm";
            supported((webm || track.container=="fmp4") && (track.codec.empty() || (webm?(index?(track.codec=="opus" || track.codec=="vorbis"):track.codec=="vp8" || track.codec=="vp9" || track.codec=="av1"):(index?(track.codec=="aac-lc" || track.codec=="opus"):track.codec=="h264" || track.codec=="hevc" || track.codec=="av1" || track.codec=="vp9"))),"Use a declared separate fMP4 or WebM codec/container profile");
            const auto& resource=snapshot.resources.at(track.resource);supported(!resource.retired,"Cannot start media from a retired resource");
            auto& output=result.tracks[index];output.path=resource.name;output.container=webm?"webm":index?"m4a":"mp4";
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
    static void hevcConfiguration(harvester::BoxData config,bool completeArrays) {
        // HEVCDecoderConfigurationRecord framing; NAL decoding remains in the existing backend.
        supported(config.size>=23 && config.data[0]==1,"Truncated/invalid HEVC decoder configuration");
        const auto* b=config.data;const unsigned profile=b[1]&31;
        supported((b[1]>>6)==0 && (profile==1 || profile==2),"Only HEVC Main/Main10 is enabled");
        supported((b[13]&0xf0)==0xf0 && (b[15]&0xfc)==0xfc && (b[16]&0xfc)==0xfc
                  && (b[17]&0xf8)==0xf8 && (b[18]&0xf8)==0xf8,"Invalid HEVC configuration reserved bits");
        const unsigned luma=b[17]&7,chroma=b[18]&7;
        supported((b[16]&3)==1 && luma==chroma && (luma==0 || (profile==2 && luma==2)),"Only 8/10-bit HEVC 4:2:0 is enabled");
        supported((b[21]&3)==3,"Only four-byte HEVC NAL lengths are enabled");
        supported(b[22]>0 && b[22]<=64,"Invalid HEVC NAL array count");
        size_t at=23;bool parameterSets[3]={false,false,false};
        for(unsigned array=0;array<b[22];++array) {
            supported(at<=config.size && config.size-at>=3,"Truncated HEVC NAL array");
            const auto flags=b[at++];const unsigned type=flags&63;
            supported(!(flags&64) && (type==32 || type==33 || type==34 || type==39 || type==40),"Unsupported HEVC NAL array type");
            const auto count=(unsigned(b[at])<<8)|b[at+1];at+=2;
            supported(count>0 && count<=1024,"Invalid HEVC parameter-set count");
            if(type>=32 && type<=34) {
                supported(!parameterSets[type-32],"Duplicate HEVC parameter-set array");
                supported(!completeArrays || (flags&128),"hvc1 requires complete parameter-set arrays");
                parameterSets[type-32]=true;
            }
            for(unsigned unit=0;unit<count;++unit) {
                supported(at<=config.size && config.size-at>=2,"Truncated HEVC NAL length");
                const auto size=(unsigned(b[at])<<8)|b[at+1];at+=2;
                supported(size>=2 && size<=config.size-at,"HEVC NAL exceeds configuration");
                supported(!(b[at]&128) && ((b[at]>>1)&63)==type && !(b[at]&1)
                          && !(b[at+1]&0xf8) && (b[at+1]&7),"Invalid/multilayer HEVC NAL header");
                at+=size;
            }
        }
        supported(at==config.size && parameterSets[0] && parameterSets[1] && parameterSets[2],"HEVC requires complete VPS/SPS/PPS initialization");
    }
    static void av1Configuration(harvester::BoxData config) {
        harvester::AV1Configuration(config.data,config.size,supported);
    }
    static void vp9Configuration(harvester::BoxData config) {
        // VP Codec ISO BMFF binding: FullBox v1, fixed record, no codec init data.
        supported(config.size==12 && config.U32(0)==0x01000000,"Invalid VP9 configuration length/version/flags");
        const auto* b=config.data;const unsigned profile=b[4],depth=b[6]>>4,chroma=(b[6]>>1)&7;
        supported((profile==0 && depth==8) || (profile==2 && depth==10),"Only VP9 Profile 0/8-bit and Profile 2/10-bit are enabled");
        supported(chroma<=1,"Only VP9 4:2:0 is enabled");
        const auto level=b[5];
        supported(level==0 || level==10 || level==11 || level==20 || level==21 || level==30 || level==31 || level==40 || level==41 || level==50 || level==51 || level==52 || level==60 || level==61 || level==62,"Invalid VP9 level");
        supported(b[7]==1 || b[7]==2 || (b[7]>=4 && b[7]<=12) || b[7]==22,"Reserved VP9 colour primaries");
        supported(b[8]==1 || b[8]==2 || (b[8]>=4 && b[8]<=18),"Reserved VP9 transfer characteristics");
        supported(b[9]==1 || b[9]==2 || (b[9]>=4 && b[9]<=14),"Invalid VP9 matrix for 4:2:0");
        supported(b[10]==0 && b[11]==0,"VP9 codec initialization data must be empty");
    }
    static void opusConfiguration(harvester::BoxData sample,harvester::BoxData config,harvester::BoxData movie) {
        supported(config.size==11 && config.data[0]==0,"Invalid Opus configuration size/version");
        const auto channels=config.data[1];
        supported((channels==1 || channels==2) && config.data[10]==0,"Only Opus mono/stereo mapping family 0 is enabled");
        supported(sample.U32(16)==(uint32_t(channels)<<16 | 16) && sample.U32(24)==(48000u<<16),"Invalid Opus sample-entry channels/size/rate");
        const auto media=movie.One("trak").One("mdia");const auto mdhd=media.One("mdhd");
        supported(mdhd.size>=4 && mdhd.data[0]<=1 && mdhd.U32(mdhd.data[0]?20:12)==48000,"Opus requires a 48 kHz media clock");
        const auto edit=movie.One("trak").One("edts").One("elst");
        supported(edit.size>=8 && edit.data[0]<=1 && !(edit.U32(0)&0xffffff) && edit.U32(4)==1,"Opus requires one media edit");
        const bool wide=edit.data[0]!=0;
        supported(edit.size==(wide?28u:20u),"Invalid Opus edit-list length");
        const auto start=wide?edit.U64(16):uint64_t(edit.U32(12));
        const auto skip=(uint32_t(config.data[2])<<8)|config.data[3];
        supported(start==skip && edit.U32(wide?24:16)==0x00010000,"Opus media edit must match pre-skip with unit rate");
    }
    static std::string codec(int track,harvester::BoxData movie) {
        auto media=movie.One("trak").One("mdia");auto minf=media.One("minf");
        auto dref=minf.One("dinf").One("dref");supported(dref.size>=8 && dref.U32(4)==1,"External media references are not supported");
        auto url=dref.Slice(8,dref.size-8).One("url ");supported(url.size==4 && url.U32(0)==1,"External media references are not supported");
        auto descriptions=minf.One("stbl").One("stsd");supported(descriptions.size>=8 && descriptions.U32(4)==1,"Only one codec configuration is supported");
        auto entries=descriptions.Slice(8,descriptions.size-8).Children();
        supported(entries.size()==1,"Expected exactly one sample entry");
        const auto type=entries.front().first;const auto sample=entries.front().second;
        supported(sample.size>=8 && sample.data[6]==0 && sample.data[7]==1,"Invalid media data reference index");
        if(!track) {
            supported(type==harvester::FourCC("avc1") || type==harvester::FourCC("hvc1") || type==harvester::FourCC("hev1") || type==harvester::FourCC("av01") || type==harvester::FourCC("vp09"),"Unsupported video sample entry");
            supported(sample.size>=78,"Truncated video sample entry");auto children=sample.Slice(78,sample.size-78);
            supported(!children.One("sinf",true).data,"Encrypted video is unsupported");
            if(type==harvester::FourCC("avc1")) {
                auto config=children.One("avcC");
                supported(config.size>=7 && config.data[0]==1,"Invalid AVC initialization");
                return "h264";
            }
            if(type==harvester::FourCC("av01")) {
                av1Configuration(children.One("av1C"));return "av1";
            }
            if(type==harvester::FourCC("vp09")) {
                vp9Configuration(children.One("vpcC"));return "vp9";
            }
            hevcConfiguration(children.One("hvcC"),type==harvester::FourCC("hvc1"));
            return "hevc";
        }else {
            supported(type==harvester::FourCC("mp4a") || type==harvester::FourCC("Opus"),"Unsupported audio sample entry");
            supported(sample.size>=28 && sample.data[8]==0 && sample.data[9]==0,"Only version-zero audio sample entries are supported");
            auto children=sample.Slice(28,sample.size-28);supported(!children.One("sinf",true).data,"Encrypted audio is unsupported");
            if(type==harvester::FourCC("Opus")) {opusConfiguration(sample,children.One("dOps"),movie);return "opus";}
            auto esds=children.One("esds");supported(esds.size>=4,"Invalid AAC initialization");auto es=descriptor(esds.Slice(4,esds.size-4),3);
            supported(es.size>=3 && es.data[2]==0,"External/dependent AAC descriptors are unsupported");auto config=descriptor(es.Slice(3,es.size-3),4);
            supported(config.size>=13 && config.data[0]==0x40 && (config.data[1]>>2)==5,"Expected MPEG-4 AAC");
            auto asc=descriptor(config.Slice(13,config.size-13),5);supported(asc.size>=2 && (asc.data[0]>>3)==2,"Only AAC-LC is enabled in this media profile");
            return "aac-lc";
        }
    }
    void initializeWebm(int track) {
        Require(legacy.tracks[track].initializationReady,"initializationReady","Wait for WebM initialization","waiting_initialization");
        try {
            const auto info=harvester::WebMInitialization([&](uint64_t at,void* data,size_t count){return exact(track,at,data,count);},legacy.tracks[track].available,track);
            for(const auto& item:model.tracks)if(item.second.kind==(track?"audio":"video"))
                supported(item.second.codec.empty() || item.second.codec==info.codec,"Codec hint does not match WebM CodecID");
            if(!track)videoCodecName=info.codec;else audioCodecName=info.codec;
            initOffsets[track]=0;initialization[track].resize(size_t(info.headerEnd));
            Require(exact(track,0,initialization[track].data(),initialization[track].size()),"initialization","Cannot read WebM initialization","source_not_ready");
        }catch(const harvester::WebMWaiting&){throw Error("waiting_initialization","initialization","Wait for complete WebM initialization");}
        catch(const Error&){throw;}catch(const std::exception& e){throw Error("unsupported_media","initialization",e.what());}
    }
    void initialize(int track) {
        if(isWebm(track)){initializeWebm(track);return;}
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
                try{
                    const auto actual=codec(track,{reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size()});
                    for(const auto& item:model.tracks)if(item.second.kind==(track?"audio":"video"))
                        supported(item.second.codec.empty() || item.second.codec==actual,"Codec hint does not match the actual sample entry");
                    if(!track)videoCodecName=actual;else audioCodecName=actual;
                }
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
    const std::string& videoCodec() const {return videoCodecName;}
    const std::string& audioCodec() const {return audioCodecName;}
    bool isWebm(int track=0) const {return webmMode.at(track);}
    static void CheckProfile(const std::string& json){map(Parse(json));}
    Fmp4V2Bridge(const std::filesystem::path& root,const std::filesystem::path& receipts,const std::string& json):model(Parse(json)),identityGeneration(model.generation),identityWriter(model.writer),detailsRevision(model.revision),registry(receipts) {
        legacy=map(model,&resourceIds);for(int i=0;i<2;++i)webmMode[i]=legacy.tracks[i].container=="webm";source=std::make_shared<ResourceSource>(root,receipts,json);
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
        const std::string videoContainer=isWebm(0)?"webm":"fmp4",audioContainer=isWebm(1)?"webm":"fmp4";
        const auto mode="separate-"+videoContainer+(videoContainer==audioContainer?"":"-"+audioContainer)+"-"+videoCodecName+"-"+audioCodecName;
        d.AddMember("videoContainer",Value(videoContainer.c_str(),a),a);d.AddMember("audioContainer",Value(audioContainer.c_str(),a),a);
        d.AddMember("mediaMode",Value(mode.c_str(),a),a);d.AddMember("videoCodec",Value(videoCodecName.c_str(),a),a);d.AddMember("audioCodec",Value(audioCodecName.c_str(),a),a);Value bytes(rapidjson::kObjectType);
        bytes.AddMember("video",Value(std::to_string(decoderBytes[0].load()).c_str(),a),a);bytes.AddMember("audio",Value(std::to_string(decoderBytes[1].load()).c_str(),a),a);d.AddMember("decoderIoBytes",bytes,a);
        return lease_detail::Encode(d);
    }
};
}} // namespace foxicmp::progressive
