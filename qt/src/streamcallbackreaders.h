#pragma once
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

// libmpv cookies may be opaque values, not necessarily object addresses. In
// particular, cancellation runs on another thread: never dereference a cookie
// whose owner may already be executing close. Closed IDs are never reused.
template<class Reader> class StreamCallbackReaders {
    struct Registry {
        std::mutex mutex;
        uintptr_t next=1;
        std::map<uintptr_t,std::shared_ptr<Reader>> readers;
    };
    static Registry& registry(){static Registry value;return value;}
public:
    static void* insert(std::shared_ptr<Reader> reader){
        auto& r=registry();std::lock_guard<std::mutex> lock(r.mutex);
        if(r.next==std::numeric_limits<uintptr_t>::max())throw std::overflow_error("Stream callback ID exhausted");
        const auto id=r.next;r.readers.emplace(id,std::move(reader));++r.next;
        return reinterpret_cast<void*>(id);
    }
    static std::shared_ptr<Reader> get(void* cookie){
        auto& r=registry();std::lock_guard<std::mutex> lock(r.mutex);
        const auto it=r.readers.find(reinterpret_cast<uintptr_t>(cookie));
        return it==r.readers.end()?std::shared_ptr<Reader>{}:it->second;
    }
    static void close(void* cookie){
        std::shared_ptr<Reader> reader;
        {
            auto& r=registry();std::lock_guard<std::mutex> lock(r.mutex);
            const auto it=r.readers.find(reinterpret_cast<uintptr_t>(cookie));
            if(it==r.readers.end())return;
            reader=std::move(it->second);r.readers.erase(it);
        }
        // Neither reader I/O nor destruction runs with the registry locked.
        // A concurrent callback retains its own reference until it returns.
        reader->interrupt();
    }
};
