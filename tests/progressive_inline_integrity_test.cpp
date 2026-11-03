// Schema-2 inline SHA arrays must not inherit the legacy 128-item bound.
#include "../shared/progressivesession.h"
#include <iostream>
namespace p=foxicmp::progressive;
using rapidjson::Value;
std::string encode(const Value& v){rapidjson::StringBuffer b;rapidjson::Writer<rapidjson::StringBuffer>w(b);v.Accept(w);return {b.GetString(),b.GetSize()};}
int main(){
 try{
  unsigned checks=0;
  auto check=[&](bool v){if(!v)throw std::runtime_error("Inline integrity regression");++checks;};
  rapidjson::Document d;d.Parse(R"({"schemaVersion":2,"sessionId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","generation":"1","revision":"1","writer":{"instanceId":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","state":"downloading"},"timeline":{"timebase":{"numerator":1,"denominator":10000000},"live":false,"duration":"100000000","timestampOffset":"0"},"resources":[{"id":"media","name":"input.part","length":"4096","ranges":[{"offset":"0","length":"300"}],"state":"downloading","integrity":{"algorithm":"sha256","blocks":[]}}],"tracks":[{"id":"audio","kind":"audio","required":true,"codecHint":"aac-lc","containerHint":"fmp4","state":"downloading","initializationReady":true,"source":{"resourceId":"media"}}]})");
  auto& a=d.GetAllocator();auto& blocks=d["resources"][0]["integrity"]["blocks"];
  for(unsigned i=0;i<300;++i){Value block(rapidjson::kObjectType);auto offset=std::to_string(i);block.AddMember("offset",Value(offset.c_str(),a),a);block.AddMember("length","1",a);block.AddMember("sha256",Value(std::string(64,'a').c_str(),a),a);blocks.PushBack(block,a);}
  auto model=p::Parse(encode(d));check(model.resources.at("media").blocks.size()==300);
  bool legacy=false;try{harvester::UniqueKeys(d);}catch(const std::exception&){legacy=true;}check(legacy);
  rapidjson::Document request;request.SetObject();auto& ra=request.GetAllocator();request.AddMember("version",1,ra);request.AddMember("action","update",ra);request.AddMember("session",Value(d,ra),ra);p::InlineKeys(request);check(true);
  auto reject=[&](const Value& value){bool rejected=false;try{p::InlineKeys(value);}catch(const std::exception&){rejected=true;}check(rejected);};
  for(const char* key:{"metadata","reads","blocks"}){Value v(rapidjson::kArrayType);for(unsigned i=0;i<129;++i)v.PushBack(i,a);d.AddMember(Value(key,a),v,a);reject(d);d.RemoveMember(key);}
  blocks[0].AddMember("offset","0",a);reject(d);blocks[0].RemoveMember("offset");blocks[0].RemoveMember("offset");blocks[0].AddMember("offset","0",a);
  d["schemaVersion"]=1;reject(d);d["schemaVersion"]=2;
  Value impostor(rapidjson::kObjectType);impostor.AddMember("integrity",Value(d["resources"][0]["integrity"],a),a);d.AddMember("metadata",impostor,a);reject(d);d.RemoveMember("metadata");
  auto saved=encode(blocks);while(blocks.Size()<=p::MaxIntegrityBlocks)blocks.PushBack(Value(blocks[0],a),a);reject(d);
  bool oversized=false;try{p::Parse(encode(d));}catch(const p::Error&){oversized=true;}check(oversized);
  rapidjson::Document restored;restored.Parse(saved.c_str());blocks.CopyFrom(restored,a);
  blocks[0]["sha256"].SetString("bad",a);bool digest=false;try{p::Parse(encode(d));}catch(const p::Error&){digest=true;}check(digest);
  rapidjson::Document nesting;nesting.SetArray();for(unsigned i=0;i<34;++i){rapidjson::Document parent;parent.SetArray();parent.PushBack(Value(nesting,parent.GetAllocator()),parent.GetAllocator());nesting.Swap(parent);}reject(nesting);
  std::cout<<"{\"result\":\"PASS\",\"checks\":"<<checks<<",\"shaBlocks\":300,\"legacyArrayLimit\":128,\"inlineByteLimit\":65536}"<<std::endl;return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}
}
