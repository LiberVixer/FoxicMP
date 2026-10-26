// Asynchronous diagnostic bridge for the v2 I/O adapter. GPL-3.0-or-later.
// Does NOT activate playback or expose media bytes through IPC.
#pragma once
#include "progressiveresourceio.h"
#include <deque>
#include <thread>

namespace foxicmp { namespace progressive {
class ResourceProbeManager {
    struct Query {std::string resource;uint64_t offset=0;size_t count=0;};
    struct Result {Query query;ResourceRead read;};
    struct Job {
        std::string id,requestId,requestFingerprint,initial,phase="queued",error;
        Snapshot accepted;
        std::filesystem::path directory,registry;
        std::vector<Query> queries;std::vector<Result> results;std::vector<std::string> readers;
        std::deque<std::string> updates;
        uint64_t appliedRevision=0;unsigned waitMs=0;
        std::mutex mutex;std::condition_variable changed;
        std::atomic<bool> cancel{false},stop{false},done{false};
        bool forgetRequested=false,finished=false;
        std::set<std::string> forgotten;
        std::weak_ptr<ResourceSource> source;
        std::shared_ptr<std::atomic<bool>> released=std::make_shared<std::atomic<bool>>(false);
        std::thread worker;
        ~Job(){stop.store(true);requestCancel();if(worker.joinable())worker.join();}
        void requestCancel() {
            cancel.store(true);
            std::shared_ptr<ResourceSource> active;{std::lock_guard<std::mutex> lock(mutex);active=source.lock();}
            if(active)active->cancel();changed.notify_all();
        }
        void apply(ResourceSource& active) {
            std::deque<std::string> pending;
            {std::lock_guard<std::mutex> lock(mutex);pending.swap(updates);}
            for(const auto& json:pending){active.publish(json);std::lock_guard<std::mutex> lock(mutex);appliedRevision=Parse(json).revision;}
        }
        void run() {
            std::shared_ptr<ResourceSource> active;
            std::string terminal="finished",failure;
            try {
                active=std::make_shared<ResourceSource>(directory,registry,initial);
                {std::lock_guard<std::mutex> lock(mutex);source=active;released=active->releaseReceipt();appliedRevision=Parse(initial).revision;phase="running";}
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(waitMs);
                std::vector<char> data(65536);
                for(const auto& query:queries) {
                    ResourceRead read;
                    for(;;) {
                        if(cancel.load() || stop.load())active->cancel();
                        apply(*active);
                        read=active->read(query.resource,query.offset,data.data(),query.count);
                        if((read.code!="waiting-range" && read.code!="waiting-resource") || std::chrono::steady_clock::now()>=deadline)break;
                        std::unique_lock<std::mutex> lock(mutex);
                        changed.wait_until(lock,(std::min)(deadline,std::chrono::steady_clock::now()+std::chrono::milliseconds(25)),[&]{return cancel.load() || stop.load() || !updates.empty();});
                    }
                    {std::lock_guard<std::mutex> lock(mutex);results.push_back({query,read});}
                    if(read.code=="cancelled"){terminal="cancelled";break;}
                    if(read.code!="data" && read.code!="eof" && read.code!="waiting-range" && read.code!="waiting-resource" && read.code!="resource-retired") {
                        terminal="error";failure=read.code;break;
                    }
                }
            }catch(const Error& e){terminal=cancel.load()?"cancelled":"error";failure=e.code;}
            catch(const std::exception&){terminal="error";failure="resource-io-error";}
            {std::lock_guard<std::mutex> lock(mutex);phase="closing";if(active)readers=active->readerIds();}
            active.reset(); // Actual media handles close before the job reports a release.
            {
                std::lock_guard<std::mutex> lock(mutex);
                if(source.expired() && readers.empty())released->store(true);
                finished=true;phase=terminal;error=failure;
            }
            changed.notify_all();
            // Receipt acknowledgement is explicit. Shutdown leaves closed receipts
            // available for readerStatus/readers after process restart.
            for(;;) {
                {std::unique_lock<std::mutex> lock(mutex);changed.wait(lock,[&]{return stop.load() || forgetRequested;});if(stop.load())break;forgetRequested=false;}
                try {
                    for(const auto& reader:readers) {
                        if(forgotten.count(reader))continue;
                        const auto text=ReaderRequest(registry,"{\"version\":1,\"action\":\"readerForget\",\"readerId\":\""+reader+"\"}");
                        rapidjson::Document reply;reply.Parse(text.c_str());
                        resource_detail::Check(reply.IsObject() && reply.HasMember("ok") && reply["ok"].IsBool() && reply["ok"].GetBool(),"receipt-cleanup-failed","Cannot acknowledge a closed resource receipt");
                        forgotten.insert(reader);
                    }
                    std::lock_guard<std::mutex> lock(mutex);phase="forgotten";error.clear();done.store(true);break;
                }catch(const Error& e){std::lock_guard<std::mutex> lock(mutex);phase="error";error=e.code;}
                catch(const std::exception&){std::lock_guard<std::mutex> lock(mutex);phase="error";error="receipt-cleanup-failed";}
            }
            done.store(true);
        }
        std::string status() {
            std::lock_guard<std::mutex> lock(mutex);rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();
            out.AddMember("version",1,a);out.AddMember("ok",true,a);out.AddMember("mode","resource-io-only",a);out.AddMember("playbackSupported",false,a);
            out.AddMember("probeId",Value(id.c_str(),a),a);out.AddMember("sessionId",Value(accepted.id.c_str(),a),a);
            out.AddMember("requestId",Value(requestId.c_str(),a),a);
            out.AddMember("generation",Value(std::to_string(accepted.generation).c_str(),a),a);
            out.AddMember("acceptedRevision",Value(std::to_string(accepted.revision).c_str(),a),a);out.AddMember("appliedRevision",Value(std::to_string(appliedRevision).c_str(),a),a);
            out.AddMember("state",Value(phase.c_str(),a),a);out.AddMember("errorCode",Value(error.c_str(),a),a);
            out.AddMember("released",released->load(),a);Value ids(rapidjson::kArrayType);
            for(const auto& reader:readers)ids.PushBack(Value(reader.c_str(),a),a);out.AddMember("readerIds",ids,a);
            Value reads(rapidjson::kArrayType);bool allReadable=finished && results.size()==queries.size();
            for(const auto& result:results) {
                Value r(rapidjson::kObjectType);r.AddMember("resourceId",Value(result.query.resource.c_str(),a),a);
                r.AddMember("offset",Value(std::to_string(result.query.offset).c_str(),a),a);r.AddMember("length",Value(std::to_string(result.query.count).c_str(),a),a);
                r.AddMember("code",Value(result.read.code.c_str(),a),a);r.AddMember("reason",Value(result.read.reason.c_str(),a),a);r.AddMember("bytesRead",uint32_t(result.read.bytes),a);reads.PushBack(r,a);
                allReadable=allReadable && ((result.read.data() && result.read.bytes==result.query.count) || result.read.code=="eof");
            }
            out.AddMember("reads",reads,a);out.AddMember("allReadable",allReadable,a);return lease_detail::Encode(out);
        }
    };
    std::unique_ptr<Job> job;
    std::mutex dispatch;
public:
    ResourceProbeManager()=default;
    ~ResourceProbeManager()=default;
    std::string request(const std::filesystem::path& registry,const std::string& json) {
        std::lock_guard<std::mutex> serialized(dispatch);
        try {
            resource_detail::Check(json.size()<=harvester::MaxManifestBytes,"invalid_request","Probe request exceeds limit");
            rapidjson::Document d;d.Parse<rapidjson::kParseValidateEncodingFlag|rapidjson::kParseIterativeFlag>(json.data(),json.size());
            resource_detail::Check(!d.HasParseError() && d.IsObject(),"invalid_request","Invalid resource probe JSON");harvester::UniqueKeys(d);
            resource_detail::Check(d.HasMember("version") && d["version"].IsUint() && d["version"].GetUint()==1 && d.HasMember("action"),"invalid_request","Expected API version 1");
            const auto action=Text(d["action"],"action",32);
            if(action=="resourceProbe") {
                Keys(d,"request",{"version","action","session","sessionDirectory","reads"},{"waitMilliseconds","requestId"});
                const auto requestId=d.HasMember("requestId")?Id(d["requestId"],"requestId",true):lease_detail::RandomId();
                rapidjson::StringBuffer canonical;rapidjson::Writer<rapidjson::StringBuffer> writer(canonical);Canonical(d,writer);
                const std::string fingerprint(canonical.GetString(),canonical.GetSize());
                if(job && d.HasMember("requestId") && requestId==job->requestId) {
                    resource_detail::Check(fingerprint==job->requestFingerprint,"probe_conflict","A request identifier cannot be reused for a different probe");return job->status();
                }
                if(job && !job->done.load())throw Error("probe_busy","probeId","Acknowledge the previous resource probe first");
                auto next=std::make_unique<Job>();next->id=lease_detail::RandomId();next->requestId=requestId;next->requestFingerprint=fingerprint;next->registry=registry;
                next->directory=std::filesystem::u8path(Text(d["sessionDirectory"],"sessionDirectory",8192));
                next->initial=lease_detail::Encode(d["session"]);next->accepted=Parse(next->initial);
                if(d.HasMember("waitMilliseconds")){resource_detail::Check(d["waitMilliseconds"].IsUint() && d["waitMilliseconds"].GetUint()<=10000,"invalid_request","Probe wait limit is 10000 ms");next->waitMs=d["waitMilliseconds"].GetUint();}
                resource_detail::Check(d["reads"].IsArray() && d["reads"].Size()>0 && d["reads"].Size()<=16,"invalid_request","Expected 1..16 read queries");
                for(const auto& item:d["reads"].GetArray()) {
                    Keys(item,"reads[]",{"resourceId","offset","length"});Query query;
                    query.resource=Id(item["resourceId"],"resourceId");query.offset=Unsigned(item["offset"],"offset",MaxByte);query.count=size_t(Unsigned(item["length"],"length",65536));
                    resource_detail::Check(query.count<=MaxByte-query.offset && next->accepted.resources.count(query.resource)!=0,"invalid_request","Read resource/offset is invalid");next->queries.push_back(query);
                }
                next->worker=std::thread([raw=next.get()]{raw->run();});job=std::move(next);return job->status();
            }
            if(action=="resourceProbeStatus")Keys(d,"request",{"version","action"},{"probeId"});
            else if(action=="resourceProbeUpdate")Keys(d,"request",{"version","action","probeId","session"});
            else {resource_detail::Check(action=="resourceProbeCancel" || action=="resourceProbeForget","invalid_request","Unknown resource probe command");Keys(d,"request",{"version","action","probeId"});}
            resource_detail::Check(job!=nullptr,"unknown_probe","Resource probe is unknown");
            if(d.HasMember("probeId"))resource_detail::Check(Id(d["probeId"],"probeId",true)==job->id,"unknown_probe","Resource probe identifier does not match");
            if(action=="resourceProbeCancel")job->requestCancel();
            else if(action=="resourceProbeUpdate") {
                const auto snapshotJson=lease_detail::Encode(d["session"]);const auto parsed=Parse(snapshotJson);std::lock_guard<std::mutex> lock(job->mutex);
                resource_detail::Check(!job->finished && (job->phase=="queued" || job->phase=="running") && !job->cancel.load(),"probe_closed","Resource probe no longer accepts updates");
                resource_detail::Check(job->updates.size()<8,"probe_backpressure","Resource probe update queue is full");Advance(job->accepted,parsed);
                job->updates.push_back(snapshotJson);job->accepted=parsed;job->changed.notify_all();
            }else if(action=="resourceProbeForget") {
                std::lock_guard<std::mutex> lock(job->mutex);
                resource_detail::Check(job->finished && job->released->load(),"reader_in_use","Wait until the resource probe closes its files");
                if(job->phase!="forgotten"){job->forgetRequested=true;job->phase="forgetting";job->changed.notify_all();}
            }
            return job->status();
        }catch(const Error& e) {
            rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("version",1,a);out.AddMember("ok",false,a);
            out.AddMember("code",Value(e.code.c_str(),a),a);out.AddMember("error",Value(e.what(),a),a);return lease_detail::Encode(out);
        }catch(const std::exception&) {return "{\"version\":1,\"ok\":false,\"code\":\"resource-io-error\",\"error\":\"Resource probe failed\"}";}
    }
};
}} // namespace foxicmp::progressive
