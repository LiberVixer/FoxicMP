// FoxicMP progressive local playback. GPL-3.0-or-later.
#pragma once
#include "HarvesterMP4.h"
#include <atomic>
#include <mutex>
#include <thread>

class CHarvesterSession {
    std::wstring m_manifest, m_root;
    harvester::Snapshot m_state;
    mutable std::mutex m_mutex;
    std::array<HANDLE,2> m_files{{INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE}};
    std::array<std::vector<harvester::Sample>,2> m_samples;
    std::array<harvester::FragmentIndex,2> m_fragments;
    std::array<int64_t,2> m_duration{{0,0}};
    mutable std::array<size_t,2> m_indexPosition{{0,0}};
    mutable std::array<int64_t,2> m_indexEnd{{0,0}};
    std::atomic<int64_t> m_position{0};
    std::atomic<unsigned> m_completionWaiters{0};
    std::thread m_thread;
    HANDLE m_cancel=nullptr, m_stop=nullptr;
    std::string m_error, m_status="attached";
    std::atomic<ULONGLONG> m_lastUpdate{0};
    std::atomic<bool> m_connected{false};
    HANDLE m_pipe=INVALID_HANDLE_VALUE;
    void RefreshFragments();
    const std::vector<harvester::Sample>& Samples(int track) const;
    bool ReadManifest(harvester::Snapshot& state);
    void Watch();
    bool ConnectPipe();
    bool Send(const char* type);
    void DisconnectPipe();
public:
    CHarvesterSession();
    ~CHarvesterSession();
    static std::shared_ptr<CHarvesterSession> Open(const std::wstring& path, CStringW& error);
    HANDLE File(int track) const { return m_files[track]; }
    HANDLE CancelEvent() const { return m_cancel; }
    std::wstring Path(int track) const;
    harvester::Snapshot Snapshot() const;
    std::string Error() const;
    void Fail(const char* reason);
    void Cancel();
    bool Connected() const { return m_connected && GetTickCount64()-m_lastUpdate.load()<15000; }
    HRESULT WaitRange(int track, uint64_t offset, uint64_t size, HANDLE flush, HANDLE breaker, bool completion=false);
    void BeginCompletionWait() {++m_completionWaiters;}
    void EndCompletionWait() {--m_completionWaiters;}
    bool SetIndex(int track, std::vector<harvester::Sample> samples, int64_t duration);
    int SampleAt(int track,size_t index,harvester::Sample& sample) const; // 0 waiting, 1 packet, 2 confirmed EOF
    size_t SeekSample(int track,int64_t position) const;
    int64_t ReadyEnd() const;
    bool Complete() const;
    void Notify(const char* status);
    void ReportPosition(int64_t position) {m_position=position;}
};
