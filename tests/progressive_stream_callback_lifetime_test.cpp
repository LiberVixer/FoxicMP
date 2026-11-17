#include "../qt/src/streamcallbackreaders.h"
#include <atomic>
#include <iostream>
#include <thread>

struct Reader {
    std::atomic<bool> interrupted{false};
    std::atomic<unsigned>& destroyed;
    explicit Reader(std::atomic<unsigned>& count):destroyed(count){}
    void interrupt(){interrupted.store(true);}
    ~Reader(){++destroyed;}
};
int main(){
    using Readers=StreamCallbackReaders<Reader>;
    std::atomic<unsigned> destroyed{0};
    for(unsigned i=0;i<20000;++i){
        auto reader=std::make_shared<Reader>(destroyed);
        const auto cookie=Readers::insert(reader);
        auto held=Readers::get(cookie);reader.reset();
        std::atomic<bool> go{false};
        std::thread cancel([&]{while(!go.load())std::this_thread::yield();for(unsigned j=0;j<8;++j)if(auto r=Readers::get(cookie))r->interrupt();});
        go=true;Readers::close(cookie);cancel.join();
        if(!held->interrupted || Readers::get(cookie) || destroyed!=2*i)return 1;
        held.reset();if(destroyed!=2*i+1)return 2;
        const auto next=Readers::insert(std::make_shared<Reader>(destroyed));
        if(next==cookie)return 3;
        // A delayed cancel/duplicate close must not affect the next stream.
        Readers::close(cookie);
        if(auto r=Readers::get(cookie))r->interrupt();
        if(Readers::get(next)->interrupted)return 4;
        Readers::close(next);
        if(destroyed!=2*i+2)return 5;
    }
    std::cout<<"PASS: 20000 close/cancel races; late cookies cannot reach a new stream\n";
}
