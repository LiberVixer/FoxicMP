// Portable conformance runner for the shared v2 model. No Qt/MFC/OS dependencies.
#include "../shared/progressivereaders.h"
#include <iostream>
namespace p=foxicmp::progressive;
std::string encode(const rapidjson::Value& v) {
    rapidjson::StringBuffer b;rapidjson::Writer<rapidjson::StringBuffer> w(b);v.Accept(w);return {b.GetString(),b.GetSize()};
}
int main() {
    std::string line;
    while(std::getline(std::cin,line)) {
        rapidjson::StringBuffer out;rapidjson::Writer<rapidjson::StringBuffer> w(out);w.StartObject();
        try {
            rapidjson::Document d;d.Parse(line.data(),line.size());
            p::Require(!d.HasParseError() && d.IsObject() && d.HasMember("session"),"$","Expected a session");
            p::Keys(d,"$",{"session"},{"previous","range","readerExercise"});
            auto current=p::Parse(encode(d["session"]));
            if(d.HasMember("previous"))p::Advance(p::Parse(encode(d["previous"])),current);
            bool covered=false;
            if(d.HasMember("range")) {
                const auto& range=d["range"];p::Keys(range,"range",{"resourceId","offset","length"});auto id=p::Id(range["resourceId"],"resourceId");
                p::Require(current.resources.count(id)!=0,"resourceId","Unknown resource");
                covered=current.resources.at(id).covers(p::Unsigned(range["offset"],"offset",p::MaxByte),p::Unsigned(range["length"],"length",p::MaxByte));
            }
            if(d.HasMember("readerExercise")) {
                p::ReaderBook book;const std::string id(32,'a'),other(32,'b');
                p::ProcessIdentity peer{42,"boot-A:100"},reused{42,"boot-A:200"};
                auto resource=current.resources.begin()->first;std::set<std::string> scope{resource};
                book.attach(current,id,peer,scope);book.attach(current,other,peer,scope);
                book.attach(current,id,peer,scope); // Idempotent attachment.
                bool premature=false;try {book.filesClosed(id,peer);}catch(const p::Error& e){premature=e.code=="reader_not_closing";}
                p::Require(premature,"reader","Premature release was accepted");
                p::Require(book.inUse(current.id,current.generation,resource),"reader","No live lease");
                book.beginRelease(id,peer);book.observe(other,p::ProcessObservation::Unknown);
                p::Require(book.inUse(current.id,current.generation,resource),"reader","Closing/uncertain readers lost leases");
                book.filesClosed(id,peer);p::Require(book.inUse(current.id,current.generation,resource),"reader","Released one reader erased another");
                bool rejected=false;try {book.beginRelease(other,reused);}catch(const p::Error& e){rejected=e.code=="reader_owner_mismatch";}
                p::Require(rejected,"reader","PID reuse was not rejected");
                book.observe(other,p::ProcessObservation::ReusedPid);
                p::Require(!book.inUse(current.id,current.generation,resource),"reader","Confirmed dead readers retained leases");
                book.forgetReleased(id);book.forgetReleased(other);
                for(unsigned i=0;i<p::MaxReaders;++i) {
                    std::string key(32,'0');const char* hex="0123456789abcdef";key[30]=hex[i/16];key[31]=hex[i%16];book.attach(current,key,peer,scope);
                }
                bool limited=false;try {book.attach(current,std::string(32,'f'),peer,scope);}catch(const p::Error& e){limited=e.code=="reader_limit";}
                p::Require(limited,"reader","Unbounded reader registration");
            }
            w.Key("ok");w.Bool(true);w.Key("rangeCovered");w.Bool(covered);
            w.Key("generation");w.String(std::to_string(current.generation).c_str());
            w.Key("revision");w.String(std::to_string(current.revision).c_str());
        }catch(const p::Error& e){w.Key("ok");w.Bool(false);w.Key("code");w.String(e.code.c_str());w.Key("path");w.String(e.path.c_str());w.Key("error");w.String(e.what());}
        catch(const std::exception& e){w.Key("ok");w.Bool(false);w.Key("error");w.String(e.what());}
        w.EndObject();std::cout<<out.GetString()<<std::endl;
    }
}
