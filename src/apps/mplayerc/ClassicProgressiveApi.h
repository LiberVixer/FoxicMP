#pragma once
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <cstdint>
class CMainFrame;
class CHarvesterSession;
namespace foxicmp { namespace progressive {class ResourceProbeManager;} }
constexpr UINT WM_CLASSIC_PROGRESSIVE_REQUEST = WM_APP + 188;
class ClassicProgressiveApi {
public:
    struct Request : std::enable_shared_from_this<Request> {
        std::string input, output;
        std::string descriptor, preparedReply;
        std::wstring registry;
        std::shared_ptr<CHarvesterSession> candidate;
        std::atomic<bool> abandoned{false}, finished{false};
        bool requested=false, ready=false, probe=false, publication=false;
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
    void PlaybackIntentChanged(){++intentEpoch;if(preparation)preparation->abandoned.store(true);}
    bool IsPreparing() const{return preparation && !preparation->abandoned.load();}
    void Ended(const std::shared_ptr<CHarvesterSession>&, bool natural);
private:
    static void Prepare(const std::shared_ptr<Request>&);
    std::shared_ptr<Request> preparation;
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
