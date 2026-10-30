// Pure catalog-model fixture. No filesystem/media/backend access.
#include "../shared/progressivesegmentcatalog.h"
#include <iostream>
namespace p=foxicmp::progressive;
std::string Encode(const rapidjson::Value& value) {
    rapidjson::StringBuffer buffer;rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);value.Accept(writer);return {buffer.GetString(),buffer.GetSize()};
}
int main() {
    std::unique_ptr<p::SegmentCatalog> current;std::string line;
    while(std::getline(std::cin,line)) {
        size_t loads=0;
        try {
            rapidjson::Document d;d.Parse(line.c_str());p::Require(!d.HasParseError() && d.IsObject(),"$","Invalid command");
            const auto action=p::Text(d["action"],"action");
            if(action=="inline")p::Parse(Encode(d["session"]));
            else if(action!="status") {
                p::Require(action=="decode" || action=="advance","action","Unknown command");
                auto next=p::DecodeSegmentCatalog(Encode(d["session"]),[&](const p::SegmentCatalogPage& page) {
                    ++loads;p::Require(d.HasMember("pages") && d["pages"].IsObject() && d["pages"].HasMember(page.name.c_str()),"page","Missing catalog page","waiting-index");
                    const auto& value=d["pages"][page.name.c_str()];p::Require(value.IsString(),"page","Expected page bytes");
                    return std::string(value.GetString(),value.GetStringLength());
                });
                if(action=="advance"){p::Require(bool(current),"catalog","No previous catalog");p::AdvanceSegmentCatalog(*current,next);}
                current=std::make_unique<p::SegmentCatalog>(std::move(next));
            }
            rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);out.AddMember("loads",unsigned(loads),a);
            if(current) {
                out.AddMember("resources",unsigned(current->snapshot.resources.size()),a);out.AddMember("pages",unsigned(current->pages.size()),a);
                out.AddMember("revision",p::Value(std::to_string(current->snapshot.revision).c_str(),a),a);
                p::Value tracks(rapidjson::kObjectType),starts(rapidjson::kObjectType);
                for(const auto& item:current->snapshot.tracks) {
                    p::Value name(item.first.c_str(),a);tracks.AddMember(name,unsigned(item.second.segments.size()),a);
                    if(!item.second.segments.empty()) {
                        p::Value key(item.first.c_str(),a),value(std::to_string(item.second.segments.front().start).c_str(),a);
                        starts.AddMember(key,value,a);
                    }
                }
                out.AddMember("segments",tracks,a);out.AddMember("firstStarts",starts,a);
            }
            rapidjson::StringBuffer buffer;rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);out.Accept(writer);std::cout<<buffer.GetString()<<std::endl;
        }catch(const p::Error& e) {
            rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);out.AddMember("loads",unsigned(loads),a);
            out.AddMember("code",p::Value(e.code.c_str(),a),a);out.AddMember("path",p::Value(e.path.c_str(),a),a);out.AddMember("error",p::Value(e.what(),a),a);
            rapidjson::StringBuffer buffer;rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);out.Accept(writer);std::cout<<buffer.GetString()<<std::endl;
        }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 3;}
    }
    return 0;
}
