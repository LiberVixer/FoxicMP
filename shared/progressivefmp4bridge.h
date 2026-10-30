// Bounded schema-2 separate fMP4/WebM tracks, including mixed containers. GPL-3.0-or-later.
// Reuses the existing MP4/Matroska demuxers; ALL index and decoder bytes use ResourceSource.
#pragma once
#include "progressiveresourceio.h"
#include "progressivesubtitles.h"
#include "progressivesegmentstream.h"
#include "progressivelocalcatalog.h"
#include "DSUtil/HarvesterMP4.h"
#include <array>

namespace foxicmp { namespace progressive {
inline const char* Fmp4V2Capabilities() {
    return R"({"mode":"separate-fmp4-h264-aac-lc","profiles":["segmented-webm-vp8-opus","segmented-webm-vp9-opus","segmented-webm-av1-opus","segmented-fmp4-vp9-aac-lc","segmented-fmp4-h264-opus","segmented-fmp4-hevc-opus","segmented-fmp4-av1-opus","segmented-fmp4-vp9-opus","segmented-fmp4-av1-aac-lc","segmented-fmp4-hevc-aac-lc","segmented-fmp4-h264-aac-lc","separate-fmp4-h264-aac-lc","separate-fmp4-hevc-aac-lc","separate-fmp4-av1-aac-lc","separate-fmp4-vp9-aac-lc","separate-fmp4-h264-opus","separate-fmp4-hevc-opus","separate-fmp4-av1-opus","separate-fmp4-vp9-opus","separate-webm-vp9-opus","separate-webm-av1-opus","separate-fmp4-webm-h264-opus","separate-fmp4-webm-hevc-opus","separate-fmp4-webm-av1-opus","separate-fmp4-webm-vp9-opus","separate-webm-fmp4-vp9-aac-lc","separate-webm-fmp4-av1-aac-lc","separate-webm-fmp4-vp9-opus","separate-webm-fmp4-av1-opus","separate-webm-vp8-opus","separate-webm-vp8-vorbis","separate-webm-vp9-vorbis","separate-webm-av1-vorbis","separate-fmp4-webm-h264-vorbis","separate-fmp4-webm-hevc-vorbis","separate-fmp4-webm-av1-vorbis","separate-fmp4-webm-vp9-vorbis","separate-webm-fmp4-vp8-aac-lc","separate-webm-fmp4-vp8-opus"],"mixedContainers":true,"integrity":{"algorithm":"sha256","maxBlockBytes":4194304,"maxBlocksPerResource":4096,"confirmedRangesCovered":true,"verifiedBeforeDecoder":true,"readChunkBytes":65536,"boundedReadAudit":true},"vorbisChannels":[1,2],"vorbisSampleRate":48000,"vorbisFloorTypes":[1],"vorbisCodebookEntryLimit":65536,"vorbisSeekPrerollMilliseconds":200,"containers":["fmp4","webm"],"webm":{"videoCodecs":["vp8","vp9","av1"],"audioCodecs":["opus","vorbis"],"knownClusterSizesRequired":true,"lacing":false,"defaultVideoDurationRequired":true,"clusterLimitBytes":16777216},"audioCodecs":["aac-lc","opus","vorbis"],"opusChannels":[1,2],"opusChannelMappingFamilies":[0],"opusSampleRate":48000,"opusPrerollMilliseconds":80,"opusEditListRequired":true,"videoCodecs":["h264","hevc","av1","vp9","vp8"],"vp9Profiles":[0,2],"vp9BitDepths":[8,10],"vp9SampleEntries":["vp09"],"av1Profiles":["main"],"av1BitDepths":[8,10],"av1SampleEntries":["av01"],"av1SequenceHeaderInConfigurationRequired":true,"hevcProfiles":["main","main10"],"hevcBitDepths":[8,10],"hevcSampleEntries":["hvc1","hev1"],"trackCount":2,"trackCountMin":2,"trackCountMax":13,"mediaTrackCountMax":9,"audioTrackLimit":8,"multiAudio":true,"subtitles":{"containers":["srt"],"codecs":["srt"],"encoding":"utf-8","trackLimit":4,"completeFileRequired":true,"lateCompletion":true,"optionalErrorIsolation":true,"retainsValidatedCopyOnSourceError":true,"sizeLimitBytes":524288},"audioTracksRequired":false,"optionalAudio":{"completeFileRequired":false,"selectedTrackBuffering":true,"minimumReadySeconds":5,"lateCompletion":true,"pendingErrorIsolation":true,"activeResourceErrorsFatal":false,"activeOptionalAudioErrorsIsolated":true,"requiredPrimaryAudio":true},"metadataApplied":true,"metadata":{"chapters":true,"lateRevision":true,"title":true,"maxChapters":128},"separateResourcesRequired":true,"durationRequired":true,"timebase":{"numerator":1,"denominator":10000000},"negativeTimestampOffset":false,"muxed":false,"segments":true,"segmentedPlayback":{"mode":"segmented-fmp4-h264-aac-lc","profiles":["segmented-fmp4-h264-aac-lc","segmented-fmp4-hevc-aac-lc","segmented-fmp4-av1-aac-lc","segmented-fmp4-vp9-aac-lc","segmented-fmp4-h264-opus","segmented-fmp4-hevc-opus","segmented-fmp4-av1-opus","segmented-fmp4-vp9-opus"],"videoCodecs":["h264","hevc","av1","vp9"],"hevcProfiles":["main","main10"],"hevcBitDepths":[8,10],"hevcSampleEntries":["hvc1","hev1"],"completeSegmentsRequired":true,"discontinuity":false,"configurationChanges":false,"timestampRebasing":false,"live":false,"trackCount":2,"maxInitBytes":1048576,"maxSegmentBytes":16777216,"av1Profiles":["main"],"av1BitDepths":[8,10],"av1SampleEntries":["av01"],"av1SequenceHeaderInConfigurationRequired":true,"vp9Profiles":[0,2],"vp9BitDepths":[8,10],"vp9SampleEntries":["vp09"],"audioCodecs":["aac-lc","opus"],"opusChannels":[1,2],"opusChannelMappingFamilies":[0],"opusSampleRate":48000,"opusPrerollMilliseconds":80,"opusEditListRequired":true,"catalog":{"mode":"paged-fmp4-v1","maxPages":128,"maxPageBytes":65536,"maxResources":8192,"maxSegmentsPerTrack":4096,"maxOpenResources":8,"integrityRequired":true,"pageIntegrity":"sha256","publishedPagesImmutable":true,"retainAllPublishedResourcesUntilRelease":true,"validation":"probe-with-sessionDirectory"},"webm":{"mode":"segmented-webm-vp9-opus","profiles":["segmented-webm-vp8-opus","segmented-webm-vp9-opus","segmented-webm-av1-opus"],"videoCodecs":["vp8","vp9","av1"],"audioCodecs":["opus"],"vp9Profiles":[0,2],"vp9BitDepths":[8,10],"av1Profiles":["main"],"av1BitDepths":[8,10],"opusChannels":[1,2],"opusChannelMappingFamilies":[0],"opusSampleRate":48000,"opusPrerollMilliseconds":80,"timestampScaleNanoseconds":1000000,"maxTimelineGapTicks":10000,"externalInitializationRequired":true,"unknownSegmentSizeRequired":true,"completeClustersRequired":true,"videoSegmentStartsWithKeyframe":true,"lacing":false,"discontinuity":false,"configurationChanges":false,"timestampRebasing":false,"live":false,"trackCount":2,"maxInitBytes":1048576,"maxSegmentBytes":16777216,"maxSamplesPerSegment":65536,"catalog":{"mode":"paged-webm-v1","maxPages":128,"maxPageBytes":65536,"maxResources":8192,"maxSegmentsPerTrack":4096,"maxOpenResources":8,"integrityRequired":true,"pageIntegrity":"sha256","publishedPagesImmutable":true,"retainAllPublishedResourcesUntilRelease":true,"validation":"probe-with-sessionDirectory"}}},"live":false,"initializationLimitBytes":1048576})";
}
class Fmp4V2Bridge {
    std::unique_ptr<LocalSegmentCatalog> catalog;
    Snapshot model;
    const uint64_t identityGeneration;
    const std::string identityWriter;
    std::atomic<uint64_t> detailsRevision;
    harvester::Snapshot legacy;
    std::shared_ptr<ResourceSource> source;
    std::filesystem::path registry;
    std::array<std::string,harvester::MaxMediaTracks> resourceIds,trackIds,trackCodecs;
    std::array<std::vector<char>,harvester::MaxMediaTracks> initialization;
    std::array<uint64_t,harvester::MaxMediaTracks> initOffsets{};
    std::atomic<uint64_t> decoderBytes[harvester::MaxMediaTracks]{};
    std::string failure;
    std::array<std::string,harvester::MaxMediaTracks> audioErrors,audioErrorCodes;
    std::array<bool,harvester::MaxMediaTracks> optionalFailed{};
    std::string videoCodecName,audioCodecName;
    std::array<bool,harvester::MaxMediaTracks> webmMode{};
    bool segmented=false;
    std::array<SegmentLayout,harvester::MaxMediaTracks> segmentLayouts;
    std::set<std::string> validatedSegments;
    struct Subtitle {std::string id,resource,label,language,bytes,path,errorCode,error;bool defaultTrack=false,forced=false;};
    std::map<std::string,Subtitle> subtitleTracks;
    std::unique_ptr<SubtitleCache> subtitleCache;
    mutable std::mutex mutex;
    std::mutex publicationMutex;
    static bool catalogDeclared(const std::string& json) {
        return catalog_detail::Json(json,harvester::MaxManifestBytes,"$").HasMember("segmentCatalog");
    }
    static std::unique_ptr<LocalSegmentCatalog> openCatalog(const std::filesystem::path& root,const std::string& json) {
        if(!catalogDeclared(json))return {};
        auto value=std::make_unique<LocalSegmentCatalog>(root);value->publish(json);return value;
    }
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
    static bool segmentProfile(const Snapshot& snapshot) {
        for(const auto& item:snapshot.tracks)if((item.second.kind=="video" || item.second.kind=="audio") && item.second.resource.empty())return true;
        return false;
    }
    static harvester::Snapshot mapSegments(const Snapshot& snapshot,std::array<std::string,harvester::MaxMediaTracks>* resources,
                                           std::array<std::string,harvester::MaxMediaTracks>* tracks) {
        supported(!snapshot.live && !snapshot.hasWindow && snapshot.knownDuration && snapshot.numerator==1 && snapshot.denominator==10000000 && snapshot.offset==0,
                  "Segment playback requires VOD with a known duration, 100 ns timebase and zero session offset");
        supported(snapshot.tracks.size()==2,"Segment playback currently requires exactly one video and one audio track");
        const Track* video=nullptr;const Track* audio=nullptr;
        for(const auto& item:snapshot.tracks) {
            if(item.second.kind=="video"){supported(!video,"Only one segmented video track is enabled");video=&item.second;}
            else if(item.second.kind=="audio"){supported(!audio,"Only one segmented audio track is enabled");audio=&item.second;}
        }
        supported(video && audio,"Both segmented video and audio tracks are required");
        supported(video->container==audio->container,"Segmented tracks must use the same container");
        harvester::Snapshot result;result.tracks.resize(2);result.sessionId=snapshot.id;result.generation=snapshot.generation;result.revision=snapshot.revision;
        result.duration=snapshot.duration;result.timestampOffset=0;result.state=state(snapshot.state);std::set<std::string> used;
        const Track* ordered[2]={video,audio};
        for(size_t i=0;i<2;++i) {
            const auto& track=*ordered[i];const auto layout=BuildSegmentLayout(snapshot,track);
            supported(used.insert(layout.initialization).second,"Segmented tracks need independent initialization resources");
            for(const auto& segment:track.segments)supported(used.insert(segment.resource).second,"Segmented tracks need distinct media resources");
            auto& output=result.tracks[i];output.path=snapshot.resources.at(layout.initialization).name;output.container=track.container=="webm"?"webm":i?"m4a":"mp4";
            output.required=true;output.fragmented=true;output.initializationReady=layout.initialized;output.available=layout.available;
            output.total=layout.complete?layout.available:0;output.state=state(track.state);
            if(resources)(*resources)[i]=layout.initialization;if(tracks)(*tracks)[i]=track.id;
        }
        supported(used.size()==snapshot.resources.size(),"Segment playback accepts only resources referenced by its two tracks");
        return result;
    }
    static harvester::Snapshot map(const Snapshot& snapshot,std::array<std::string,harvester::MaxMediaTracks>* resources=nullptr,std::array<std::string,harvester::MaxMediaTracks>* tracks=nullptr) {
        if(segmentProfile(snapshot))return mapSegments(snapshot,resources,tracks);
        supported(!snapshot.live && !snapshot.hasWindow && snapshot.knownDuration && snapshot.numerator==1 && snapshot.denominator==10000000 && snapshot.offset>=0,
                  "Media v2 requires VOD, known duration, 100 ns timebase and nonnegative offset");
        supported(snapshot.tracks.size()>=2 && snapshot.tracks.size()<=harvester::MaxMediaTracks+4 && snapshot.resources.size()==snapshot.tracks.size(),"Use one video and one to eight separate audio resources");
        std::vector<const Track*> ordered;const Track* video=nullptr;unsigned defaults=0,subtitles=0;std::set<std::string> resourcesUsed;
        for(const auto& item:snapshot.tracks){const auto& t=item.second;
            supported(!t.resource.empty() && t.segments.empty() && t.initialization.empty() && resourcesUsed.insert(t.resource).second,"Each track needs a distinct resource without segments/external init");
            if(t.kind=="subtitle"){supported(++subtitles<=4 && !t.required && t.container=="srt" && (t.codec.empty() || t.codec=="srt") && snapshot.offset==0,"Only optional UTF-8 SRT at zero timestamp offset is enabled");continue;}
            supported(t.kind=="video" || t.kind=="audio","Unsupported track kind");
            if(t.kind=="video"){supported(!video,"Only one video track is enabled");video=&t;}
            else if(t.defaultTrack)++defaults;
        }
        supported(video && video->required && defaults<=1,"Required video and at most one default audio track are needed");ordered.push_back(video);
        for(const auto& item:snapshot.tracks)if(item.second.kind=="audio" && item.second.defaultTrack){supported(item.second.required,"The default audio track must be required");ordered.push_back(&item.second);}
        for(const auto& item:snapshot.tracks)if(item.second.kind=="audio" && item.second.required && !item.second.defaultTrack)ordered.push_back(&item.second);
        supported(ordered.size()>=2,"At least one required primary audio track is needed");
        for(const auto& item:snapshot.tracks)if(item.second.kind=="audio" && !item.second.required)ordered.push_back(&item.second);
        supported(ordered.size()>=2 && ordered.size()<=harvester::MaxMediaTracks,"One video and one to eight audio tracks are required");
        harvester::Snapshot result;result.tracks.resize(ordered.size());result.sessionId=snapshot.id;result.generation=snapshot.generation;result.revision=snapshot.revision;
        result.duration=snapshot.duration;result.timestampOffset=snapshot.offset;result.state=state(snapshot.state);std::set<std::string> ids;
        for(size_t index=0;index<ordered.size();++index){const auto& track=*ordered[index];
            supported(!track.resource.empty() && track.segments.empty() && track.initialization.empty(),"All media tracks must be separate; segments/external init are unsupported");
            const bool webm=track.container=="webm";
            supported((webm || track.container=="fmp4") && (track.codec.empty() || (webm?(index?(track.codec=="opus" || track.codec=="vorbis"):track.codec=="vp8" || track.codec=="vp9" || track.codec=="av1"):(index?(track.codec=="aac-lc" || track.codec=="opus"):track.codec=="h264" || track.codec=="hevc" || track.codec=="av1" || track.codec=="vp9"))),"Unsupported codec/container profile");
            const auto& resource=snapshot.resources.at(track.resource);supported(!resource.retired && ids.insert(resource.id).second,"Tracks need distinct non-retired resources");
            auto& output=result.tracks[index];output.path=resource.name;output.container=webm?"webm":index?"m4a":"mp4";
            output.required=track.required;output.fragmented=true;output.initializationReady=track.initializationReady;output.available=prefix(resource);output.total=resource.known?resource.length:0;output.state=state(track.state);
            if(resources)(*resources)[index]=resource.id;if(tracks)(*tracks)[index]=track.id;
        }
        return result;
    }
    ResourceRead resourceRead(int track,uint64_t offset,void* buffer,size_t count) {
        return segmented?ReadSegmentStream(*source,segmentLayouts[track],offset,buffer,count):source->read(resourceIds[track],offset,buffer,count);
    }
    bool exact(int track,uint64_t offset,void* buffer,size_t count) {
        auto* out=static_cast<char*>(buffer);
        while(count) {
            const auto size=(std::min)(count,size_t(65536));const auto read=resourceRead(track,offset,out,size);
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
            const auto info=harvester::WebMInitialization([&](uint64_t at,void* data,size_t count){return exact(track,at,data,count);},segmented?model.resources.at(resourceIds[track]).length:legacy.tracks[track].available,track?1:0,segmented);
            const auto& declared=model.tracks.at(trackIds[track]);
            supported(declared.codec.empty() || declared.codec==info.codec,"Codec hint does not match WebM CodecID");
            trackCodecs[track]=info.codec;if(!track)videoCodecName=info.codec;else if(track==1)audioCodecName=info.codec;
            initOffsets[track]=0;initialization[track].resize(size_t(info.headerEnd));
            Require(exact(track,0,initialization[track].data(),initialization[track].size()),"initialization","Cannot read WebM initialization","source_not_ready");
        }catch(const harvester::WebMWaiting&){throw Error(segmented?"unsupported_media":"waiting_initialization","initialization",segmented?"Truncated completed WebM initialization":"Wait for complete WebM initialization");}
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
                    const auto& declared=model.tracks.at(trackIds[track]);
                    supported(declared.codec.empty() || declared.codec==actual,"Codec hint does not match the actual sample entry");
                    trackCodecs[track]=actual;if(!track)videoCodecName=actual;else if(track==1)audioCodecName=actual;
                }
                catch(const Error&){throw;}
                catch(const std::exception&){throw Error("unsupported_media","initialization","Invalid or unsupported fMP4 codec initialization");}
                return;
            }
            supported(std::memcmp(header+4,"mdat",4)!=0 && std::memcmp(header+4,"moof",4)!=0,"Expected a complete head moov before fragments");at+=size;
        }
        throw Error("waiting_initialization","initialization","Wait for complete MP4 initialization");
    }
    static void subtitleError(Subtitle& out,const std::string& code,const std::string& reason) {
        if(out.errorCode.empty()){out.errorCode=code;out.error=reason;}
    }
    void subtitleResourceErrors() {
        for(auto& item:subtitleTracks) {
            const auto error=source->resourceError(item.second.resource);
            if(!error.code.empty())subtitleError(item.second,error.code,error.reason);
        }
    }
    void prepareSubtitles(const Snapshot& next) {
        for(const auto& item:next.tracks){const auto& track=item.second;if(track.kind!="subtitle")continue;
            auto& out=subtitleTracks[track.id];out.id=track.id;out.resource=track.resource;out.label=track.label;out.language=track.language;out.defaultTrack=track.defaultTrack;out.forced=track.forced;
            if(!out.errorCode.empty())continue; // Immutable published resource: do not retry bad bytes.
            const auto& resource=next.resources.at(track.resource);
            if(track.state=="failed" || track.state=="cancelled" || resource.state=="failed" || resource.state=="cancelled" || resource.retired) {
                subtitleError(out,"resource-failed","Optional SRT failed, was cancelled or retired");continue;
            }
            if(track.state!="complete" || resource.state!="complete")continue;
            try {
                Require(track.initializationReady && resource.known && resource.length>0 && resource.length<=524288 && prefix(resource)==resource.length,"subtitles","SRT requires a complete bounded resource","unsupported_media");
                std::string bytes(size_t(resource.length),'\0');size_t at=0;
                while(at<bytes.size()) {
                    const auto result=source->read(resource.id,at,&bytes[at],(std::min)(size_t(65536),bytes.size()-at));
                    if(!result.data() || !result.bytes)throw Error(result.code.empty()?"source_not_ready":result.code.c_str(),"subtitles",result.reason.empty()?"Cannot read complete SRT":result.reason.c_str());
                    at+=result.bytes;
                }
                if(!out.path.empty()){Require(out.bytes==bytes,"subtitles","Published SRT bytes changed","source_rewritten");continue;}
                const auto checked=CheckedSrt(bytes,next.duration);
                if(!subtitleCache)subtitleCache=std::make_unique<SubtitleCache>(registry);
                out.path=subtitleCache->write(checked).u8string();out.bytes=std::move(bytes);
            }catch(const Error& error) {
                // Parsing, local I/O and materialization errors belong to this
                // optional track. Required A/V and snapshot validation stay strict.
                if(error.code=="cancelled")throw;
                subtitleError(out,error.code,error.what());
            }
        }
        subtitleResourceErrors();
    }
    void audioFailure(size_t i,const std::string& code,const std::string& reason) {
        if(audioErrorCodes[i].empty()){audioErrorCodes[i]=code;audioErrors[i]=reason;}
        optionalFailed[i]=true;
        legacy.tracks[i].initializationReady=false;
    }
    void markOptionalFailures() {
        for(size_t i=1;i<legacy.tracks.size();++i) {
            if(legacy.tracks[i].required || optionalFailed[i])continue;
            const auto& track=model.tracks.at(trackIds[i]);
            const auto& resource=model.resources.at(resourceIds[i]);
            if(track.state=="failed" || track.state=="cancelled" || resource.state=="failed" || resource.state=="cancelled")
                audioFailure(i,"resource-failed","Optional audio writer failed or cancelled");
            const auto error=source->resourceError(resourceIds[i]);
            if(!error.code.empty())audioFailure(i,error.code,error.reason);
        }
    }
    void prepareAudio() {
        markOptionalFailures();
        for(size_t i=1;i<legacy.tracks.size();++i) {
            auto& out=legacy.tracks[i];if(out.required)continue;
            out.initializationReady=false;
            if(optionalFailed[i] || !audioErrorCodes[i].empty())continue;
            if(!initialization[i].empty()){out.initializationReady=true;continue;}
            const auto& track=model.tracks.at(trackIds[i]);const auto& resource=model.resources.at(resourceIds[i]);
            if(!track.initializationReady)continue;
            const bool complete=track.state=="complete" && resource.state=="complete";
            try {
                Require(!complete || (resource.known && resource.length && prefix(resource)==resource.length),"optionalAudio","Complete audio needs a bounded resource","source_not_ready");
                out.initializationReady=true;initialize(int(i));
                harvester::FragmentIndex index;
                index.Update([&](uint64_t at,void* data,size_t count){return exact(int(i),at,data,count);},out.available,1,complete,legacy.timestampOffset,false,false,false,trackCodecs[i]=="opus",isWebm(int(i)));
                const auto end=harvester::ReadyEnd(index.Samples(),out.available,legacy.duration);
                if(!complete && end<50000000) {
                    initialization[i].clear();trackCodecs[i].clear();out.initializationReady=false;continue;
                }
                Require(!index.Samples().empty(),"optionalAudio","Optional audio contains no validated packets","unsupported_media");
                source->activateMedia(resourceIds[i]);
            }catch(const std::exception& error) {
                const auto* typed=dynamic_cast<const Error*>(&error);if(typed && typed->code=="cancelled")throw;
                const bool waiting=!complete && typed && (typed->code=="waiting_initialization" || typed->code=="source_not_ready");
                if(!waiting)audioFailure(i,typed?typed->code:"unsupported_media",error.what());
                initialization[i].clear();trackCodecs[i].clear();out.initializationReady=false;
            }
        }
    }
    void verifyInitialization() {
        for(size_t i=0;i<legacy.tracks.size();++i) {
            if(initialization[i].empty() || optionalFailed[i])continue;
            std::vector<char> bytes(initialization[i].size());
            if(!exact(int(i),initOffsets[i],bytes.data(),bytes.size()) || bytes!=initialization[i]) {
                if(legacy.tracks[i].required)throw Error("source_rewritten","initialization","Published codec initialization changed");
                const auto error=source->resourceError(resourceIds[i]);
                audioFailure(i,error.code.empty()?"source_rewritten":error.code,error.reason.empty()?"Published codec initialization changed":error.reason);
            }
        }
    }
