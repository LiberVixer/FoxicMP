#include "progressivesource.h"
#include "../../shared/progressivereaderlease.h"
#include "../../shared/progressiveretirement.h"
#include "../../shared/progressivefmp4bridge.h"
#include "progressivefile.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>
#include <windows.h>
#include <set>
#include <vector>

namespace {
QJsonObject readJson(const QString &path, bool manifest) {
  const auto file = ProgressiveFile::open(path);
  try {
    const auto info = ProgressiveFile::inspect(file, 0);
    if (!info.size || info.size > harvester::MaxManifestBytes)
      throw std::runtime_error("Invalid legacy JSON size");
    QByteArray bytes(int(info.size), '\0');
    if (ProgressiveFile::read(file, 0, bytes.data(), size_t(bytes.size())) != bytes.size())
      throw std::runtime_error("Cannot read legacy JSON");
    rapidjson::Document checked;
    checked.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag>(bytes.constData(), size_t(bytes.size()));
    if (checked.HasParseError() || !checked.IsObject()) throw std::runtime_error("Invalid legacy JSON");
    harvester::UniqueKeys(checked);
    if (manifest) harvester::Parse(bytes.toStdString());
    auto value = QJsonDocument::fromJson(bytes).object();
    if (manifest) value["sessionDirectory"] = QFileInfo(path).absolutePath();
    ProgressiveFile::close(file); return value;
  } catch (...) { ProgressiveFile::close(file); throw; }
}
QByteArray account(HANDLE process) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return {};
  DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<BYTE> data(size); QByteArray result;
  if (size && GetTokenInformation(token, TokenUser, data.data(), size, &size)) {
    auto sid = reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid;
    result = QByteArray(reinterpret_cast<const char *>(sid), int(GetLengthSid(sid)));
  }
  CloseHandle(token); return result;
}
class ProviderIoError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
std::mutex leaseMutex;
std::set<QString> leases;
}

// Compatibility only: new integrations use ProgressiveApi. The provider records
// the actual player's PID and creation time, never an external adapter's PID.
class WindowsProgressiveLease {
public:
  explicit WindowsProgressiveLease(const QJsonObject &descriptor)
      : state(descriptor), reader(QUuid::createUuid().toString(QUuid::Id128)) {
    id = descriptor["sessionId"].toString();
    {
      std::lock_guard<std::mutex> lock(leaseMutex);
      if (!leases.insert(id).second)
        throw std::runtime_error("This process already has a legacy reader for this session");
    }
    try { attach(nullptr); } catch (...) { close(); throw; }
  }
  ~WindowsProgressiveLease() { close(); }
  void update(const QJsonObject &next) { state = next; }
  void send(const char *type) {
    QJsonObject message{{"schemaVersion", 1}, {"sessionId", id}, {"readerId", reader},
        {"pid", qint64(GetCurrentProcessId())}, {"generation", state["generation"]},
        {"revision", state["revision"]}, {"type", QString::fromLatin1(type)}};
    auto raw = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    DWORD count = 0;
    if (!transfer(true, raw.data(), DWORD(raw.size()), count, 500) || count != DWORD(raw.size()))
      throw std::runtime_error("Legacy provider write failed");
  }
  bool connected() const { return pipe != INVALID_HANDLE_VALUE; }
  bool heartbeat(const std::atomic<bool> *stop) {
    if (!connected()) {
      if (GetTickCount64() < retryAt) return false;
      retryAt = GetTickCount64() + 1000;
      try { attach(stop); } catch (const std::exception &error) {
        if (qEnvironmentVariableIsSet("FOXICMP_LEGACY_DIAGNOSTICS")) qWarning("Legacy reconnect: %s", error.what());
        if (connected()) CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE; return false;
      }
    }
    char bytes[4096]; DWORD count = 0;
    try {
      if (!transfer(false, bytes, sizeof(bytes), count, 100)) return false;
      if (!count) throw ProviderIoError("Legacy provider disconnected");
    } catch (const ProviderIoError &) {
      CloseHandle(pipe); pipe = INVALID_HANDLE_VALUE; incoming.clear(); return false;
    }
    incoming.append(bytes, int(count));
    if (incoming.size() > int(harvester::MaxManifestBytes)) throw std::runtime_error("Oversized legacy notification");
    bool received = false;
    for (int newline; (newline = incoming.indexOf('\n')) >= 0;) {
      const auto line = incoming.left(newline); incoming.remove(0, newline + 1);
      rapidjson::Document checked;
      checked.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag>(line.constData(), size_t(line.size()));
      if (checked.HasParseError() || !checked.IsObject()) throw std::runtime_error("Invalid legacy notification");
      harvester::UniqueKeys(checked);
      const auto value = QJsonDocument::fromJson(line).object();
      if (value["schemaVersion"] != QJsonValue(1) || value["sessionId"] != id ||
          value["generation"] != state["generation"] ||
          (value["type"] != QJsonValue("heartbeat") && value["type"] != QJsonValue("changed")) ||
          harvester::Number(checked, "revision") < 1)
        throw std::runtime_error("Legacy notification belongs to another session");
      received = true;
    }
    return received;
  }
private:
  HANDLE pipe = INVALID_HANDLE_VALUE;
  QJsonObject state;
  QString id, reader;
  QByteArray incoming;
  ULONGLONG retryAt = 0;
  void attach(const std::atomic<bool> *stop) {
    reader = QUuid::createUuid().toString(QUuid::Id128);
    const auto name = QStringLiteral("\\\\.\\pipe\\FoxicMP.Harvester.") + id;
    pipe = CreateFileW(reinterpret_cast<LPCWSTR>(name.utf16()), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) throw std::runtime_error("Legacy provider is unavailable");
    ULONG pid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &pid)) throw std::runtime_error("Cannot authenticate legacy provider");
    const auto own = account(GetCurrentProcess());
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    const auto peer = process ? account(process) : QByteArray();
    if (process) CloseHandle(process);
    if (own.isEmpty() || own != peer) throw std::runtime_error("Legacy provider belongs to another user");
    send("attached");
    // Original Windows provider has no wire ACK. It atomically persists this
    // file after the process lease. The random readerId defeats stale proofs.
    const auto proof = QDir(state["sessionDirectory"].toString()).filePath(
        QString("player-%1.json").arg(GetCurrentProcessId()));
    const auto deadline = GetTickCount64() + 2000;
    bool acknowledged = false;
    while (GetTickCount64() < deadline && !(stop && stop->load())) {
      try {
        const auto value = readJson(proof, false);
        acknowledged = value["type"] == QJsonValue("attached") && value["readerId"] == reader &&
            value["sessionId"] == id && value["generation"] == state["generation"] &&
            value["pid"].toInteger() == GetCurrentProcessId();
      } catch (const std::exception &) { }
      if (acknowledged) break;
      Sleep(20);
    }
    if (!acknowledged) throw std::runtime_error("Legacy provider did not persist a reader lease");
  }

  bool transfer(bool write, char *bytes, DWORD size, DWORD &count, DWORD timeout) {
    OVERLAPPED ov{}; ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) throw std::runtime_error("Cannot create legacy I/O event");
    BOOL ok = write ? WriteFile(pipe, bytes, size, &count, &ov) : ReadFile(pipe, bytes, size, &count, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
      if (WaitForSingleObject(ov.hEvent, timeout) == WAIT_OBJECT_0)
        ok = GetOverlappedResult(pipe, &ov, &count, FALSE);
      else {
        CancelIoEx(pipe, &ov); GetOverlappedResult(pipe, &ov, &count, TRUE);
        CloseHandle(ov.hEvent); return false;
      }
    }
    const auto error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(ov.hEvent);
    if (!ok) throw ProviderIoError(error == ERROR_BROKEN_PIPE ? "Legacy provider disconnected" : "Legacy provider I/O failed");
    return true;
  }
  void close() noexcept {
    if (pipe != INVALID_HANDLE_VALUE) {
      try { send("released"); } catch (...) { }
      CloseHandle(pipe); pipe = INVALID_HANDLE_VALUE;
    }
    std::lock_guard<std::mutex> lock(leaseMutex); leases.erase(id);
  }
};

