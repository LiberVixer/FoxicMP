#pragma once
#include <QImage>
#include <QMap>
#include <QQueue>
#include <QSet>
#include <QVariant>
#include <QWidget>
#include <atomic>
#include <memory>
#include <mpv/client.h>
#include <mpv/render.h>

// Owns libmpv and the selected Render API surface. All public operations run on
// the GUI thread.
class GlSurface;
class ProgressiveSource;
class StdinStream;
class ProgressiveStreams;
class Player : public QWidget {
  Q_OBJECT
public:
  explicit Player(QWidget *parent = nullptr, const QString &output = "software",
                  const QMap<QString, QString> &startupOptions = {},
                  int swapInterval = 1);
  QString outputName() const;
  ~Player() override;
  bool command(const QStringList &arguments, quint64 requestId = 0);
  bool set(const char *name, const QVariant &value);
  // Serializes asynchronous external additions and reuses tracks after completion.
  bool addExternalTrack(const QString &type, const QString &source, bool select);
  bool restartAudioDecoder(double position);
  // Submit native graph options; decoder/filter initialization may be deferred.
  bool setAudioGraph(const QString &graph, const QString &format = {},
                     int sampleRate = 0);
  bool setList(const char *name, const QStringList &values);
  QVariant get(const char *name) const;
  bool ready() const { return renderer != nullptr; }
  QImage grabFramebuffer();
  bool captureFrame(quint64 requestId, bool subtitles);
  void setSoftwarePrecision(int bits);
  QVariantMap renderStatistics();
  void resetRenderStatistics();
  void recordRender(qint64 nanoseconds);
  int requestedSwapInterval() const { return swapInterval; }
  void setRenderInterval(int milliseconds);
  bool loadSource(const QString &url, const QMap<QString, QString> &options);
  QString attachStdin(int descriptor, QString *error = nullptr);
  void progressiveSource(const std::shared_ptr<ProgressiveSource> &source);
signals:
  void property(const QString &name, const QVariant &value);
  void loaded();
  void ended(bool natural);
  void fileFailed(const QString &path);
  void error(const QString &message);
  void diagnosticMessage(const QString &message);
  void initialized();
  void doubleClicked();
  void commandFinished(quint64 requestId, int status);
  void frameCaptured(quint64 requestId, const QImage &image);

protected:
  void paintEvent(QPaintEvent *) override;
  void resizeEvent(QResizeEvent *) override;
  void mouseDoubleClickEvent(QMouseEvent *) override;

private:
  struct ExternalTrack {
    QString type, source;
    bool select;
  };
  QQueue<ExternalTrack> externalTracks;
  quint64 externalRequest = 0, externalSequence = 0;
  void pumpExternalTracks();
  void cancelExternalTracks();
  friend class GlSurface;
  friend class AudioMirror;
  void rendererReady();
  void createSoftwareRenderer();
  void fallbackToSoftware(const QString &reason);
  GlSurface *surface = nullptr;
  QStringList pendingLoad;
  QMap<QString, QString> pendingOptions;
  std::atomic_bool redrawPending{false};
  void scheduleFrame();
  void drainEvents();
  void renderFrame();
  QImage frame;
  QSet<quint64> frameRequests;
  int softwarePrecision = 8, swapInterval = 1;
  quint64 renderCalls = 0;
  QQueue<double> renderTimes;
  QMap<QString, qlonglong> counterBaseline;
  static void wakeup(void *self);
  static void redraw(void *self);
  mpv_handle *handle = nullptr;
  mpv_render_context *renderer = nullptr;
  std::unique_ptr<ProgressiveStreams> progressive;
  std::unique_ptr<StdinStream> stdinStream;
};
