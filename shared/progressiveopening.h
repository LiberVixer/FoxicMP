// Window-independent, single-owner preparation state. GPL-3.0-or-later.
#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <stdexcept>

namespace foxicmp { namespace progressive {
template<class Source> struct SourceOpening {
    std::atomic<bool> finished{false}, abandoned{false};
    std::shared_ptr<Source> candidate;
    std::string error;
    const std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    bool expired() const {return std::chrono::steady_clock::now()-started>=std::chrono::seconds(5);}
    template<class Builder> static std::shared_ptr<SourceOpening> launch(Builder builder) {
        auto job=std::make_shared<SourceOpening>();
        // No GUI pointers, posted raw payloads or UI-thread shutdown joins.
        // The adapter retains its occupied slot until this worker finishes.
        std::thread([job,builder=std::move(builder)]()mutable{
            try{if(!job->abandoned.load())job->candidate=builder();}
            catch(const std::exception& e){job->error=e.what();}
            catch(...){job->error="Unknown progressive preparation failure";}
            if(job->abandoned.load())job->candidate.reset();
            job->finished.store(true,std::memory_order_release);
        }).detach();
        return job;
    }
};
}} // namespace foxicmp::progressive
