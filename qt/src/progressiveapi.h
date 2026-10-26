#pragma once
#include <QJsonObject>
#include <QLocalServer>
#include <QLockFile>
#include <memory>
#include <atomic>
#include <functional>
class Window;
class ProgressiveSource;
namespace foxicmp { namespace progressive {class ResourceProbeManager;} }

class ProgressiveApi : public QObject {
public:
  explicit ProgressiveApi(Window *, QObject *parent = nullptr);
  ~ProgressiveApi() override;
  bool listen(const QString &profileDirectory);
  QString errorString() const { return server.errorString(); }
  static QString endpointFor(const QString &profileDirectory);
  static QString readerRegistryFor(const QString &profileDirectory);
  static QJsonObject information(const QString &profileDirectory);
  QJsonObject request(const QJsonObject &);

private:
  struct Preparation;
  QJsonObject execute(const QJsonObject &, Preparation *);
  void dispatch(const QJsonObject &, class QLocalSocket *, class QTimer *);
  std::shared_ptr<Preparation> preparation;
  friend class ProgressiveApiTest;
  std::function<void()> preparationHook;
  void acceptClients();
  Window *window;
  QLocalServer server;
  std::unique_ptr<QLockFile> lease;
  std::weak_ptr<ProgressiveSource> source;
  QString sessionId, readerId, readerRegistry;
  QJsonValue sessionGeneration;
  QJsonObject lastOutcome;
  std::shared_ptr<std::atomic<bool>> releaseReceipt;
  int clients = 0;
  std::unique_ptr<foxicmp::progressive::ResourceProbeManager> resourceProbes;
};