std::shared_ptr<ProgressiveSource> ProgressiveSource::open(const QString &path) {
  const auto manifest = QFileInfo(path).absoluteFilePath();
  auto initial = readJson(manifest, true);
  auto lease = std::make_shared<WindowsProgressiveLease>(initial);
  auto source = foxicmp::progressive::SourceRetirement::create<ProgressiveSource>([&]{return new ProgressiveSource(initial);});
  source->manifest = manifest;
  source->windowsLease = lease;
  source->worker = std::thread([raw = source.get(), lease, initial]() mutable {
    auto last = initial;
    while (!raw->stopping.load()) {
      try {
        auto next = readJson(raw->manifest, true);
        if (next["sessionId"] != last["sessionId"] || next["generation"] != last["generation"])
          throw std::runtime_error("Legacy session identity changed; reopen playback");
        if (next["revision"].toInteger() > last["revision"].toInteger()) {
          QString error;
          if (!raw->publish(next, &error)) throw std::runtime_error(error.toStdString());
          last = next; lease->update(next);
        }
        // Unchanged JSON is never a substitute for a provider/writer heartbeat.
        if (lease->heartbeat(&raw->stopping)) raw->publishedHeartbeat();
        std::unique_lock<std::mutex> lock(raw->mutex);
        for (int i = 0; i < 2; ++i) ProgressiveFile::inspect(raw->files[i], raw->snapshot.tracks[i].available);
        if (!raw->currentStatus.complete && (!lease->connected() || std::chrono::steady_clock::now() - raw->lastPublished > std::chrono::seconds(15))) {
          raw->currentStatus.connected = false; raw->changed.notify_all();
        }
        raw->changed.wait_for(lock, std::chrono::milliseconds(100), [raw] { return raw->stopping.load(); });
      } catch (const std::exception &e) { raw->fail(QString::fromUtf8(e.what())); return; }
    }
  });
  return source;
}
ProgressiveSource::~ProgressiveSource() {
  cancel();
  if(retirementHook)retirementHook();
  if (worker.joinable()) worker.join();
  for (auto file : files) if (file != -1) ProgressiveFile::close(file);
  v2Bridge.reset();
  if (readerLease) readerLease->filesClosed();
  windowsLease.reset(); // After the last reader closed both media handles.
  released->store(true);
}
