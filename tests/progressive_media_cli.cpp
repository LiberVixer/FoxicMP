// Native media-profile fixture: no Qt, DirectShow or decoder dependencies.
#if defined(FOXICMP_INDEXED_MP4_ONLY) || defined(FOXICMP_TAIL_MP4_ONLY)
#include "../shared/progressiveindexedmp4.h"
#else
#include "../shared/progressivefmp4bridge.h"
#endif
#include <iostream>
namespace p=foxicmp::progressive;
#if defined(FOXICMP_INDEXED_MP4_ONLY) || defined(FOXICMP_TAIL_MP4_ONLY)
#ifdef FOXICMP_TAIL_MP4_ONLY
using FocusBridge=p::TailMp4Bridge;
#else
using FocusBridge=p::IndexedMp4Bridge;
#endif
// Focus sanitizers on the new projection and its real resource/lease backend.
// Regular tests still exercise the production Fmp4V2Bridge and codec validator.
class TestedBridge:public FocusBridge {
public:
    TestedBridge(const std::filesystem::path& root,const std::filesystem::path& receipts,const std::string& json)
        :FocusBridge(root,receipts,json,[](unsigned,harvester::BoxData){}){}
    int64_t readDecoderPayload(int track,uint64_t offset,void* buffer,size_t count){return FocusBridge::readPayload(unsigned(track),offset,buffer,count,true);}
    bool isWebm(int)const{return false;}
    std::string audioCodec(int)const{return FocusBridge::audioCodec();}
};
#else
using TestedBridge=p::Fmp4V2Bridge;
#endif

