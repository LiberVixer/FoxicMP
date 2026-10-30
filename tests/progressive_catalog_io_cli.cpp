// Test-only local catalog adapter. Never launches a player or touches a profile.
#include "../shared/progressivelocalcatalog.h"
#include <future>
#include <iostream>
namespace p=foxicmp::progressive;
std::string Encode(const rapidjson::Value& value) {return p::lease_detail::Encode(value);}
std::string Publish(const std::shared_ptr<p::LocalSegmentCatalog>& source,const std::string& json) {
    try{source->publish(json);return "{\"ok\":true}";}
    catch(const p::Error& error) {
        rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);
        out.AddMember("code",p::Value(error.code.c_str(),a),a);out.AddMember("error",p::Value(error.what(),a),a);return Encode(out);
    }
}
int main() {
    std::shared_ptr<p::LocalSegmentCatalog> source;std::future<std::string> pending;std::string line;
    while(std::getline(std::cin,line))try {
        rapidjson::Document input;input.Parse(line.c_str());p::Require(!input.HasParseError() && input.IsObject(),"$","Invalid command");
        const auto action=p::Text(input["action"],"action");
        if(action=="cancel"){if(source)source->cancel();std::cout<<"{\"ok\":true}"<<std::endl;continue;}
        if(action=="result") {
            p::Require(pending.valid(),"action","No pending publish");
            if(pending.wait_for(std::chrono::milliseconds(0))==std::future_status::ready)std::cout<<pending.get()<<std::endl;
            else std::cout<<"{\"ok\":true,\"pending\":true}"<<std::endl;
            continue;
        }
        p::Require(!pending.valid(),"action","Wait for the pending publish before accessing its owner");
        if(action=="open")source=std::make_shared<p::LocalSegmentCatalog>(std::filesystem::u8path(p::Text(input["directory"],"directory",8192)));
        else if(action=="release")source.reset();
        else if(action=="publish" || action=="beginPublish") {
            p::Require(bool(source),"catalog","No catalog owner");const auto json=Encode(input["session"]);
            if(action=="publish"){std::cout<<Publish(source,json)<<std::endl;continue;}
            auto started=std::make_shared<std::promise<void>>();auto ready=started->get_future();
            pending=std::async(std::launch::async,[owned=source,json,started]{started->set_value();return Publish(owned,json);});ready.wait();
        }else if(action=="status") {
            p::Require(bool(source),"catalog","No catalog owner");const auto& catalog=source->catalog();
            std::cout<<"{\"ok\":true,\"resources\":"<<catalog.snapshot.resources.size()<<",\"pages\":"<<catalog.pages.size()<<",\"revision\":\""<<catalog.snapshot.revision<<"\"}"<<std::endl;continue;
        }else if(action=="handles") {
            unsigned count=0;
#ifdef _WIN32
            DWORD handles=0;p::Require(GetProcessHandleCount(GetCurrentProcess(),&handles)!=FALSE,"test","Cannot inspect handles");count=handles;
#else
            DIR* directory=opendir("/proc/self/fd");p::Require(directory!=nullptr,"test","Cannot inspect handles");
            while(auto entry=readdir(directory))if(entry->d_name[0]!='.')++count;closedir(directory);
#endif
            std::cout<<"{\"ok\":true,\"handles\":"<<count<<"}"<<std::endl;continue;
        }else p::Require(false,"action","Unknown command");
        std::cout<<"{\"ok\":true}"<<std::endl;
    }catch(const p::Error& error) {
        rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);
        out.AddMember("code",p::Value(error.code.c_str(),a),a);out.AddMember("error",p::Value(error.what(),a),a);std::cout<<Encode(out)<<std::endl;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 3;}
    if(source)source->cancel();if(pending.valid())pending.wait();source.reset();return 0;
}
