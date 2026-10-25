// FoxicMP progressive session model v2. GPL-3.0-or-later.
// No filesystem, media backend or platform API: validation never opens a resource.
#pragma once
#include "DSUtil/HarvesterState.h"
#include "ExtLib/rapidjson/include/rapidjson/stringbuffer.h"
#include "ExtLib/rapidjson/include/rapidjson/writer.h"
#include <map>
#include <set>
#include <memory>
#include <utility>

namespace foxicmp { namespace progressive {
using rapidjson::Value;
constexpr uint64_t MaxByte = uint64_t(INT64_MAX);
constexpr size_t MaxResources = 64, MaxTracks = 32, MaxRanges = 128, MaxReaders = 128;
struct Error : std::runtime_error {
    std::string code, path;
    Error(const char* c, const std::string& p, const char* why):std::runtime_error(why),code(c),path(p){}
};
inline void Require(bool condition,const std::string& path,const char* message,const char* code="invalid_snapshot") {
    if(!condition)throw Error(code,path,message);
}
inline void Keys(const Value& v,const std::string& p,std::initializer_list<const char*> required,
                 std::initializer_list<const char*> optional={}) {
    Require(v.IsObject(),p,"Expected an object");
    for(auto k:required)Require(v.HasMember(k),p+"."+k,"Missing field");
    for(auto it=v.MemberBegin();it!=v.MemberEnd();++it) {
        bool found=false;
        for(auto k:required)found|=it->name==k;
        for(auto k:optional)found|=it->name==k;
        Require(found,p+"."+it->name.GetString(),"Unknown field");
    }
}
inline std::string Text(const Value& v,const std::string& p,size_t limit=255,bool empty=false) {
    Require(v.IsString(),p,"Expected a string");
    std::string s(v.GetString(),v.GetStringLength());
    Require(s.size()<=limit && (empty || !s.empty()) && s.find('\0')==std::string::npos,p,"Invalid string length or embedded null");
    for(unsigned char c:s)Require(c>=32 && c!=127,p,"Control characters are not allowed");
    return s;
}
inline bool Flag(const Value& v,const std::string& p) {Require(v.IsBool(),p,"Expected a boolean");return v.GetBool();}
inline std::string Id(const Value& v,const std::string& p,bool uuid=false) {
    auto s=Text(v,p,uuid?32:64);
    Require(!uuid || s.size()==32,p,"Expected a 128-bit lowercase hex ID");
    Require(s.find_first_not_of(uuid?"0123456789abcdef":"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos,p,"Invalid identifier");
    return s;
}
// Canonical decimal strings only; JSON numbers are deliberately not accepted.
inline uint64_t Unsigned(const Value& v,const std::string& p,uint64_t max=UINT64_MAX,bool positive=false) {
    auto s=Text(v,p,20);uint64_t n=0;
    Require(s=="0" || s.front()!='0',p,"Non-canonical decimal string");
    for(char c:s) {
        Require(c>='0' && c<='9',p,"Expected an unsigned decimal string");
        auto digit=uint64_t(c-'0');Require(n<=max/10 && (n!=max/10 || digit<=max%10),p,"Integer overflow");n=n*10+digit;
    }
    Require(!positive || n>0,p,"Expected a positive integer");return n;
}
inline int64_t Signed(const Value& v,const std::string& p) {
    auto s=Text(v,p,20);const bool negative=s.front()=='-';auto magnitude=negative?s.substr(1):s;
    Require(!magnitude.empty() && (!negative || magnitude!="0"),p,"Non-canonical signed decimal");
    Value raw;raw.SetString(magnitude.data(),rapidjson::SizeType(magnitude.size()));
    const auto n=Unsigned(raw,p,negative?uint64_t(INT64_MAX)+1:MaxByte);
    return negative?(n==uint64_t(INT64_MAX)+1?INT64_MIN:-int64_t(n)):int64_t(n);
}
inline std::string State(const Value& v,const std::string& p,bool writer=false) {
    auto s=Text(v,p,16);
    Require(s=="downloading" || s=="paused" || s=="complete" || s=="failed" || s=="cancelled" || (writer && s=="merging"),p,"Unknown writer/resource state");return s;
}
inline bool Terminal(const std::string& s) {return s=="complete" || s=="failed" || s=="cancelled";}
template<class Writer> inline void Canonical(const Value& value,Writer& writer) {
    if(value.IsObject()) {
        std::map<std::string,const Value*> fields;
        for(auto it=value.MemberBegin();it!=value.MemberEnd();++it)fields.emplace(std::string(it->name.GetString(),it->name.GetStringLength()),&it->value);
        writer.StartObject();for(const auto& field:fields){writer.Key(field.first.data(),rapidjson::SizeType(field.first.size()));Canonical(*field.second,writer);}writer.EndObject();
    }else if(value.IsArray()) {writer.StartArray();for(const auto& item:value.GetArray())Canonical(item,writer);writer.EndArray();}
    else value.Accept(writer);
}
struct Range {uint64_t offset=0,length=0;};
struct Resource {
    std::string id,name,state;bool known=false,retired=false;uint64_t length=0;
    std::vector<Range> ranges;
    bool covers(uint64_t offset,uint64_t count) const {
        if(offset>MaxByte || count>MaxByte-offset || (known && (offset>length || count>length-offset)))return false;
        if(!count)return true;
        uint64_t end=offset+count;
        for(const auto& r:ranges) {
            if(r.offset>MaxByte || r.length>MaxByte-r.offset)return false;
            if(r.offset>offset)return false;
            if(r.offset+r.length<=offset)continue;
            offset=r.offset+r.length;if(offset>=end)return true;
        }
        return false;
    }
};
struct Segment {
    std::string resource,initialization,configuration;uint64_t sequence=0;int64_t start=0,duration=0,offset=0;bool discontinuity=false;
};
struct Track {
    std::string id,kind,codec,container,state,label,language,resource,initialization;
    bool required=false,initializationReady=false,defaultTrack=false,forced=false;
    std::vector<Segment> segments;
    std::set<std::string> resources() const {
        std::set<std::string> ids;if(!resource.empty())ids.insert(resource);if(!initialization.empty())ids.insert(initialization);
        for(const auto& s:segments) {ids.insert(s.resource);if(!s.initialization.empty())ids.insert(s.initialization);}return ids;
    }
};
struct Snapshot {
    std::string id,writer,state;uint64_t generation=0,revision=0,metadataRevision=0;
    bool live=false,knownDuration=false,hasWindow=false;int64_t duration=0,offset=0,windowStart=0,windowEnd=0;
    unsigned numerator=0,denominator=0;
    std::map<std::string,Resource> resources;
    std::map<std::string,Track> tracks;
    std::string metadata;
};
inline void PlainName(const std::string& name,const std::string& p) {
    Require(name!="." && name!=".." && name.find_first_of("/\\:<>|?*\"")==std::string::npos && name.back()!='.' && name.back()!=' ',p,"Expected a portable plain local filename");
    auto stem=name.substr(0,name.find('.'));for(auto& c:stem)if(c>='a' && c<='z')c-=32;
    Require(stem!="CON" && stem!="PRN" && stem!="AUX" && stem!="NUL" && !(stem.size()==4 && (stem.substr(0,3)=="COM" || stem.substr(0,3)=="LPT") && stem[3]>='1' && stem[3]<='9'),p,"Windows device names are forbidden");
}
inline Snapshot Parse(const std::string& json) {
    Require(!json.empty() && json.size()<=harvester::MaxManifestBytes,"$","Snapshot exceeds 65536 bytes");
    rapidjson::Document d;d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(json.data(),json.size());
    Require(!d.HasParseError() && d.IsObject(),"$","Invalid UTF-8 JSON object");
    try {harvester::UniqueKeys(d);}catch(const std::exception& e){throw Error("invalid_snapshot","$",e.what());}
    Keys(d,"$",{"schemaVersion","sessionId","generation","revision","writer","timeline","resources","tracks"},{"metadata"});
    Require(d["schemaVersion"].IsUint() && d["schemaVersion"].GetUint()==2,"$.schemaVersion","Expected schema 2","unsupported_schema");
    Snapshot s;s.id=Id(d["sessionId"],"$.sessionId",true);s.generation=Unsigned(d["generation"],"$.generation",UINT64_MAX,true);s.revision=Unsigned(d["revision"],"$.revision",UINT64_MAX,true);
    const auto& w=d["writer"];Keys(w,"$.writer",{"instanceId","state"});s.writer=Id(w["instanceId"],"$.writer.instanceId",true);s.state=State(w["state"],"$.writer.state",true);
    const auto& time=d["timeline"];Keys(time,"$.timeline",{"timebase","live","duration","timestampOffset"},{"window"});
    const auto& base=time["timebase"];Keys(base,"$.timeline.timebase",{"numerator","denominator"});
    for(auto key:{"numerator","denominator"})Require(base[key].IsUint() && base[key].GetUint()>0 && base[key].GetUint()<=1000000000,"$.timeline.timebase","Invalid rational timebase");
    s.numerator=base["numerator"].GetUint();s.denominator=base["denominator"].GetUint();s.live=Flag(time["live"],"$.timeline.live");s.offset=Signed(time["timestampOffset"],"$.timeline.timestampOffset");
    s.knownDuration=!time["duration"].IsNull();if(s.knownDuration)s.duration=int64_t(Unsigned(time["duration"],"$.timeline.duration",MaxByte,true));
    Require(!s.live || !s.knownDuration,"$.timeline.duration","Live duration must be null");
    if(time.HasMember("window")) {
        const auto& window=time["window"];Keys(window,"$.timeline.window",{"start","end"});s.hasWindow=true;
        s.windowStart=Signed(window["start"],"$.timeline.window.start");s.windowEnd=Signed(window["end"],"$.timeline.window.end");
        Require(s.live && s.windowStart<s.windowEnd,"$.timeline.window","Invalid live window");
    }
    const auto& resources=d["resources"];Require(resources.IsArray() && resources.Size()>0 && resources.Size()<=MaxResources,"$.resources","Expected 1..64 resources");std::set<std::string> names;
    for(rapidjson::SizeType i=0;i<resources.Size();++i) {
        const auto p="$.resources["+std::to_string(i)+"]";const auto& v=resources[i];
        Keys(v,p,{"id","name","length","ranges","state"},{"retired"});Resource r;
        r.id=Id(v["id"],p+".id");r.name=Text(v["name"],p+".name");PlainName(r.name,p+".name");
        std::string portable=r.name;for(auto& c:portable)if(c>='A' && c<='Z')c+=32;
        Require(names.insert(portable).second,p+".name","Duplicate portable resource filename");
        r.state=State(v["state"],p+".state");r.known=!v["length"].IsNull();if(r.known)r.length=Unsigned(v["length"],p+".length",MaxByte);
        if(v.HasMember("retired"))r.retired=Flag(v["retired"],p+".retired");
        const auto& ranges=v["ranges"];Require(ranges.IsArray() && ranges.Size()<=MaxRanges,p+".ranges","Too many ranges");uint64_t previousEnd=0;
        for(rapidjson::SizeType j=0;j<ranges.Size();++j) {
            const auto q=p+".ranges["+std::to_string(j)+"]";const auto& range=ranges[j];Keys(range,q,{"offset","length"});
            Range a{Unsigned(range["offset"],q+".offset",MaxByte),Unsigned(range["length"],q+".length",MaxByte,true)};
            Require(a.length<=MaxByte-a.offset && a.offset>=previousEnd && (!r.known || (a.offset<=r.length && a.length<=r.length-a.offset)),q,"Overlapping, unsorted or out-of-bounds range");
            previousEnd=a.offset+a.length;r.ranges.push_back(a);
        }
        Require(r.state!="complete" || (r.known && r.covers(0,r.length)),p,"Complete resource has gaps or unknown length","incomplete_resource");
        Require(s.resources.emplace(r.id,std::move(r)).second,p+".id","Duplicate resource ID");
    }
    const auto& tracks=d["tracks"];Require(tracks.IsArray() && tracks.Size()>0 && tracks.Size()<=MaxTracks,"$.tracks","Expected 1..32 tracks");bool media=false;
    for(rapidjson::SizeType i=0;i<tracks.Size();++i) {
        const auto p="$.tracks["+std::to_string(i)+"]";const auto& v=tracks[i];
        Keys(v,p,{"id","kind","required","codecHint","containerHint","state","source","initializationReady"},{"label","language","default","forced","initializationResourceId"});Track t;
        t.id=Id(v["id"],p+".id");t.kind=Text(v["kind"],p+".kind",16);
        Require(t.kind=="video" || t.kind=="audio" || t.kind=="subtitle" || t.kind=="attachment",p+".kind","Invalid track kind");
        media|=t.kind=="video" || t.kind=="audio";t.required=Flag(v["required"],p+".required");t.state=State(v["state"],p+".state");
        t.codec=Text(v["codecHint"],p+".codecHint",64,true);t.container=Text(v["containerHint"],p+".containerHint",64,true);t.initializationReady=Flag(v["initializationReady"],p+".initializationReady");
        if(v.HasMember("label"))t.label=Text(v["label"],p+".label",1024,true);
        if(v.HasMember("language"))t.language=Text(v["language"],p+".language",64,true);
        if(v.HasMember("default"))t.defaultTrack=Flag(v["default"],p+".default");
        if(v.HasMember("forced"))t.forced=Flag(v["forced"],p+".forced");
        if(v.HasMember("initializationResourceId"))t.initialization=Id(v["initializationResourceId"],p+".initializationResourceId");
        const auto& source=v["source"];Keys(source,p+".source",{},{"resourceId","segments"});
        Require(source.HasMember("resourceId")!=source.HasMember("segments"),p+".source","Expected a resource or segments, exclusively");
        if(source.HasMember("resourceId"))t.resource=Id(source["resourceId"],p+".source.resourceId");
        else {
            const auto& list=source["segments"];Require(list.IsArray() && list.Size()<=128,p+".source.segments","Invalid segment list");uint64_t seq=0;int64_t end=INT64_MIN;
            for(rapidjson::SizeType j=0;j<list.Size();++j) {
                const auto q=p+".source.segments["+std::to_string(j)+"]";const auto& item=list[j];
                Keys(item,q,{"resourceId","sequence","start","duration","timestampOffset","discontinuity","configurationId"},{"initializationResourceId"});Segment seg;
                seg.resource=Id(item["resourceId"],q+".resourceId");seg.sequence=Unsigned(item["sequence"],q+".sequence");seg.start=Signed(item["start"],q+".start");seg.duration=int64_t(Unsigned(item["duration"],q+".duration",MaxByte,true));seg.offset=Signed(item["timestampOffset"],q+".timestampOffset");seg.discontinuity=Flag(item["discontinuity"],q+".discontinuity");seg.configuration=Id(item["configurationId"],q+".configurationId");
                if(item.HasMember("initializationResourceId"))seg.initialization=Id(item["initializationResourceId"],q+".initializationResourceId");
                Require((!j || seg.sequence>seq) && seg.start>=end && seg.start<=INT64_MAX-seg.duration,q,"Invalid segment order or timestamp overflow");
                if(j && seg.configuration!=t.segments.back().configuration)Require(seg.discontinuity && !seg.initialization.empty(),q,"Codec configuration change requires discontinuity and initialization");
                seq=seg.sequence;end=seg.start+seg.duration;t.segments.push_back(seg);
            }
        }
        for(const auto& id:t.resources())Require(s.resources.count(id)!=0,p+".source","Unknown resource reference");
        if(t.state=="complete")for(const auto& id:t.resources())Require(s.resources.at(id).state=="complete",p,"Completed track references unfinished resource","incomplete_resource");
        Require(s.tracks.emplace(t.id,std::move(t)).second,p+".id","Duplicate track ID");
    }
    Require(media,"$.tracks","At least one audio or video track is required");
    if(s.state=="complete" || s.state=="merging") {
        for(const auto& t:s.tracks)Require(!t.second.required || t.second.state=="complete","$.tracks","Required track is not complete","incomplete_resource");
        if(s.state=="complete")for(const auto& r:s.resources)Require(Terminal(r.second.state),"$.resources","Writer completed with active resources","incomplete_resource");
    }
    if(d.HasMember("metadata")) {
        const auto& meta=d["metadata"];Keys(meta,"$.metadata",{"revision"},{"title","chapters","coverResourceId"});s.metadataRevision=Unsigned(meta["revision"],"$.metadata.revision",UINT64_MAX,true);
        if(meta.HasMember("title"))Text(meta["title"],"$.metadata.title",4096,true);
        if(meta.HasMember("coverResourceId"))Require(s.resources.count(Id(meta["coverResourceId"],"$.metadata.coverResourceId"))!=0,"$.metadata.coverResourceId","Unknown cover resource");
        if(meta.HasMember("chapters")) {
            const auto& chapters=meta["chapters"];Require(chapters.IsArray() && chapters.Size()<=128,"$.metadata.chapters","Too many chapters");std::set<std::string> ids;int64_t last=INT64_MIN;
            for(const auto& ch:chapters.GetArray()) {
                Keys(ch,"$.metadata.chapters[]",{"id","time","title"});Require(ids.insert(Id(ch["id"],"$.metadata.chapters[].id")).second,"$.metadata.chapters","Duplicate chapter ID");
                auto at=Signed(ch["time"],"$.metadata.chapters[].time");Require(at>=0 && at>=last && (!s.knownDuration || at<=s.duration),"$.metadata.chapters[].time","Invalid chapter time");last=at;Text(ch["title"],"$.metadata.chapters[].title",1024,true);
            }
        }
        rapidjson::StringBuffer b;rapidjson::Writer<rapidjson::StringBuffer> writer(b);Canonical(meta,writer);s.metadata.assign(b.GetString(),b.GetSize());
    }
    return s;
}
inline void Advance(const Snapshot& old,const Snapshot& next) {
    Require(old.id==next.id && old.generation==next.generation && old.writer==next.writer,"$","Session generation or writer changed; reopen","generation_changed");
    Require(next.revision>old.revision,"$.revision","Revision must increase","stale_revision");
    Require(!Terminal(old.state) || next.state==old.state,"$.writer.state","Terminal writer state changed");
    Require(!Terminal(old.state) || (next.resources.size()==old.resources.size() && next.tracks.size()==old.tracks.size()),"$","Terminal writer cannot introduce new resources or tracks");
    Require(old.numerator==next.numerator && old.denominator==next.denominator && old.offset==next.offset && old.live==next.live && (!old.knownDuration || (next.knownDuration && old.duration==next.duration)),"$.timeline","Session clock changed");
    Require(!old.hasWindow || (next.hasWindow && next.windowStart>=old.windowStart && next.windowEnd>=old.windowEnd),"$.timeline.window","Live window moved backwards");
    for(const auto& item:old.resources) {
        auto found=next.resources.find(item.first);Require(found!=next.resources.end(),"$.resources","Resource removal needs a runtime reader registry","readers_required");const auto& a=item.second;const auto& b=found->second;
        Require(a.name==b.name && (!a.known || (b.known && a.length==b.length)) && (!a.retired || b.retired),"$.resources","Resource identity changed");
        Require(!Terminal(a.state) || a.state==b.state,"$.resources","Terminal resource state changed");
        for(const auto& range:a.ranges)Require(b.covers(range.offset,range.length),"$.resources.ranges","Published bytes were withdrawn");
        if(Terminal(old.state)) {
            Require(a.ranges.size()==b.ranges.size(),"$.resources.ranges","Terminal writer published new bytes");
            for(size_t i=0;i<a.ranges.size();++i)Require(a.ranges[i].offset==b.ranges[i].offset && a.ranges[i].length==b.ranges[i].length,"$.resources.ranges","Terminal writer changed bytes");
        }
        if(Terminal(a.state)) {Require(a.ranges.size()==b.ranges.size(),"$.resources.ranges","Terminal resource grew");for(size_t i=0;i<a.ranges.size();++i)Require(a.ranges[i].offset==b.ranges[i].offset && a.ranges[i].length==b.ranges[i].length,"$.resources.ranges","Terminal resource changed");}
    }
    for(const auto& item:old.tracks) {
        const auto found=next.tracks.find(item.first);Require(found!=next.tracks.end(),"$.tracks","Tracks cannot disappear within a generation");const auto& a=item.second;const auto& b=found->second;
        Require(a.kind==b.kind && a.codec==b.codec && a.container==b.container && a.required==b.required && a.resource==b.resource && a.initialization==b.initialization && (!a.initializationReady || b.initializationReady),"$.tracks","Track identity changed");
        Require(!Terminal(a.state) || a.state==b.state,"$.tracks.state","Terminal track state changed");
        Require(b.segments.size()>=a.segments.size(),"$.tracks.source.segments","Segments were withdrawn");
        Require(!Terminal(a.state) || b.segments.size()==a.segments.size(),"$.tracks.source.segments","Terminal track grew");
        for(size_t i=0;i<a.segments.size();++i) {
            const auto& x=a.segments[i];const auto& y=b.segments[i];
            Require(x.resource==y.resource && x.initialization==y.initialization && x.configuration==y.configuration && x.sequence==y.sequence && x.start==y.start && x.duration==y.duration && x.offset==y.offset && x.discontinuity==y.discontinuity,"$.tracks.source.segments","Published segment changed");
        }
    }
    Require(next.metadataRevision>=old.metadataRevision && (old.metadata==next.metadata || next.metadataRevision>old.metadataRevision),"$.metadata.revision","Metadata changed without a new revision");
}
inline std::string ValidateReply(const std::string& json,const std::string& previous={}) {
    rapidjson::StringBuffer out;rapidjson::Writer<rapidjson::StringBuffer> w(out);w.StartObject();
    w.Key("version");w.Int(1);w.Key("action");w.String("validate");w.Key("schemaVersion");w.Int(2);
    try {
        const auto snapshot=Parse(json);if(!previous.empty())Advance(Parse(previous),snapshot);
        w.Key("ok");w.Bool(true);w.Key("validated");w.Bool(true);
        w.Key("mode");w.String("validation-only");w.Key("playbackSupported");w.Bool(false);
        w.Key("reasonCode");w.String("backend-not-integrated");
        w.Key("resourceCount");w.Uint(unsigned(snapshot.resources.size()));w.Key("trackCount");w.Uint(unsigned(snapshot.tracks.size()));
        w.Key("generation");w.String(std::to_string(snapshot.generation).c_str());
        w.Key("revision");w.String(std::to_string(snapshot.revision).c_str());
    }catch(const Error& e) {
        w.Key("ok");w.Bool(false);w.Key("validated");w.Bool(false);w.Key("code");w.String(e.code.c_str());w.Key("path");w.String(e.path.c_str());w.Key("error");w.String(e.what());
    }catch(const std::exception& e) {
        w.Key("ok");w.Bool(false);w.Key("validated");w.Bool(false);w.Key("code");w.String("invalid_snapshot");w.Key("path");w.String("$");w.Key("error");w.String(e.what());
    }
    w.EndObject();return {out.GetString(),out.GetSize()};
}
}} // namespace foxicmp::progressive
