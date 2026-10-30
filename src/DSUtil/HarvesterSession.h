// FoxicMP progressive local playback. GPL-3.0-or-later.
#pragma once
#include "HarvesterMP4.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <memory>
#include <map>
namespace foxicmp { namespace progressive {class ReaderLease;class Fmp4V2Bridge;} }

class CHarvesterSession {
    std::wstring m_manifest, m_root;
    bool m_published = false;
    std::array<std::wstring, harvester::MaxMediaTracks> m_publishedPaths;
    std::wstring m_publishedRoot;
    std::shared_ptr<std::atomic<bool>> m_released = std::make_shared<std::atomic<bool>>(false);
    std::unique_ptr<foxicmp::progressive::ReaderLease> m_readerLease;
    std::shared_ptr<foxicmp::progressive::Fmp4V2Bridge> m_v2Bridge;
    harvester::Snapshot m_state;
    mutable std::mutex m_mutex;
    std::mutex m_publishMutex;
    std::array<HANDLE,harvester::MaxMediaTracks> m_files=[] {std::array<HANDLE,harvester::MaxMediaTracks> a;a.fill(INVALID_HANDLE_VALUE);return a;}();
    std::array<std::vector<harvester::Sample>,harvester::MaxMediaTracks> m_samples;
    using Fragments=std::array<std::shared_ptr<const harvester::FragmentIndex>,harvester::MaxMediaTracks>;
    Fragments m_fragments;
    std::array<int64_t,harvester::MaxMediaTracks> m_duration{{0,0}};
    mutable std::array<size_t,harvester::MaxMediaTracks> m_indexPosition{{0,0}};
    mutable std::array<int64_t,harvester::MaxMediaTracks> m_indexEnd{{0,0}};
    std::atomic<int64_t> m_position{0};
    std::atomic<int> m_selectedAudioTrack{1};
    std::atomic<unsigned> m_completionWaiters{0};
    std::thread m_thread;
    HANDLE m_cancel=nullptr, m_stop=nullptr;
    std::string m_error, m_status="attached";
    std::map<std::wstring,std::string> m_subtitleLoadErrors;
    std::atomic<ULONGLONG> m_lastUpdate{0};
    std::atomic<bool> m_connected{false};
    HANDLE m_pipe=INVALID_HANDLE_VALUE;
    void RefreshFragments();
    Fragments BuildFragments(const harvester::Snapshot&);
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
    static std::shared_ptr<CHarvesterSession> OpenPublished(const std::string& json, CStringW& error, const std::wstring& readerRegistry = {}, std::string* reasonCode=nullptr, bool trackGroup=true);
    std::string RegisteredReaderId() const;
    std::string PlaybackDetails() const;
    bool HasV2Bridge() const {return bool(m_v2Bridge);}
    bool AllowsHevcFragments() const;
    bool AllowsAv1Fragments() const;
    bool AllowsVp9Fragments() const;
    bool AllowsOpusFragments(int track=1) const;
    size_t TrackCount() const {return Snapshot().tracks.size();}
    struct SubtitleFile {std::wstring path,label;std::string language;bool selected=false;};
    std::vector<SubtitleFile> SubtitleFiles() const;
    void SubtitleLoadFailed(const std::wstring& path);
    std::wstring TrackLabel(int index) const;
    std::string WebMCodec(int track) const;
    bool IsWebM(int track=0) const;
    bool OptionalAudioFailed(int track) const;
    HRESULT ReadV2(int track,uint64_t offset,size_t size,void* data,HANDLE flush,HANDLE breaker);
    bool Publish(const std::string& json, CStringW& error, std::string* reasonCode=nullptr);
    void PublishedHeartbeat() {m_lastUpdate=GetTickCount64(); m_connected=true;}
    std::shared_ptr<std::atomic<bool>> ReleaseReceipt() const {return m_released;}
    HANDLE File(int track) const { return m_files[track]; }
    HANDLE CancelEvent() const { return m_cancel; }
    std::wstring Path(int track) const;
    harvester::Snapshot Snapshot() const;
    std::string Error() const;
    void Fail(const char* reason);
    void Cancel();
    bool Connected() const { return (m_published && Complete()) || (m_connected && GetTickCount64()-m_lastUpdate.load()<15000); }
    HRESULT WaitRange(int track, uint64_t offset, uint64_t size, HANDLE flush, HANDLE breaker, bool completion=false);
    void BeginCompletionWait() {++m_completionWaiters;}
    void EndCompletionWait() {--m_completionWaiters;}
    bool SetIndex(int track, std::vector<harvester::Sample> samples, int64_t duration);
    int SampleAt(int track,size_t index,harvester::Sample& sample) const; // 0 waiting, 1 packet, 2 confirmed EOF
    size_t SeekSample(int track,int64_t position) const;
    void SelectAudioTrack(int track);
    int64_t ReadyEnd() const;
    bool Complete() const;
    void Notify(const char* status);
    void ReportPosition(int64_t position) {m_position=position;}
};
