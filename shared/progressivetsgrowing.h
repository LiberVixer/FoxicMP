// One shared growing TS resource over confirmed, immutable SHA ranges.
// GPL-3.0-or-later. No network, extra media file or player-owned profile writes.
#pragma once
#include "progressivetsstream.h"
#include "DSUtil/HarvesterTSGrowing.h"
namespace foxicmp { namespace progressive {
class TsGrowingStream {
    using Parser=harvester::ts::GrowingTransport;
    struct Parsing {
        Parser transport;harvester::ts::Segment video;
        size_t videoBytes=0;std::array<std::shared_ptr<harvester::ts::AccessIndex>,2> index;
    };
    struct Published {Snapshot model;std::string resource;std::array<std::string,2> ids;uint64_t prefix=0;bool complete=false;std::shared_ptr<const Parsing> parser;};
    std::shared_ptr<ResourceSource> source;std::filesystem::path registry;
    std::shared_ptr<const Published> current;std::mutex publication;mutable std::mutex errors;
    std::atomic<bool> cancelled{false},failed{false};std::string errorCode,errorText;
    static void supported(bool b,const char* why){Require(b,"growingTs",why,"unsupported_media");}
    static uint64_t origin(const Snapshot& m){supported(m.offset>=0,"Growing origin must be nonnegative");const auto t=uint64_t(m.offset),n=t/10000000*90000+(t%10000000*90000+9999999)/10000000;supported(n<(uint64_t(1)<<33) && harvester::Time100ns(int64_t(n),90000)==m.offset,"Growing origin must equal a rescaled 90 kHz clock");return n;}
    static std::shared_ptr<Published> plan(const std::string& json) {
        auto out=std::make_shared<Published>();out->model=Parse(json);const auto& m=out->model;
        supported(!m.live && !m.hasWindow && m.knownDuration && m.numerator==1 && m.denominator==10000000 && m.tracks.size()==2 && m.resources.size()==1,"Growing TS requires known-duration VOD, two logical tracks and one resource");origin(m);
        Require(m.state!="failed" && m.state!="cancelled","growingTs","Writer failed or cancelled","writer-failed");
        for(const auto& item:m.tracks){const auto& t=item.second;supported((t.kind=="video" || t.kind=="audio") && t.required && t.container=="mpegts-growing" && !t.resource.empty() && t.segments.empty() && t.initialization.empty(),"Growing TS requires shared scalar AVC/AAC views without external init");const unsigned k=t.kind=="video"?0:1;supported(out->ids[k].empty(),"Duplicate growing track kind");out->ids[k]=t.id;supported(t.codec.empty() || t.codec==(k?"aac-lc":"h264"),"Unsupported growing TS codec hint");Require(t.state!="failed" && t.state!="cancelled","growingTs","Track writer failed","resource-failed");if(out->resource.empty())out->resource=t.resource;supported(out->resource==t.resource,"Growing views require the same resource ID");}
        supported(!out->ids[0].empty() && !out->ids[1].empty(),"Growing TS needs video and audio");const auto& v=m.tracks.at(out->ids[0]);const auto& a=m.tracks.at(out->ids[1]);supported(v.state==a.state && v.initializationReady==a.initializationReady,"Growing views need equal readiness/state");
        const auto& r=m.resources.at(out->resource);Require(r.integrity,"growingTs","Growing TS requires SHA-256","integrity-required");supported(!r.retired && (!r.known || r.length<=uint64_t(SIZE_MAX)),"Invalid growing resource length/state");Require(r.state!="failed" && r.state!="cancelled","growingTs","Resource writer failed","resource-failed");
        for(const auto& range:r.ranges){supported(range.offset==out->prefix,"Growing TS needs a contiguous confirmed prefix");out->prefix=range.offset+range.length;}
        out->complete=r.state=="complete" && r.known && out->prefix==r.length && v.state=="complete";
        return out;
    }
    void check() const {Require(!cancelled.load(),"growingTs","Growing reader cancelled","cancelled");if(failed.load()){std::lock_guard<std::mutex> lock(errors);throw Error(errorCode.empty()?"session_closed":errorCode.c_str(),"growingTs",errorText.empty()?"Growing reader failed":errorText.c_str());}}
    void fail(const std::string& code,const std::string& text){failed.store(true);std::lock_guard<std::mutex> lock(errors);if(errorCode.empty()){errorCode=code;errorText=text;}}
    void exact(const std::string& id,uint64_t at,void* data,size_t count) {
        auto* out=static_cast<char*>(data);for(size_t n=0;n<count;){check();const auto part=(std::min)(size_t(65536),count-n);const auto r=source->read(id,at+n,out+n,part);Require(r.data() && r.bytes==part,"growingTs",r.reason.c_str(),r.data()?"source-truncated":r.code.c_str());n+=part;}check();
    }
    std::vector<uint8_t> gather(const std::string& id,const harvester::ts::Pes& pes) {
        std::vector<uint8_t> bytes(pes.size),window(65536);size_t at=0,n=0;uint64_t start=0;
        for(const auto& range:pes.payload){supported(range.offset>=pes.transportOffset && range.offset<=pes.transportEnd && range.size<=pes.transportEnd-range.offset && range.size<=bytes.size()-at,"Growing PES ranges exceed bounds");size_t copied=0;
            while(copied<range.size){const auto pos=uint64_t(range.offset)+copied;if(!n || pos<start || pos-start>=n){start=pos;n=size_t((std::min)(uint64_t(65536),uint64_t(pes.transportEnd)-pos));exact(id,pos,window.data(),n);}const auto count=(std::min)(range.size-copied,n-size_t(pos-start));std::memcpy(bytes.data()+at,window.data()+size_t(pos-start),count);at+=count;copied+=count;}}
        supported(at==bytes.size(),"Incomplete growing payload ranges");return bytes;
    }
    void accept(Parsing& p,unsigned kind,harvester::ts::Segment batch,const Published& state) {
        if(batch.packets.empty())return;
        const auto& program=p.transport.program(kind);if(!p.index[kind])p.index[kind]=std::make_shared<harvester::ts::AccessIndex>(program,size_t(p.transport.initEnd()),origin(state.model));
        auto next=harvester::ts::AccessPes(program,batch,[&](const harvester::ts::Pes& unit){return gather(state.resource,unit);},origin(state.model),p.index[kind]->configuration());
        supported(next.start>=(kind?-10000000:0) && (!p.index[kind]->samples().empty() || next.start<=(kind?112:10000000)),"Growing track must start near timeline zero");
        supported(next.end<=state.model.duration || uint64_t(next.end)-uint64_t(state.model.duration)<=uint64_t(harvester::ts::ClockTolerance),"Growing packet exceeds declared duration");
        uint64_t end=0;for(const auto& unit:batch.packets)end=(std::max)(end,uint64_t(unit.transportEnd));p.index[kind]->appendParsed(std::move(next),end);check();
    }
    void materialize(Published& next,const Published* old) {
        const bool ready=next.model.tracks.at(next.ids[0]).initializationReady;
        const auto previous=old?old->prefix:0;const auto position=old && old->parser?old->parser->transport.parsedBytes()+old->parser->transport.pendingPacketBytes():0;
        if(old && old->parser && ((!ready) || (next.prefix==previous && next.complete==old->complete))){next.parser=old->parser;return;}
        auto p=old && old->parser?std::make_shared<Parsing>(*old->parser):std::make_shared<Parsing>();for(unsigned k=0;k<2;++k)if(p->index[k])p->index[k]=std::make_shared<harvester::ts::AccessIndex>(*p->index[k]);
        if(!ready){next.parser=std::move(p);return;}
        supported(next.prefix>=position && next.prefix-position<=harvester::ts::MaxSegmentBytes,"Publish growing bytes in increments of at most 16 MiB");
        auto flushVideo=[&](){if(p->video.packets.empty())return;supported(p->transport.hasClock(),"Growing video GOP needs a PCR");accept(*p,0,std::move(p->video),next);p->video={};p->videoBytes=0;};
        auto onPes=[&](unsigned kind,harvester::ts::Pes unit){if(kind){harvester::ts::Segment batch;batch.packets.push_back(std::move(unit));accept(*p,1,std::move(batch),next);return;}
            const auto bytes=gather(next.resource,unit);bool idr=false;for(const auto& nal:harvester::ts::AnnexB({bytes.data(),bytes.size()}))idr|=nal.type==5;
            if(idr)flushVideo();supported(!p->video.packets.empty() || idr,"Growing video must begin at IDR");supported(unit.size<=harvester::ts::MaxSegmentBytes-p->videoBytes && p->video.packets.size()<harvester::ts::MaxAccessUnits,"Growing GOP exceeds bounded staging");p->videoBytes+=unit.size;p->video.packets.push_back(std::move(unit));};
        std::vector<uint8_t> bytes(65536);for(uint64_t at=position;at<next.prefix;){check();const auto count=size_t((std::min)(uint64_t(bytes.size()),next.prefix-at));exact(next.resource,at,bytes.data(),count);p->transport.append({bytes.data(),count},onPes);at+=count;}
        if(next.complete){p->transport.finish(onPes);flushVideo();supported(p->index[0] && p->index[1],"Completed growing file has no AVC/AAC units");}
        check();next.parser=std::move(p);
    }
    void cleanup() noexcept {if(!source)return;try{const auto ids=source->readerIds();source.reset();for(const auto& id:ids)ReaderRequest(registry,"{\"version\":1,\"action\":\"readerForget\",\"readerId\":\""+id+"\"}");}catch(...){source.reset();}}
public:
    using Status=TsResourceStream::Status;using Packet=TsResourceStream::Packet;
    static bool Declared(const std::string& json){const auto d=catalog_detail::Json(json,harvester::MaxManifestBytes,"$");if(d.HasMember("tracks") && d["tracks"].IsArray())for(const auto& t:d["tracks"].GetArray())if(t.IsObject() && t.HasMember("containerHint") && t["containerHint"].IsString() && std::string(t["containerHint"].GetString(),t["containerHint"].GetStringLength())=="mpegts-growing")return true;return false;}
    static void CheckProfile(const std::string& json){plan(json);}
    TsGrowingStream(const std::filesystem::path& root,const std::filesystem::path& receipts,const std::string& json):registry(receipts){auto next=plan(json);source=std::make_shared<ResourceSource>(root,receipts,json);try{materialize(*next,nullptr);current=std::move(next);}catch(const harvester::ts::Error& e){cleanup();throw Error(e.code.c_str(),"growingTs",e.what());}catch(...){cleanup();throw;}}
    ~TsGrowingStream(){cancel();cleanup();}
    void cancel(){cancelled.store(true);if(source)source->cancel();}
    void publish(const std::string& json){std::lock_guard<std::mutex> lock(publication);check();auto old=std::atomic_load(&current);auto next=plan(json);Advance(old->model,next->model);supported(next->prefix-(old->model.tracks.at(old->ids[0]).initializationReady?old->prefix:0)<=harvester::ts::MaxSegmentBytes,"Publish at most 16 MiB of growing bytes at a time");try{source->publish(json);materialize(*next,old.get());std::shared_ptr<const Published> accepted=std::move(next);std::atomic_store(&current,std::move(accepted));}catch(const harvester::ts::Error& e){fail(e.code,e.what());throw Error(e.code.c_str(),"growingTs",e.what());}catch(const Error& e){fail(e.code,e.what());throw;}catch(const std::exception& e){fail("growing-ts-error",e.what());throw;}}
    Snapshot model() const{return std::atomic_load(&current)->model;}
    std::shared_ptr<const harvester::ts::AccessIndex> index(unsigned kind) const{check();Require(kind<2,"track","Unknown growing track","unknown-track");return std::atomic_load(&current)->parser->index[kind];}
    size_t seekSample(unsigned kind,int64_t position) const{const auto i=index(kind);return i?i->seekSample(position):0;}
    void ensureOpen() const{check();}
    bool usesCatalog() const{return false;}
    bool isMuxed() const{return true;}
    Status status() const{const auto state=std::atomic_load(&current);Status out;out.revision=state->model.revision;out.resources=1;out.complete=state->complete;out.available=state->model.duration;for(unsigned k=0;k<2;++k){out.bytes[k]=state->prefix;out.trackComplete[k]=state->complete;const auto& i=state->parser->index[k];out.initialized[k]=i && !i->samples().empty();if(i){out.units[k]=i->samples().size();out.ready[k]=(std::max)(int64_t(0),(std::min)(state->model.duration,i->readyEnd()));}out.available=(std::min)(out.available,out.ready[k]);}if(cancelled.load())out.code="cancelled";else if(failed.load()){std::lock_guard<std::mutex> lock(errors);out.code=errorCode.empty()?"session_closed":errorCode;}if(!out.code.empty()){out.available=0;out.complete=false;}return out;}
    Packet sample(unsigned kind,size_t position,bool encoded=false){Packet out;try{check();Require(kind<2,"track","Unknown growing track","unknown-track");const auto state=std::atomic_load(&current);const auto& index=state->parser->index[kind];if(!index || position>=index->samples().size()){out.result={state->complete?"eof":"waiting-segment","Wait for complete PES/GOP",0};return out;}const auto& unit=index->samples()[position];harvester::ts::Pes pes;pes.transportOffset=size_t(unit.sample.offset);pes.transportEnd=size_t(unit.sample.offset+unit.sample.size);pes.payload=encoded?unit.encoded:unit.payload;for(const auto& r:pes.payload)pes.size+=r.size;const auto bytes=gather(state->resource,pes);check();out.bytes.assign(bytes.begin(),bytes.end());out.sample=unit.sample;out.result={"data","Verified growing TS access unit",out.bytes.size()};}catch(const Error& e){if(e.code!="cancelled")fail(e.code,e.what());out.bytes.clear();out.result={e.code,e.what(),0};}return out;}
    ResourceRead read(unsigned kind,uint64_t offset,void* data,size_t count){
        try{check();if(kind>=2 || offset>MaxByte || count>65536 || count>MaxByte-offset || (!data && count))return {"invalid-read","Invalid bounded growing read",0};
            const auto state=std::atomic_load(&current);if(offset>state->prefix)return {state->complete?"out-of-range":"waiting-range","Growing byte tail",0};
            if(!count)return {"data","Empty growing read",0};if(offset==state->prefix)return {state->complete?"eof":"waiting-range","Growing byte tail",0};
            const auto result=source->read(state->resource,offset,data,size_t((std::min)(uint64_t(count),state->prefix-offset)));if(!result.data())fail(result.code,result.reason);return result;
        }catch(const Error& e){return {e.code,e.what(),0};}
    }
    std::vector<BoundResource> bindings() const{const auto state=std::atomic_load(&current);std::vector<BoundResource> out;for(const auto& id:state->ids){auto b=source->binding(state->resource);b.id=id;out.push_back(std::move(b));}return out;}
    void verify(){check();try{source->verifyPinned();}catch(const Error& e){fail(e.code,e.what());throw;}}
    std::vector<std::string> readerIds() const{return source->readerIds();}
    std::shared_ptr<std::atomic<bool>> releaseReceipt() const{return source->releaseReceipt();}
};

class TsPlaybackInput {
    std::unique_ptr<TsResourceStream> segments;std::unique_ptr<TsGrowingStream> growing;
public:
    using Status=TsResourceStream::Status;using Packet=TsResourceStream::Packet;
    static bool Declared(const std::string& json){return TsGrowingStream::Declared(json) || TsResourceStream::Declared(json);}
    static void CheckProfile(const std::string& json){if(TsGrowingStream::Declared(json))TsGrowingStream::CheckProfile(json);else TsResourceStream::CheckProfile(json);}
    TsPlaybackInput(const std::filesystem::path& root,const std::filesystem::path& registry,const std::string& json){if(TsGrowingStream::Declared(json))growing=std::make_unique<TsGrowingStream>(root,registry,json);else segments=std::make_unique<TsResourceStream>(root,registry,json);}
    bool isGrowing() const{return bool(growing);}
    bool isMuxed() const{return growing || segments->isMuxed();}
    bool usesCatalog() const{return !growing && segments->usesCatalog();}
    Snapshot model() const{return growing?growing->model():segments->model();}
    Status status() const{return growing?growing->status():segments->status();}
    auto index(unsigned k) const{return growing?growing->index(k):segments->index(k);}
    size_t seekSample(unsigned k,int64_t at) const{return growing?growing->seekSample(k,at):segments->seekSample(k,at);}
    Packet sample(unsigned k,size_t i,bool encoded=false){return growing?growing->sample(k,i,encoded):segments->sample(k,i,encoded);}
    ResourceRead read(unsigned k,uint64_t at,void* p,size_t n){return growing?growing->read(k,at,p,n):segments->read(k,at,p,n);}
    void ensureOpen() const{if(growing)growing->ensureOpen();else segments->ensureOpen();}
    void verify(){if(growing)growing->verify();else segments->verify();}
    void publish(const std::string& json){if(growing)growing->publish(json);else segments->publish(json);}
    void cancel(){if(growing)growing->cancel();else segments->cancel();}
    auto bindings() const{return growing?growing->bindings():segments->bindings();}
    auto readerIds() const{return growing?growing->readerIds():segments->readerIds();}
    auto releaseReceipt() const{return growing?growing->releaseReceipt():segments->releaseReceipt();}
};
}} // namespace foxicmp::progressive
