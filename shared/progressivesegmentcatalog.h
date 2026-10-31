// Bounded, filesystem-free decoder for immutable segment catalog pages.
// The catalog playback capability is negotiated separately from inline schema 2.
// This layer performs no filesystem or backend work. GPL-3.0-or-later.
#pragma once
#include "progressivesession.h"
#include "progressivesha256.h"

namespace foxicmp { namespace progressive {
constexpr size_t MaxCatalogPages=128, MaxCatalogPageBytes=65536;
struct SegmentCatalogPage {
    std::string id,name,sha256;size_t length=0;
    bool operator==(const SegmentCatalogPage& b) const {
        return id==b.id && name==b.name && sha256==b.sha256 && length==b.length;
    }
};
struct SegmentCatalog {
    Snapshot snapshot;
    std::vector<SegmentCatalogPage> pages;
    std::string expandedJson;
};
namespace catalog_detail {
inline rapidjson::Document Json(const std::string& text,size_t limit,const char* path) {
    Require(!text.empty() && text.size()<=limit && text.find('\0')==std::string::npos,path,"Catalog JSON exceeds its byte limit or contains a null byte");
    rapidjson::Document d;d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(text.data(),text.size());
    Require(!d.HasParseError() && d.IsObject(),path,"Invalid UTF-8 catalog JSON object");
    try{harvester::UniqueKeys(d);}catch(const std::exception& e){throw Error("invalid_snapshot",path,e.what());}
    return d;
}
inline std::string FilenameKey(std::string name) {
    for(auto& c:name)if(c>='A' && c<='Z')c+='a'-'A';return name;
}
} // namespace catalog_detail

// The adapter supplies ONLY the named local page, bounded to descriptor.length.
// This pure decoder never opens a file, follows a path, or downloads anything.
// Each payload is hashed again here, independently of any adapter cache.
template<class Loader> SegmentCatalog DecodeSegmentCatalog(const std::string& json,Loader&& load) {
    auto root=catalog_detail::Json(json,harvester::MaxManifestBytes,"$");
    SegmentCatalog result;
    if(!root.HasMember("segmentCatalog")) {
        result.snapshot=Parse(json);result.expandedJson=json;return result;
    }
    Keys(root,"$",{"schemaVersion","sessionId","generation","revision","writer","timeline","resources","tracks","segmentCatalog"},{"metadata"});
    Require(root["schemaVersion"].IsUint() && root["schemaVersion"].GetUint()==2,"$.schemaVersion","Expected schema 2","unsupported_schema");
    const auto session=Id(root["sessionId"],"$.sessionId",true);
    const auto generation=Unsigned(root["generation"],"$.generation",UINT64_MAX,true);
    Keys(root["writer"],"$.writer",{"instanceId","state"});
    const auto writer=Id(root["writer"]["instanceId"],"$.writer.instanceId",true);
    const auto& descriptors=root["segmentCatalog"];
    Require(descriptors.IsArray() && descriptors.Size()<=MaxCatalogPages,"$.segmentCatalog","Too many catalog pages");
    Require(root["resources"].IsArray() && root["resources"].Size()<=MaxResources,"$.resources","Too many inline resources");
    Require(root["tracks"].IsArray() && root["tracks"].Size()==2,"$.tracks","Catalog requires two segmented media tracks");
    // Only the explicit muxed TS profile may describe two logical views over
    // one physical resource. Other catalog profiles retain exclusive ownership.
    bool muxedTs=true;std::set<std::string> muxKinds;
    for(const auto& t:root["tracks"].GetArray()) {
        muxedTs=muxedTs && t.IsObject() && t.HasMember("containerHint") && t["containerHint"].IsString() && std::string(t["containerHint"].GetString(),t["containerHint"].GetStringLength())=="mpegts-muxed";
        if(t.IsObject() && t.HasMember("kind") && t["kind"].IsString())muxKinds.insert(std::string(t["kind"].GetString(),t["kind"].GetStringLength()));
    }
    muxedTs=muxedTs && muxKinds==std::set<std::string>{"video","audio"};
    auto& allocator=root.GetAllocator();
    std::map<std::string,size_t> tracks;std::map<std::string,Value> segments;
    for(rapidjson::SizeType i=0;i<root["tracks"].Size();++i) {
        auto& track=root["tracks"][i];Require(track.IsObject() && track.HasMember("id") && track.HasMember("source"),"$.tracks","Invalid catalog track");
        const auto id=Id(track["id"],"$.tracks[].id");
        Require(tracks.emplace(id,i).second,"$.tracks","Duplicate catalog track");
        Keys(track["source"],"$.tracks[].source",{"segments"});
        Require(track["source"]["segments"].IsArray() && track["source"]["segments"].Size()<=128,"$.tracks[].source.segments","Too many inline segments");
        segments.emplace(id,Value(rapidjson::kArrayType));
    }
    std::set<std::string> ids,names;
    // Validate all descriptors before invoking the adapter, even if the last
    // descriptor is malformed. Duplicate/unsafe paths never reach the loader.
    for(const auto& item:descriptors.GetArray()) {
        Keys(item,"$.segmentCatalog[]",{"id","name","length","sha256"});SegmentCatalogPage page;
        page.id=Id(item["id"],"$.segmentCatalog[].id");page.name=Text(item["name"],"$.segmentCatalog[].name");PlainName(page.name,"$.segmentCatalog[].name");
        page.length=size_t(Unsigned(item["length"],"$.segmentCatalog[].length",MaxCatalogPageBytes,true));
        page.sha256=Text(item["sha256"],"$.segmentCatalog[].sha256",64);
        Require(page.sha256.size()==64 && page.sha256.find_first_not_of("0123456789abcdef")==std::string::npos,"$.segmentCatalog[].sha256","Invalid SHA-256 digest");
        Require(ids.insert(page.id).second && names.insert(catalog_detail::FilenameKey(page.name)).second,"$.segmentCatalog","Duplicate page ID or filename");
        result.pages.push_back(std::move(page));
    }
    std::set<std::string> archived;
    for(const auto& page:result.pages) {
        const auto text=load(page);
        Require(text.size()==page.length,"segmentCatalog.page","Catalog page length mismatch","catalog_length_mismatch");
        Sha256 hash;hash.update(text.data(),text.size());
        Require(hash.hex()==page.sha256,"segmentCatalog.page","Catalog page SHA-256 mismatch","integrity-mismatch");
        auto input=catalog_detail::Json(text,MaxCatalogPageBytes,"segmentCatalog.page");
        Keys(input,"segmentCatalog.page",{"version","sessionId","generation","writerInstanceId","pageId","resources","tracks"});
        Require(input["version"].IsUint() && input["version"].GetUint()==1,"segmentCatalog.page.version","Unsupported catalog page version");
        Require(Id(input["sessionId"],"page.sessionId",true)==session &&
            Unsigned(input["generation"],"page.generation",UINT64_MAX,true)==generation &&
            Id(input["writerInstanceId"],"page.writerInstanceId",true)==writer &&
            Id(input["pageId"],"page.pageId")==page.id,"segmentCatalog.page","Page belongs to another session, generation, writer or page","catalog_identity_mismatch");
        Require(input["resources"].IsArray() && input["resources"].Size()>0 && input["resources"].Size()<=MaxResources,"page.resources","Expected 1..64 archived resources");
        Require(root["resources"].Size()<=MaxCatalogResources-input["resources"].Size(),"page.resources","Expanded catalog has too many resources");
        std::set<std::string> pageResources,used;
        for(const auto& resource:input["resources"].GetArray()) {
            Require(resource.IsObject() && resource.HasMember("id"),"page.resources","Invalid archived resource");
            const auto id=Id(resource["id"],"page.resources[].id");
            Require(archived.insert(id).second && pageResources.insert(id).second,"page.resources","Duplicate archived resource");
            root["resources"].PushBack(Value(resource,allocator),allocator);
        }
        Require(input["tracks"].IsArray() && input["tracks"].Size()>0 && input["tracks"].Size()<=2,"page.tracks","Invalid archived track list");
        std::set<std::string> seenTracks;
        for(const auto& track:input["tracks"].GetArray()) {
            Keys(track,"page.tracks[]",{"trackId","segments"});const auto id=Id(track["trackId"],"page.tracks[].trackId");
            Require(tracks.count(id) && seenTracks.insert(id).second,"page.tracks","Unknown or duplicate archived track");
            const auto& parts=track["segments"];
            Require(parts.IsArray() && parts.Size()>0 && parts.Size()<=MaxResources,"page.tracks[].segments","Invalid archived segment list");
            auto& output=segments.at(id);
            Require(output.Size()<=MaxCatalogSegments-parts.Size(),"page.tracks[].segments","Too many expanded segments");
            std::set<std::string> trackResources;
            for(const auto& part:parts.GetArray()) {
                Require(part.IsObject() && part.HasMember("resourceId"),"page.tracks[].segments","Invalid archived segment");
                const auto resource=Id(part["resourceId"],"page.segments[].resourceId");
                const bool first=used.insert(resource).second;
                Require(pageResources.count(resource) && trackResources.insert(resource).second && (first || muxedTs),"page.segments","Each page segment needs a resource in the same page; sharing is restricted to explicit muxed TS views");
                output.PushBack(Value(part,allocator),allocator);
            }
        }
        Require(used==pageResources,"page.resources","Unreferenced archived resource");
    }
    for(const auto& track:tracks) {
        auto& output=segments.at(track.first);auto& tail=root["tracks"][rapidjson::SizeType(track.second)]["source"]["segments"];
        Require(output.Size()<=MaxCatalogSegments-tail.Size(),"$.tracks[].source.segments","Too many expanded segments");
        for(const auto& part:tail.GetArray())output.PushBack(Value(part,allocator),allocator);
        tail.Swap(output);
    }
    root.RemoveMember("segmentCatalog");
    rapidjson::StringBuffer buffer;rapidjson::Writer<rapidjson::StringBuffer> encoder(buffer);root.Accept(encoder);
    result.expandedJson.assign(buffer.GetString(),buffer.GetSize());
    result.snapshot=Parse(result.expandedJson,SnapshotScope::ExpandedCatalog);
    for(const auto& id:archived) {
        const auto& resource=result.snapshot.resources.at(id);
        Require(resource.state=="complete" && !resource.retired && resource.known && resource.length && resource.covers(0,resource.length),"page.resources","Archive only complete, fully confirmed, non-retired resources");
    }
    for(const auto& item:result.snapshot.resources)
        Require(!names.count(catalog_detail::FilenameKey(item.second.name)),"page.resources","Catalog page cannot also be a media resource");
    return result;
}
inline void AdvanceSegmentCatalog(const SegmentCatalog& old,const SegmentCatalog& next) {
    Require(next.pages.size()>=old.pages.size(),"$.segmentCatalog","Published catalog pages cannot be removed");
    for(size_t i=0;i<old.pages.size();++i)
        Require(old.pages[i]==next.pages[i],"$.segmentCatalog","Published catalog pages cannot change or move");
    Advance(old.snapshot,next.snapshot);
}
}} // namespace foxicmp::progressive
