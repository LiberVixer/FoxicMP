#pragma once
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <cstdint>
class CMainFrame;
class CHarvesterSession;
constexpr UINT WM_CLASSIC_PROGRESSIVE_REQUEST = WM_APP + 188;
class ClassicProgressiveApi {
public:
    struct Request {
        std::string input, output;
        HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ~Request() {if(done)CloseHandle(done);}
    };
    explicit ClassicProgressiveApi(CMainFrame&);
    ~ClassicProgressiveApi();
    static std::wstring ProfileDirectory();
    static std::wstring Endpoint();
    static std::wstring ReaderRegistry();
    static std::string Information();
    std::string Handle(const std::string&);
    void Ended(const std::shared_ptr<CHarvesterSession>&, bool natural);
private:
    void Serve();
    std::string Status();
    CMainFrame& frame;
    HANDLE stop = nullptr;
    std::thread worker;
    std::weak_ptr<CHarvesterSession> source;
    std::string sessionId, lastOutcome, readerId;
    uint64_t sessionGeneration=0;
    std::shared_ptr<std::atomic<bool>> releaseReceipt;
};
