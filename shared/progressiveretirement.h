// Bounded, process-owned progressive source retirement. GPL-3.0-or-later.
#pragma once
#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace foxicmp { namespace progressive {
struct RetirementBusy : std::runtime_error {
    RetirementBusy():std::runtime_error("Progressive source capacity is occupied by active or closing sources; retry after release"){}
};
class SourceRetirement {
public:
    static constexpr size_t Capacity=32, WorkerLimit=2;
private:
    enum class Phase {Free,Owned,Queued,Running};
    struct Slot {Phase phase=Phase::Free;void* object=nullptr;void(*destroy)(void*)=nullptr;};
    struct State {
        std::mutex mutex;std::condition_variable changed;std::array<Slot,Capacity> entries;
        std::array<Slot*,Capacity> queue{};size_t head=0,tail=0,queued=0;
        size_t used=0,workers=0;
        void run() {
            for(;;) {
                Slot* job=nullptr;
                {std::unique_lock<std::mutex> lock(mutex);
                 changed.wait(lock,[&]{return queued!=0;});
                 job=queue[head];head=(head+1)%Capacity;--queued;job->phase=Phase::Running;}
                job->destroy(job->object);
                {std::lock_guard<std::mutex> lock(mutex);*job=Slot{};--used;}
                changed.notify_all();
            }
        }
    };
    static State& state() {
        // No shutdown join on the UI thread. Workers/state are process-owned;
        // abrupt process exit is handled by OS reader-identity recovery.
        static State* value=[] {
            auto* result=new State;
            for(size_t i=0;i<WorkerLimit;++i)try{std::thread([result]{result->run();}).detach();++result->workers;}
                catch(...){if(!result->workers){delete result;throw;}break;}
            return result;
        }();
        return *value;
    }
    static Slot* reserve() {
        auto& s=state();std::lock_guard<std::mutex> lock(s.mutex);
        for(auto& slot:s.entries)if(slot.phase==Phase::Free){slot.phase=Phase::Owned;++s.used;return &slot;}
        throw RetirementBusy();
    }
    static void undo(Slot* slot) {
        auto& s=state();{std::lock_guard<std::mutex> lock(s.mutex);*slot=Slot{};--s.used;}s.changed.notify_all();
    }
    template<class T> static void enqueue(Slot* slot,T* object) noexcept {
        auto& s=state();
        {std::lock_guard<std::mutex> lock(s.mutex);slot->object=object;slot->destroy=[](void* value){delete static_cast<T*>(value);};slot->phase=Phase::Queued;s.queue[s.tail]=slot;s.tail=(s.tail+1)%Capacity;++s.queued;}
        s.changed.notify_one();
    }
public:
    template<class T,class Builder> static std::shared_ptr<T> create(Builder build) {
        static_assert(std::is_nothrow_destructible<T>::value,"Retired sources require a noexcept destructor");
        auto* slot=reserve();T* object=nullptr;
        try{object=build();if(!object)throw std::runtime_error("Source factory returned null");}
        catch(...){undo(slot);throw;}
        // shared_ptr invokes the deleter even if control-block allocation fails.
        return std::shared_ptr<T>(object,[slot](T* value)noexcept{enqueue(slot,value);});
    }
    static size_t occupied(){auto& s=state();std::lock_guard<std::mutex> lock(s.mutex);return s.used;}
    static size_t workers(){return state().workers;}
    static bool waitEmpty(std::chrono::milliseconds timeout) {
        auto& s=state();std::unique_lock<std::mutex> lock(s.mutex);return s.changed.wait_for(lock,timeout,[&]{return !s.used;});
    }
};
}} // namespace foxicmp::progressive