public:
    const std::string& videoCodec() const {return videoCodecName;}
    const std::string& audioCodec(int track=1) const {return trackCodecs.at(track);}
    bool isWebm(int track=0) const {return webmMode.at(track);}
    bool isSegmented() const {return segmented;}
    bool usesCatalog() const {return bool(catalog);}
    static void CheckProfile(const std::string& json) {
        if(!catalogDeclared(json)){map(Parse(json));return;}
        // Metadata-only admission on UI: reuse the decoder's complete envelope
        // checks, stopping exactly at its first page request. Worker preparation
        // subsequently validates every page and the actual media profile.
        struct PagesDeferred {};
        try {
            auto value=DecodeSegmentCatalog(json,[](const SegmentCatalogPage&)->std::string{throw PagesDeferred{};});
            map(value.snapshot); // Empty catalog has no external pages to defer.
        }catch(const PagesDeferred&){}
    }
    Fmp4V2Bridge(const std::filesystem::path& root,const std::filesystem::path& receipts,const std::string& json):catalog(openCatalog(root,json)),model(catalog?catalog->catalog().snapshot:Parse(json)),identityGeneration(model.generation),identityWriter(model.writer),detailsRevision(model.revision),registry(receipts) {
        legacy=map(model,&resourceIds,&trackIds);segmented=segmentProfile(model);
        supported(!catalog || segmented,"Catalog playback requires segmented tracks");
        for(size_t i=0;i<legacy.tracks.size();++i){webmMode[i]=legacy.tracks[i].container=="webm";if(segmented)segmentLayouts[i]=BuildSegmentLayout(model,model.tracks.at(trackIds[i]));}
        source=std::make_shared<ResourceSource>(root,receipts,catalog?catalog->catalog().expandedJson:json,ResourceFailurePolicy::IsolateOptionalAudioAndSrt,
            catalog?ResourceMode::BoundedCatalog:ResourceMode::Inline);
        try {for(size_t i=0;i<legacy.tracks.size();++i)if(legacy.tracks[i].required)initialize(int(i));
             if(segmented)ValidateSegmentMedia(model,trackIds,segmentLayouts,source,validatedSegments);
             prepareAudio();prepareSubtitles(model);}catch(...){cleanup();throw;}
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
    std::vector<BoundResource> bindings() const {std::lock_guard<std::mutex> lock(mutex);std::vector<BoundResource> out;for(size_t i=0;i<legacy.tracks.size();++i)if(legacy.tracks[i].initializationReady)out.push_back(source->binding(resourceIds[i]));return out;}
    bool optionalTrackFailed(int track) const {std::lock_guard<std::mutex> lock(mutex);return track>=0 && size_t(track)<legacy.tracks.size() && optionalFailed[track];}
    void rejectOptionalIndex(int track,const std::string& reason) {
        std::lock_guard<std::mutex> lock(mutex);
        Require(track>0 && size_t(track)<legacy.tracks.size() && !legacy.tracks[track].required,"audio","Cannot isolate a required track","unsupported_media");
        audioFailure(size_t(track),"invalid-media-index",reason);
    }
    bool readIndex(int track,uint64_t offset,void* buffer,size_t count) {
        std::lock_guard<std::mutex> lock(mutex);
        if(!failure.empty() || !legacy.tracks.at(track).initializationReady || optionalFailed[track])return false;
        const bool result=exact(track,offset,buffer,count);markOptionalFailures();return result;
    }
    int64_t readPayload(int track,uint64_t offset,void* buffer,size_t count) {
        std::lock_guard<std::mutex> lock(mutex);
        if(!failure.empty() || !legacy.tracks.at(track).initializationReady || optionalFailed[track])return -1;
        const auto read=resourceRead(track,offset,buffer,(std::min)(count,size_t(65536)));
        if(read.data()){const auto size=uint64_t(read.bytes);const auto previous=decoderBytes[track].load();decoderBytes[track].store(size<=UINT64_MAX-previous?previous+size:UINT64_MAX);return int64_t(size);}
        if(!legacy.tracks[track].required && read.code!="cancelled" && read.code!="eof" && read.code!="waiting-range" && read.code!="waiting-resource")
            audioFailure(size_t(track),read.code.empty()?"resource-io-error":read.code,read.reason.empty()?"Optional audio resource failed":read.reason);
        return read.code=="eof"?0:-1;
    }
    void publish(const std::string& json) {
        std::lock_guard<std::mutex> publication(publicationMutex);
        supported(bool(catalog)==catalogDeclared(json),"Catalog/inline mode cannot change within a session");
        LocalSegmentCatalog::Prepared prepared;
        if(catalog)prepared=catalog->prepare(json); // Filesystem work outside the status/backend mutex.
        const auto next=catalog?prepared.value->snapshot:Parse(json);
        std::array<std::string,harvester::MaxMediaTracks> nextIds;const auto mapped=map(next,nullptr,&nextIds);std::lock_guard<std::mutex> lock(mutex);
        supported(nextIds==trackIds,"Media track set/default ordering cannot change; reopen playback");Advance(model,next);
        Require(failure.empty(),"mediaProfile","Media resource source previously failed","session_closed");
        try{verifyInitialization();source->publish(catalog?prepared.value->expandedJson:json);
            if(segmented) {
                auto layouts=segmentLayouts;for(size_t i=0;i<2;++i) {
                    layouts[i]=BuildSegmentLayout(next,next.tracks.at(trackIds[i]));
                    supported(layouts[i].initialization==segmentLayouts[i].initialization,"Segment initialization cannot change during playback");
                }
                ValidateSegmentMedia(next,trackIds,layouts,source,validatedSegments);segmentLayouts=std::move(layouts);
            }
            model=next;legacy=mapped;prepareAudio();prepareSubtitles(next);
            if(catalog)catalog->commit(std::move(prepared));}catch(const Error& error){failure=error.code;throw;}
        detailsRevision.store(next.revision);
    }
    void verify(){std::lock_guard<std::mutex> lock(mutex);Require(failure.empty(),"mediaProfile","Media resource source previously failed","session_closed");source->verifyPinned();markOptionalFailures();subtitleResourceErrors();}
    void cancel(){if(catalog)catalog->cancel();if(source)source->cancel();}
    std::string details() const {
        std::lock_guard<std::mutex> lock(mutex);
        rapidjson::Document d;d.SetObject();auto& a=d.GetAllocator();d.AddMember("schemaVersion",2,a);
        d.AddMember("generation",Value(std::to_string(identityGeneration).c_str(),a),a);d.AddMember("revision",Value(std::to_string(detailsRevision.load()).c_str(),a),a);
        d.AddMember("writerInstanceId",Value(identityWriter.c_str(),a),a);
        if(catalog) {
            Value info(rapidjson::kObjectType);info.AddMember("mode",Value(isWebm()?"paged-webm-v1":"paged-fmp4-v1",a),a);
            info.AddMember("pageCount",unsigned(catalog->catalog().pages.size()),a);
            info.AddMember("resourceCount",unsigned(model.resources.size()),a);
            info.AddMember("maxOpenResources",unsigned(MaxCatalogOpenResources),a);
            info.AddMember("integrityRequired",true,a);d.AddMember("segmentCatalog",info,a);
        }
        if(!model.metadata.empty()) {
            rapidjson::Document metadata;
            metadata.Parse(model.metadata.c_str());
            if(!metadata.HasParseError() && metadata.IsObject()) {
                rapidjson::Value copy(metadata, a);
                d.AddMember("metadata", copy, a);
            }
        }
        const std::string videoContainer=isWebm(0)?"webm":"fmp4",audioContainer=isWebm(1)?"webm":"fmp4";
        const auto mode=(segmented?"segmented-":"separate-")+videoContainer+(videoContainer==audioContainer?"":"-"+audioContainer)+"-"+videoCodecName+"-"+audioCodecName;
        d.AddMember("segments",segmented,a);
        d.AddMember("videoContainer",Value(videoContainer.c_str(),a),a);d.AddMember("audioContainer",Value(audioContainer.c_str(),a),a);
        d.AddMember("mediaMode",Value(mode.c_str(),a),a);d.AddMember("videoCodec",Value(videoCodecName.c_str(),a),a);d.AddMember("audioCodec",Value(audioCodecName.c_str(),a),a);Value bytes(rapidjson::kObjectType);
        bytes.AddMember("video",Value(std::to_string(decoderBytes[0].load()).c_str(),a),a);bytes.AddMember("audio",Value(std::to_string(decoderBytes[1].load()).c_str(),a),a);d.AddMember("decoderIoBytes",bytes,a);
        Value tracks(rapidjson::kArrayType);
        for(size_t i=0;i<legacy.tracks.size();++i){const auto& t=model.tracks.at(trackIds[i]);Value item(rapidjson::kObjectType);
            item.AddMember("index",unsigned(i),a);item.AddMember("id",Value(t.id.c_str(),a),a);item.AddMember("kind",Value(t.kind.c_str(),a),a);item.AddMember("label",Value(t.label.c_str(),a),a);item.AddMember("language",Value(t.language.c_str(),a),a);item.AddMember("codec",Value(trackCodecs[i].c_str(),a),a);item.AddMember("container",Value(t.container.c_str(),a),a);item.AddMember("default",t.defaultTrack,a);item.AddMember("required",t.required,a);item.AddMember("ready",legacy.tracks[i].initializationReady,a);
            item.AddMember("state",Value(audioErrorCodes[i].empty()?(legacy.tracks[i].initializationReady?"ready":"waiting"):"error",a),a);
            item.AddMember("errorCode",Value(audioErrorCodes[i].c_str(),a),a);item.AddMember("error",Value(audioErrors[i].c_str(),a),a);item.AddMember("decoderIoBytes",Value(std::to_string(decoderBytes[i].load()).c_str(),a),a);tracks.PushBack(item,a);
        }
        d.AddMember("mediaTracks",tracks,a);Value subtitles(rapidjson::kArrayType);
        for(const auto& entry:subtitleTracks){const auto& t=entry.second;Value item(rapidjson::kObjectType);item.AddMember("id",Value(t.id.c_str(),a),a);item.AddMember("label",Value(t.label.c_str(),a),a);item.AddMember("language",Value(t.language.c_str(),a),a);item.AddMember("path",Value(t.path.c_str(),a),a);item.AddMember("ready",!t.path.empty(),a);item.AddMember("state",Value(t.errorCode.empty()?(t.path.empty()?"waiting":"ready"):(t.path.empty()?"error":"cached"),a),a);item.AddMember("errorCode",Value(t.errorCode.c_str(),a),a);item.AddMember("error",Value(t.error.c_str(),a),a);item.AddMember("default",t.defaultTrack,a);item.AddMember("forced",t.forced,a);subtitles.PushBack(item,a);}d.AddMember("subtitleTracks",subtitles,a);return lease_detail::Encode(d);
    }
};
}} // namespace foxicmp::progressive
