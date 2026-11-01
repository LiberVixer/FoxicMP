// Metadata-only range hints; never downloads or activates playback.
#pragma once
#include "progressiveresourceio.h"
#include "DSUtil/HarvesterMP4IndexLocator.h"
namespace foxicmp { namespace progressive {
inline std::string ProbeMp4Index(ResourceSource& source,const std::string& id){
    rapidjson::Document result;result.SetObject();auto& a=result.GetAllocator();result.AddMember("mode","mp4-index-locator",a);result.AddMember("analysisOnly",true,a);result.AddMember("playbackSupported",false,a);result.AddMember("resourceId",Value(id.c_str(),a),a);Value requests(rapidjson::kArrayType);
    std::string code="data",reason;
    try{const auto resource=source.describe(id);Require(resource.known && resource.length>0,"mp4Index","Known source length is required","unsupported_media");Require(resource.integrity,"mp4Index","Index reads require SHA-256","integrity-required");
        const auto available=[&](uint64_t at,size_t n){if(at>resource.length || n>resource.length-at)return false;uint64_t cursor=at,end=at+n;for(const auto& r:resource.ranges){if(r.offset>cursor)break;if(r.offset+r.length>cursor)cursor=(std::min)(end,r.offset+r.length);if(cursor==end)return true;}return n==0;};
        const auto read=[&](uint64_t at,void* out,size_t n){auto* bytes=static_cast<char*>(out);for(size_t done=0;done<n;){const auto part=(std::min)(size_t(65536),n-done);const auto r=source.read(id,at+done,bytes+done,part);Require(r.data() && r.bytes==part,"mp4Index",r.reason.c_str(),r.code.c_str());done+=part;}return true;};
        const auto found=harvester::mp4index::Locate(read,available,resource.length,true);
        result.AddMember("moovOffset",Value(std::to_string(found.moovOffset).c_str(),a),a);result.AddMember("moovBytes",Value(std::to_string(found.moovBytes).c_str(),a),a);result.AddMember("mdatEnd",Value(std::to_string(found.payloadEnd).c_str(),a),a);result.AddMember("tailIndex",found.moovOffset>=found.payloadEnd,a);
    }catch(const harvester::mp4index::NeedRange& need){code="waiting-index";reason=need.what();Value request(rapidjson::kObjectType);request.AddMember("resourceId",Value(id.c_str(),a),a);request.AddMember("offset",Value(std::to_string(need.offset).c_str(),a),a);request.AddMember("length",Value(std::to_string(need.length).c_str(),a),a);request.AddMember("reason",Value(need.reason.c_str(),a),a);request.AddMember("advisory",true,a);requests.PushBack(request,a);}
    catch(const Error& e){code=e.code;reason=e.what();}catch(const std::exception& e){code="unsupported_media";reason=e.what();}
    result.AddMember("code",Value(code.c_str(),a),a);result.AddMember("reason",Value(reason.c_str(),a),a);result.AddMember("priorityRanges",requests,a);return lease_detail::Encode(result);
}
}}
