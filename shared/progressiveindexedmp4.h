// Ordinary fast-start AVC/AAC MP4 over the confirmed-range scalar bridge.
#pragma once
#include "DSUtil/HarvesterMP4IndexedMux.h"
#include "progressivemuxscalar.h"
namespace foxicmp { namespace progressive {
struct IndexedMp4Profile {
    using Layout=harvester::indexedmux::Layout;
    static constexpr uint64_t MaxAppend=16777216,Tolerance=1;
    static uint64_t Total(const Layout& l,unsigned k,bool complete){return l.Total(k,complete);}
    static bool DecoderRead(const Layout& l,const harvester::FragmentIndex::Read& r,unsigned k,uint64_t at,void* p,size_t n){return l.ReadDecoderAt(r,k,at,p,n);}
    static uint64_t Prefix(const Resource& r){uint64_t end=0;for(const auto& range:r.ranges){Require(range.offset==end,"ranges","Shared media requires a contiguous confirmed prefix","unsupported_media");end+=range.length;}return end;}
    static void Update(Layout& l,const harvester::FragmentIndex::Read& r,uint64_t prefix,bool complete,Layout::Validate validate){l.Update(r,prefix,complete,validate);}
    static const char* Tag(){return "indexedMp4";}
    static const char* Hint(){return "mp4-muxed";}
    static const char* Mode(const std::string&,const std::string&){return "growing-muxed-mp4-h264-aac-lc";}
    static const char* DecoderName(){return "fmp4";}
    static const char* Name(){return "mp4";}
    static const char* Video(){return "h264";}
    static const char* Audio(){return "aac-lc";}
    static bool AcceptsVideo(const std::string& value){return value==Video();}
    static bool AcceptsAudio(const std::string& value){return value==Audio();}
    static Layout::Validate Validation(Layout::Validate validate,const std::string&,const std::string&){return validate;}
    static void CheckResource(const Resource& resource){Require(resource.known && resource.length>0,"indexedMp4","Ordinary MP4 requires a known source length","unsupported_media");}
    static void Configure(Layout& layout,const Resource& resource){layout.SetLength(resource.length);}
    static const char* Path(unsigned k){return k?"audio.fmp4":"video.fmp4";}
    static const char* Container(unsigned k){return k?"m4a":"mp4";}
};
using IndexedMp4Bridge=SharedMuxedBridge<IndexedMp4Profile>;
struct TailMp4Profile:IndexedMp4Profile {
    static const char* Hint(){return "mp4-tail";}
    static const char* Tag(){return "tailMp4";}
    static const char* Mode(const std::string&,const std::string&){return "growing-tail-mp4-h264-aac-lc";}
    static uint64_t Prefix(const Resource& r){Require(r.ranges.size()<=2,"ranges","Tail MP4 accepts a prefix and one metadata range","unsupported_media");uint64_t end=0;for(const auto& range:r.ranges){if(range.offset!=end)break;end+=range.length;}return end;}
    static void Configure(Layout& l,const Resource& r){l.SetLength(r.length);std::vector<std::pair<uint64_t,uint64_t>> ranges;for(const auto& x:r.ranges)ranges.emplace_back(x.offset,x.length);l.SetTailRanges(ranges);}
    static void Update(Layout& l,const harvester::FragmentIndex::Read& r,uint64_t prefix,bool complete,Layout::Validate validate){try{l.Update(r,prefix,complete,validate);}catch(const harvester::mp4index::NeedRange&){throw Error("waiting-index","mp4Index","Wait for confirmed MP4 index metadata");}}
};
using TailMp4Bridge=SharedMuxedBridge<TailMp4Profile>;
struct IndexedMovProfile:TailMp4Profile {
    static const char* Hint(){return "mov";}
    static const char* Tag(){return "indexedMov";}
    static const char* Name(){return "mov";}
    static const char* Mode(const std::string&,const std::string& audio){
        return audio.empty()?"growing-indexed-mov-video":"growing-indexed-mov-aac-lc";
    }
    static bool AcceptsVideo(const std::string& codec){
        return codec=="h264" || codec=="hevc" || codec=="mpeg4-part2";
    }
    static void Configure(Layout& layout,const Resource& resource,const std::string& video,bool solo){
        TailMp4Profile::Configure(layout,resource);
        layout.SetQuickTime(solo,video);
    }
};
using IndexedMovBridge=SharedMuxedBridge<IndexedMovProfile,true>;
// Keep the new response separate from the legacy 64 KiB discovery reply.
inline const char* IndexedMovCapabilities() {
    return R"({"ok":true,"version":1,"containerHint":"mov","profiles":["growing-indexed-mov-video","growing-indexed-mov-aac-lc"],"videoCodecs":["h264","hevc","mpeg4-part2"],"audioCodec":"aac-lc","knownLengthRequired":true,"integrityRequired":true,"headOrConfirmedTailIndex":true,"maxMetadataBytes":1048576,"maxSamplesPerTrack":250000,"maxAppendBytes":16777216,"maxRanges":2})";
}
}}
