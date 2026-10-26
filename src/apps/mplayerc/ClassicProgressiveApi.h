#pragma once
#include <memory>
#include "../../../shared/progressiveopening.h"
#include <string>
#include <thread>
#include <atomic>
#include <cstdint>
class CMainFrame;
class CHarvesterSession;
namespace foxicmp { namespace progressive {class ResourceProbeManager;} }
constexpr UINT WM_CLASSIC_PROGRESSIVE_REQUEST = WM_APP + 188;
constexpr UINT_PTR TIMER_CLASSIC_PROGRESSIVE_OPEN = 0xF188;
class ClassicProgressiveApi {
public:
    struct Request : std::enable_shared_from_this<Request> {
        std::string input, output;
        std::string descriptor, preparedReply;
        std::wstring registry;
        std::shared_ptr<CHarvesterSession> candidate;
        std::atomic<bool> abandoned{false}, finished{false};
        bool requested=false, ready=false, probe=false, publication=false, readerQuery=false, readerRepeat=false;
        unsigned readerPass=0;
        uint64_t epoch=0;
        HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ~Request();
    };
    explicit ClassicProgressiveApi(CMainFrame&);
    ~ClassicProgressiveApi();
    static std::wstring ProfileDirectory();
    static std::wstring Endpoint();
    static std::wstring ReaderRegistry();
    static std::string Information();
    std::string Handle(const std::string&, Request* = nullptr);
    void PlaybackIntentChanged(){++intentEpoch;if(preparation)preparation->abandoned.store(true);if(legacyOpening)legacyOpening->abandoned.store(true);}
    bool BeginLegacy(const std::wstring&);
    void PollLegacy();
    bool IsPreparing() const{return (preparation && !preparation->abandoned.load()) || (legacyOpening && !legacyOpening->abandoned.load());}
    void Ended(const std::shared_ptr<CHarvesterSession>&, bool natural);
private:
    static void Prepare(const std::shared_ptr<Request>&);
    std::shared_ptr<Request> preparation, registryPreparation;
    std::shared_ptr<foxicmp::progressive::SourceOpening<CHarvesterSession>> legacyOpening;
    std::wstring legacyManifest;
    uint64_t intentEpoch=0;
    void Serve();
    std::string Status();
    CMainFrame& frame;
    HANDLE stop = nullptr;
    std::thread worker;
    std::weak_ptr<CHarvesterSession> source;
    std::string sessionId, lastOutcome, readerId;
    uint64_t sessionGeneration=0;
    int sessionSchema=1;
    std::shared_ptr<std::atomic<bool>> releaseReceipt;
    std::unique_ptr<foxicmp::progressive::ResourceProbeManager> resourceProbes;
};
