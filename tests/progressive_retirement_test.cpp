// Bounded source ownership/retirement contract, without a GUI or media backend.
#include "../shared/progressiveretirement.h"
#include <atomic>
#include <iostream>
#include <vector>
#include <future>
namespace p=foxicmp::progressive;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Gate {
    std::mutex mutex;std::condition_variable changed;bool released=false;std::atomic<unsigned> entered{0},closed{0};
    void close(){++entered;changed.notify_all();std::unique_lock<std::mutex> lock(mutex);changed.wait(lock,[&]{return released;});++closed;}
    bool wait(unsigned n){std::unique_lock<std::mutex> lock(mutex);return changed.wait_for(lock,std::chrono::seconds(3),[&]{return entered.load()>=n;});}
    void release(){std::lock_guard<std::mutex> lock(mutex);released=true;changed.notify_all();}
};
struct Source {std::shared_ptr<Gate> gate;~Source(){gate->close();}};
int main(){
    std::shared_ptr<Gate> gate;
    try{
        using Pool=p::SourceRetirement;
        gate=std::make_shared<Gate>();auto source=Pool::create<Source>([&]{return new Source{gate};});auto retained=source;
        source.reset();require(gate->entered==0,"Destructor ran before last owner");
        auto start=std::chrono::steady_clock::now();retained.reset();require(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100),"Last-owner drop waited for destructor");
        require(gate->wait(1) && gate->closed==0 && Pool::occupied()==1,"Blocked destructor lost its capacity reservation");
        gate->release();require(Pool::waitEmpty(std::chrono::seconds(3)) && gate->closed==1,"Destructor did not close exactly once");
        gate=std::make_shared<Gate>();std::vector<std::shared_ptr<Source>> sources;
        for(size_t i=0;i<Pool::Capacity;++i)sources.push_back(Pool::create<Source>([&]{return new Source{gate};}));
        require(Pool::occupied()==Pool::Capacity,"Active sources not counted");
        sources.clear();require(gate->wait(unsigned(Pool::workers())),"Cleanup workers did not start");
        require(gate->entered==Pool::workers() && gate->closed==0,"Worker concurrency exceeded limit");
        bool called=false,busy=false;
        try{auto extra=Pool::create<Source>([&]{called=true;return new Source{gate};});}
        catch(const p::RetirementBusy&){busy=true;}
        require(busy && !called && Pool::occupied()==Pool::Capacity,"Capacity must fail before source construction");
        gate->release();require(Pool::waitEmpty(std::chrono::seconds(5)) && gate->closed==Pool::Capacity,"Queued sources were not drained exactly once");
        bool failed=false;try{auto bad=Pool::create<Source>([]()->Source*{throw std::runtime_error("fixture");});}catch(const std::runtime_error&){failed=true;}
        require(failed && Pool::occupied()==0,"Failed constructor retained a capacity slot");
        gate=std::make_shared<Gate>();gate->release();auto retry=Pool::create<Source>([&]{return new Source{gate};});retry.reset();require(Pool::waitEmpty(std::chrono::seconds(3)),"Pool did not recover after saturation");
        std::cout<<"{\"result\":\"PASS\",\"passed\":6,\"capacity\":"<<Pool::Capacity<<",\"workers\":"<<Pool::workers()<<"}"<<std::endl;return 0;
    }catch(const std::exception& e){if(gate)gate->release();p::SourceRetirement::waitEmpty(std::chrono::seconds(3));std::cerr<<e.what()<<std::endl;return 1;}
}
