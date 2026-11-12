// Real remux EOF followed by a heartbeat while durable source receipt is busy.
#include "../shared/progressivestreamremux.h"
#include <iostream>
namespace p=foxicmp::progressive;
using namespace std::chrono_literals;
static std::string text(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv)try{
    check(argc==3,"Expected fixture root and normal worker");
    const auto root=std::filesystem::u8path(argv[1]),registry=root/"registry";
    std::atomic<uint64_t> output{0};
    p::StreamRemuxSession session(root,registry,text(root/"initial.json"),"clip",std::filesystem::u8path(argv[2]),
        [&](uint64_t at,const std::vector<char>& bytes){check(at==output.load(),"Output offset changed");output+=bytes.size();},
        {},p::StreamRemuxProfile::H264AacWindow,[](const p::StreamAudioWindow&){});
    auto wait=[&](auto ready){const auto end=std::chrono::steady_clock::now()+10s;for(;;){const auto s=session.status();if(ready(s))return s;check(std::chrono::steady_clock::now()<end,"Source close EOF deadline");std::this_thread::sleep_for(5ms);}};
    const auto initial=wait([](const auto& s){return s.code=="waiting" && !s.readerIds.empty();});
    p::lease_detail::Directory receipts(registry,false);
    auto guard=std::make_unique<p::lease_detail::Handle>(receipts.file(".guard",true));check(guard->lock(),"Cannot hold private registry");
    const auto raw=text(root/"reference.mp4");
    const auto prefix=std::filesystem::file_size(root/"input.part");check(prefix<raw.size(),"Expected incomplete fixture");
    {std::ofstream file(root/"input.part",std::ios::binary|std::ios::app);file.write(raw.data()+prefix,raw.size()-prefix);check(file.good(),"Fixture append failed");}
    session.publish(text(root/"complete.json"));
    const auto pending=wait([](const auto& s){return s.code=="data";});
    check(!pending.done && !pending.filesClosed && pending.sourceComplete && pending.reason=="source-receipt-pending","EOF was acknowledged before receipt");
    const auto began=std::chrono::steady_clock::now();
    check(!session.heartbeatWhileRunning(),"Closed producer renewed heartbeat");
    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
    check(seconds<.5,"Heartbeat blocked on durable receipt");
    // Close the guard handle only; the native original lease remains held.
    guard.reset();
    const auto final=wait([](const auto& s){return s.done;});
    check(final.filesClosed && final.code=="data" && !final.result.empty() && output>0,"Late heartbeat changed successful EOF");
    for(const auto& id:initial.readerIds){
        rapidjson::Document receipt;receipt.Parse(p::ReaderRequest(registry,"{\"version\":1,\"action\":\"readerStatus\",\"readerId\":\""+id+"\"}").c_str());
        check(receipt["ok"].GetBool() && receipt["reader"]["released"].GetBool() && receipt["reader"]["filesClosed"].GetBool(),"Missing durable source close receipt");
    }
    std::cout<<"{\"result\":\"PASS\",\"pendingDone\":false,\"pendingFilesClosed\":false,\"heartbeatRenewed\":false,\"heartbeatSeconds\":"<<seconds<<",\"finalCode\":\"data\",\"finalFilesClosed\":true,\"bytesWritten\":"<<output.load()<<"}"<<std::endl;
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
