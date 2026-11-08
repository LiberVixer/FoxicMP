// Native private cache fixture; files/paths are test diagnostics, not player API.
#include "../shared/progressivecachedremux.h"
#include <future>
#include <cstdlib>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace p=foxicmp::progressive;
static std::string read(const std::shared_ptr<p::StreamOutputStore::Reader>& reader,const rapidjson::Value& d){
    const auto offset=p::Unsigned(d["offset"],"offset"),length=p::Unsigned(d["length"],"length",65536);
    const auto wait=d.HasMember("wait")?d["wait"].GetUint():0;std::vector<char> bytes(length);const auto v=reader->read(offset,bytes.data(),bytes.size(),std::chrono::milliseconds(wait));
    rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);out.AddMember("code",p::Value(v.code.c_str(),a),a);out.AddMember("bytes",uint64_t(v.bytes),a);
    const char* digits="0123456789abcdef";std::string hex;for(size_t k=0;k<v.bytes;++k){auto b=uint8_t(bytes[k]);hex+=digits[b>>4];hex+=digits[b&15];}out.AddMember("hex",p::Value(hex.c_str(),a),a);return p::lease_detail::Encode(out);
}
int main(int argc,char** argv){
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    if(argc==1){
        // Private adversarial child. The normal cache runner has argc==3;
        // no installed application or production worker reads this variable.
        const auto* mode=std::getenv("FOXICMP_MANAGED_AAC_TEST_CHILD");if(!mode)return 2;
        std::string line;std::getline(std::cin,line);
        if(std::string(mode)=="early-write"){std::cout<<"{\"io\":\"write\",\"offset\":\"0\",\"hex\":\"00\"}"<<std::endl;std::getline(std::cin,line);return 1;}
        rapidjson::Document d;d.Parse(R"({"version":1,"trackId":2,"sampleRate":48000,"channels":1,"rawOrigin":"untrimmed-decoder-samples","timestampOrigin":"presentation-samples","rawBegin":"1024","rawEnd":"193161","timestampBegin":"0","timestampEnd":"192137","encodedEnd":"193536","endPadding":"375"})");
        const auto plan=p::StreamAudioWindow::Parse(d);const std::string fault=mode;
        if(fault=="missing-field")d.RemoveMember("rawEnd");
        const auto message=std::string("{\"io\":\"audio-window\",\"window\":")+p::lease_detail::Encode(d)+",\"sha256\":\""+(fault=="wrong-digest"?std::string(64,'0'):plan.sha256)+"\"}";
        std::cout<<message<<std::endl;std::getline(std::cin,line);
        if(fault=="duplicate-window"){std::cout<<message<<std::endl;std::getline(std::cin,line);}return 1;
    }
    if(argc!=3)return 2;const auto registry=std::filesystem::u8path(argv[1]),worker=std::filesystem::u8path(argv[2]);
    auto budget=std::make_shared<p::StreamCacheBudget>();auto owner=std::make_shared<p::StreamCacheOwner>(budget);std::map<std::string,std::shared_ptr<p::StreamOutputStore::Reader>> readers;
    std::map<std::string,std::shared_ptr<p::StreamOutputStore>> held;std::shared_ptr<p::StreamOutputStore> store;std::unique_ptr<p::CachedRemuxSession> session;std::future<std::string> pending;std::unique_ptr<p::lease_detail::Handle> heldGuard;std::string line;
    while(std::getline(std::cin,line))try{
        rapidjson::Document d;d.Parse(line.c_str());p::Require(!d.HasParseError() && d.IsObject(),"test","Invalid command");const auto action=p::Text(d["action"],"action");
        if(action=="strictMux"){
            std::ifstream file(std::filesystem::u8path(p::Text(d["path"],"path",8192)),std::ios::binary);p::Require(file.good(),"test","Mux input");std::vector<char> raw((std::istreambuf_iterator<char>(file)),{});
            harvester::muxmp4::Layout layout;layout.Update([&](uint64_t at,void* out,size_t n){if(at>raw.size() || n>raw.size()-at)return false;std::memcpy(out,raw.data()+at,n);return true;},raw.size(),true,[](unsigned,harvester::BoxData){});
        }else if(action=="projectionCheck"){
            auto source=session->decoderSource(true);
            rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);p::Value rows(rapidjson::kArrayType);
            std::vector<std::shared_ptr<p::StreamDecoderCursor>> cursors;
            bool invalidTrack=false;try{p::StreamDecoderCursor invalid(source,2);}catch(const p::Error& e){invalidTrack=e.code=="invalid_request";}
            p::Require(invalidTrack,"test","Invalid projected track accepted");
            for(unsigned kind=0;kind<2;++kind){
                auto cursor=std::make_shared<p::StreamDecoderCursor>(source,int(kind));cursors.push_back(cursor);
                uint64_t available=0;p::Sha256 hash;std::vector<char> bytes(65536);bool endpoint=false;
                for(;;){
                    auto pending=std::async(std::launch::async,[cursor,&bytes]{return cursor->read(bytes.data(),bytes.size());});
                    if(pending.wait_for(std::chrono::milliseconds(250))==std::future_status::ready){
                        const auto n=pending.get();p::Require(n>=0,"test","Projected cursor read");
                        if(!n){endpoint=true;p::Require(cursor->size()==int64_t(available),"test","Projected endpoint size");break;}
                        hash.update(bytes.data(),size_t(n));available+=uint64_t(n);continue;
                    }
                    p::Require(cursor->size()==-1,"test","Premature projected wait");cursor->interrupt();
                    p::Require(pending.wait_for(std::chrono::seconds(1))==std::future_status::ready && pending.get()==-1,"test","Projected interrupt");break;
                }
                p::Value row(rapidjson::kObjectType);row.AddMember("kind",kind,a);row.AddMember("bytes",available,a);row.AddMember("eof",endpoint,a);row.AddMember("sha256",p::Value(hash.hex().c_str(),a),a);rows.PushBack(row,a);
            }
            source->cancel();source->closeCancelledDecoderReaders();
            for(const auto& cursor:cursors){char b=0;p::Require(cursor->read(&b,1)==-1,"test","Cancelled projected reader survived");}
            out.AddMember("tracks",rows,a);out.AddMember("sourceComplete",session->status().output.complete,a);out.AddMember("decoderReadersClosed",true,a);
            std::cout<<p::lease_detail::Encode(out)<<std::endl;continue;
        }else if(action=="privateFile"){
            const auto path=std::filesystem::u8path(p::Text(d["path"],"path",8192));p::lease_detail::Directory dir(path.parent_path(),false);const auto name=path.filename();p::PlainName(name.u8string(),"filename");
#ifdef _WIN32
            p::lease_detail::Security security;p::lease_detail::Handle file(CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,&security.attributes,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));p::Require(file.value!=INVALID_HANDLE_VALUE,"test","Cannot create private file");
#else
            p::lease_detail::Handle file(openat(dir.handle.value,name.c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600));p::Require(file.value>=0,"test","Cannot create private file");
#endif
            p::lease_detail::Write(file,p::Text(d["text"],"text",16384));
        }else if(action=="recover"){
            const auto v=p::RecoverStreamCache(std::filesystem::u8path(p::Text(d["root"],"root",8192)),registry);rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);out.AddMember("ready",v.ready,a);out.AddMember("removed",uint64_t(v.removed),a);p::Value rows(rapidjson::kArrayType);
            for(const auto& row:v.entries){p::Value item(rapidjson::kObjectType);item.AddMember("cache",p::Value(row.cache.c_str(),a),a);item.AddMember("code",p::Value(row.code.c_str(),a),a);item.AddMember("bytes",row.bytes,a);rows.PushBack(item,a);}out.AddMember("entries",rows,a);std::cout<<p::lease_detail::Encode(out)<<std::endl;continue;
        }else if(action=="holdGuard"){p::lease_detail::Directory dir(registry,true);heldGuard=std::make_unique<p::lease_detail::Handle>(dir.file(".guard",true));p::Require(heldGuard->lock(),"test","Guard busy");}
        else if(action=="releaseGuard")heldGuard.reset();
        else if(action=="collect"){const auto removed=owner->collect();std::cout<<"{\"ok\":true,\"removed\":"<<removed<<",\"count\":"<<owner->count()<<",\"bytes\":"<<budget->bytes()<<"}"<<std::endl;continue;}
        else if(action=="ownerStore")store=owner->create(std::filesystem::u8path(p::Text(d["root"],"root",8192)),registry,p::Text(d["sessionId"],"sessionId"),1);
        else if(action=="ownerRetire"){std::cout<<"{\"ok\":true,\"removed\":"<<(owner->retire(store)?"true":"false")<<"}"<<std::endl;continue;}
        else if(action=="budgetStatus"){std::cout<<"{\"ok\":true,\"bytes\":"<<budget->bytes()<<"}"<<std::endl;continue;}
        else if(action=="budget"){p::Require(!store && !session && readers.empty() && owner->count()==0,"test","Budget still in use");budget=std::make_shared<p::StreamCacheBudget>(p::Unsigned(d["bytes"],"bytes"));owner=std::make_shared<p::StreamCacheOwner>(budget);}
        else if(action=="store")store=std::make_shared<p::StreamOutputStore>(std::filesystem::u8path(p::Text(d["root"],"root",8192)),registry,p::Text(d["sessionId"],"sessionId"),1,budget,d.HasMember("limit")?p::Unsigned(d["limit"],"limit"):64*1024*1024);
        else if(action=="holdStore")held[p::Text(d["name"],"name")]=store;
        else if(action=="selectStore")store=held.at(p::Text(d["name"],"name"));
        else if(action=="dropHeld")held.erase(p::Text(d["name"],"name"));
        else if(action=="repeatAppend"){for(unsigned i=0;i<d["count"].GetUint();++i)store->append(store->status().written,std::vector<char>{0});}
        else if(action=="append"){
            p::Require(bool(store),"test","No store");const auto hex=p::Text(d["hex"],"hex",131072,true);p::Require(!(hex.size()&1),"test","Invalid hex");std::vector<char> bytes;
            for(size_t k=0;k<hex.size();k+=2)bytes.push_back(char(std::stoul(hex.substr(k,2),nullptr,16)));store->append(p::Unsigned(d["offset"],"offset"),bytes);
        }else if(action=="audioWindow"){
            store->acceptAudioWindow(p::StreamAudioWindow::Parse(d["window"]));
        }else if(action=="avIndex"){
            const auto values=session?session->avSamples(d["kind"].GetUint()):store->avSamples(d["kind"].GetUint());
            auto reader=readers.at(p::Text(d["name"],"name"));rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",true,a);p::Value samples(rapidjson::kArrayType);
            for(const auto& sample:values){p::Sha256 hash;std::vector<char> bytes(65536);for(uint64_t at=0;at<sample.size;){const auto n=size_t((std::min)(sample.size-at,uint64_t(bytes.size())));const auto r=reader->read(sample.offset+at,bytes.data(),n);p::Require(r.data() && r.bytes==n,"test","Protected sample read");hash.update(bytes.data(),n);at+=n;}
                p::Value row(rapidjson::kObjectType);row.AddMember("pts",sample.start,a);row.AddMember("dts",sample.decode,a);row.AddMember("end",sample.end,a);row.AddMember("sha256",p::Value(hash.hex().c_str(),a),a);samples.PushBack(row,a);}
            out.AddMember("samples",samples,a);std::cout<<p::lease_detail::Encode(out)<<std::endl;continue;
        }else if(action=="producerClosed"){p::Require(bool(store),"test","No store");std::vector<std::string> originals;if(d.HasMember("originalReaders"))for(auto& id:d["originalReaders"].GetArray())originals.push_back(p::Text(id,"id"));store->producerClosed(originals,p::Text(d["error"],"error",64,true),d.HasMember("sha256")?p::Text(d["sha256"],"sha256",64):std::string());}
        else if(action=="start"){
            const auto profile=d.HasMember("aacWindow") && d["aacWindow"].GetBool()?p::StreamRemuxProfile::H264AacWindow:p::StreamRemuxProfile::H264Video;
            session=std::make_unique<p::CachedRemuxSession>(std::filesystem::u8path(p::Text(d["directory"],"directory",8192)),registry,std::filesystem::u8path(p::Text(d["root"],"root",8192)),p::lease_detail::Encode(d["session"]),"clip",worker,owner,d.HasMember("limit")?p::Unsigned(d["limit"],"limit"):64*1024*1024,p::StreamRemuxLimits{},profile);
        }else if(action=="decoderAdmission"){session->decoderSource();
        }else if(action=="publish")session->publish(p::lease_detail::Encode(d["session"]));
        else if(action=="heartbeat")session->heartbeat();
        else if(action=="cancel")session->cancel();
        else if(action=="reader"){
            const auto name=p::Text(d["name"],"name");p::Require(!readers.count(name),"test","Reader exists");auto reader=session?session->reader():store->reader();const auto id=reader->id();readers.emplace(name,std::move(reader));std::cout<<"{\"ok\":true,\"readerId\":\""<<id<<"\"}"<<std::endl;continue;
        }else if(action=="close"){readers.at(p::Text(d["name"],"name"))->close();}
        else if(action=="dropReader")readers.erase(p::Text(d["name"],"name"));
        else if(action=="read" || action=="beginRead"){
            auto reader=readers.at(p::Text(d["name"],"name"));if(action=="read"){std::cout<<read(reader,d)<<std::endl;continue;}
            p::Require(!pending.valid(),"test","Read already pending");auto command=p::lease_detail::Encode(d);pending=std::async(std::launch::async,[reader,command]{rapidjson::Document input;input.Parse(command.c_str());return read(reader,input);});
        }else if(action=="readResult"){
            if(pending.valid() && pending.wait_for(std::chrono::milliseconds(0))==std::future_status::ready)std::cout<<pending.get()<<std::endl;else std::cout<<"{\"ok\":true,\"pending\":true}"<<std::endl;continue;
        }else if(action=="status"){
            const auto snapshot=session?session->status():p::CachedRemuxStatus{};
            const auto s=session?snapshot.output:store->status();rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();
            out.AddMember("ok",true,a);out.AddMember("written",s.written,a);out.AddMember("visible",s.visible,a);out.AddMember("fragments",s.fragments,a);out.AddMember("samples",s.samples,a);out.AddMember("activeReaders",uint64_t(s.activeReaders),a);out.AddMember("budgetBytes",budget->bytes(),a);
            out.AddMember("videoSamples",s.videoSamples,a);out.AddMember("audioSamples",s.audioSamples,a);out.AddMember("audioWindow",p::Value(s.audioWindow.c_str(),a),a);
            out.AddMember("videoIndexedEnd100ns",s.videoIndexedEnd100ns,a);out.AddMember("audioIndexedEnd100ns",s.audioIndexedEnd100ns,a);out.AddMember("indexedEnd100ns",s.indexedEnd100ns,a);out.AddMember("audioIndexedComplete",s.audioIndexedComplete,a);
            out.AddMember("initialized",s.initialized,a);out.AddMember("complete",s.complete,a);out.AddMember("producerClosed",s.producerClosed,a);out.AddMember("originalsClosed",s.originalsClosed,a);out.AddMember("retiring",s.retiring,a);out.AddMember("removed",s.removed,a);out.AddMember("failure",p::Value(s.failure.c_str(),a),a);out.AddMember("cleanupCode",p::Value(s.cleanupCode.c_str(),a),a);
            auto path=session?session->privatePath():store->privatePath();out.AddMember("path",p::Value(path.u8string().c_str(),a),a);
            if(session){const auto& v=snapshot;out.AddMember("done",v.done,a);out.AddMember("maximumReadEnd",v.source.maximumReadEnd,a);out.AddMember("sourceFilesClosed",v.source.filesClosed,a);out.AddMember("sourceCode",p::Value(v.source.code.c_str(),a),a);out.AddMember("sourceResult",p::Value(v.source.result.c_str(),a),a);p::Value ids(rapidjson::kArrayType);for(auto& id:v.source.readerIds)ids.PushBack(p::Value(id.c_str(),a),a);out.AddMember("sourceReaderIds",ids,a);out.AddMember("workerId",v.source.workerId,a);}
            std::cout<<p::lease_detail::Encode(out)<<std::endl;continue;
        }else if(action=="retire"){const auto removed=session?session->retire():store->retire();std::cout<<"{\"ok\":true,\"removed\":"<<(removed?"true":"false")<<"}"<<std::endl;continue;}
        else if(action=="drop"){session.reset();store.reset();}
        else if(action=="readerStatus" || action=="readerForget" || action=="readers"){std::cout<<p::ReaderRequest(registry,line)<<std::endl;continue;}
        else p::Require(false,"test","Unknown command");
        std::cout<<"{\"ok\":true}"<<std::endl;
    }catch(const p::Error& e){rapidjson::Document out;out.SetObject();auto& a=out.GetAllocator();out.AddMember("ok",false,a);out.AddMember("code",p::Value(e.code.c_str(),a),a);out.AddMember("message",p::Value(e.what(),a),a);std::cout<<p::lease_detail::Encode(out)<<std::endl;}
    catch(const std::exception& e){std::cerr<<e.what()<<std::endl;std::cout<<"{\"ok\":false,\"code\":\"test-error\"}"<<std::endl;}
    session.reset();if(store){store->producerClosed({},"cancelled");store->retire();}for(auto& reader:readers)reader.second->close();readers.clear();store.reset();if(pending.valid())pending.wait();return 0;
}