int main(int argc,char** argv) {
    if((argc!=3 && argc!=4) || std::string(argv[1])!="--registry" || (argc==4 && std::string(argv[3])!="--mp3-gapless"))return 2;
    const bool allowGapless=argc==4;
    auto registry=std::filesystem::u8path(argv[2]);p::lease_detail::Directory directory(registry,true);
    std::shared_ptr<TestedBridge> bridge,retained;std::string line;
    while(std::getline(std::cin,line))try {
        rapidjson::Document d;d.Parse(line.c_str());p::Require(!d.HasParseError() && d.IsObject(),"$","Invalid command");
        const auto action=p::Text(d["action"],"action");
        if(action=="preflight"){
#if defined(FOXICMP_INDEXED_MP4_ONLY) || defined(FOXICMP_TAIL_MP4_ONLY)
            TestedBridge::CheckProfile(p::lease_detail::Encode(d["session"]));
#else
            TestedBridge::CheckProfile(p::lease_detail::Encode(d["session"]),allowGapless);
#endif
            std::cout<<"{\"ok\":true}"<<std::endl;continue;}
        if(action=="readers"){std::cout<<p::ReaderRequest(registry,line)<<std::endl;continue;}
        if(action=="open")bridge=std::make_shared<TestedBridge>(std::filesystem::u8path(p::Text(d["directory"],"directory",8192)),registry,p::lease_detail::Encode(d["session"])
#if !defined(FOXICMP_INDEXED_MP4_ONLY) && !defined(FOXICMP_TAIL_MP4_ONLY)
            ,allowGapless
#endif
        );
        else if(action=="release")bridge.reset();
        else if(action=="retain")retained=bridge;
        else if(action=="dropRetained")retained.reset();
        else {
            p::Require(bool(bridge),"source","No media source");
            if(action=="publish")bridge->publish(p::lease_detail::Encode(d["session"]));
            else if(action=="cancel")bridge->cancel();
            else if(action=="verify")bridge->verify();
            else if(action=="status") {std::cout<<bridge->details()<<std::endl;continue;}
            else if(action=="layout") {const auto s=bridge->snapshot();rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();p::Value tracks(rapidjson::kArrayType);for(const auto& t:s.tracks){p::Value v(rapidjson::kObjectType);v.AddMember("available",p::Value(std::to_string(t.available).c_str(),a),a);v.AddMember("total",p::Value(std::to_string(t.total).c_str(),a),a);v.AddMember("initialized",t.initializationReady,a);tracks.PushBack(v,a);}out.AddMember("tracks",tracks,a);out.AddMember("duration100ns",p::Value(std::to_string(s.duration).c_str(),a),a);out.AddMember("timestampOffset100ns",p::Value(std::to_string(s.timestampOffset).c_str(),a),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;continue;}
            else if(action=="index") {const auto s=bridge->snapshot();rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();p::Value tracks(rapidjson::kArrayType);for(size_t i=0;i<s.tracks.size();++i){harvester::FragmentIndex index;index.Update([&](uint64_t at,void* bytes,size_t count){return bridge->readIndex(int(i),at,bytes,count);},s.tracks[i].available,i || s.tracks[i].audio?1:0,harvester::EndReady(s.tracks[i]),s.timestampOffset,bridge->videoCodec()=="hevc",bridge->videoCodec()=="av1",bridge->videoCodec()=="vp9",bridge->audioCodec(int(i?i:1))=="opus",bridge->isWebm(int(i)),bridge->isStandaloneAudio() && bridge->audioCodec(int(i?i:1))=="flac",bridge->isStandaloneAudio() && bridge->audioCodec(int(i?i:1))=="pcm-s16le",bridge->isStandaloneAudio() && bridge->audioCodec(int(i?i:1))=="ac3",bridge->isStandaloneAudio() && bridge->audioCodec(int(i?i:1))=="eac3");p::Value v(rapidjson::kObjectType);v.AddMember("samples",uint32_t(index.Samples().size()),a);v.AddMember("position",p::Value(std::to_string(index.Position()).c_str(),a),a);v.AddMember("ready",p::Value(std::to_string(harvester::ReadyEnd(index.Samples(),s.tracks[i].available,s.duration)).c_str(),a),a);if(d.HasMember("sampleTimes") && d["sampleTimes"].IsBool() && d["sampleTimes"].GetBool()){p::Value times(rapidjson::kArrayType);size_t count=0;for(const auto& sample:index.Samples()){if(count++==8)break;p::Value time(rapidjson::kObjectType);time.AddMember("start",p::Value(std::to_string(sample.start).c_str(),a),a);time.AddMember("end",p::Value(std::to_string(sample.end).c_str(),a),a);times.PushBack(time,a);}v.AddMember("firstSamples",times,a);}tracks.PushBack(v,a);}out.AddMember("tracks",tracks,a);std::cout<<p::lease_detail::Encode(out)<<std::endl;continue;}
            else if(action=="read") {
                const auto track=d["track"].GetInt();p::Require(track>=0 && size_t(track)<bridge->snapshot().tracks.size(),"track","Bad track");
                const auto offset=p::Unsigned(d["offset"],"offset",p::MaxByte);const auto count=size_t(p::Unsigned(d["length"],"length",65536));
                std::vector<char> bytes(count);const auto size=d.HasMember("decoder") && d["decoder"].IsBool() && d["decoder"].GetBool()?bridge->readDecoderPayload(track,offset,bytes.data(),count):bridge->readPayload(track,offset,bytes.data(),count);
                std::cout<<"{\"ok\":true,\"bytesRead\":"<<size<<",\"hex\":\"";
                const char* digits="0123456789abcdef";for(int64_t i=0;i<size;++i){const auto c=static_cast<unsigned char>(bytes[size_t(i)]);std::cout<<digits[c>>4]<<digits[c&15];}
                std::cout<<"\"}"<<std::endl;continue;
            }else p::Require(false,"action","Unknown command");
        }
        std::cout<<"{\"ok\":true}"<<std::endl;
    }catch(const p::Error& error){rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);out.AddMember("code",p::Value(error.code.c_str(),a),a);out.AddMember("error",p::Value(error.what(),a),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;}
    catch(const std::exception& error){rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);out.AddMember("code","media_error",a);out.AddMember("error",p::Value(error.what(),a),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;}
    return 0;
}
