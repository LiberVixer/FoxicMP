#include "player.h"
#include "stdinstream.h"
#include "glsurface.h"
#include "i18n.h"
#include "progressivesource.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>
#include <cmath>
#include <stdexcept>

static QVariant nodeValue(const mpv_node &n) {
  switch (n.format) {
  case MPV_FORMAT_STRING:
    return QString::fromUtf8(n.u.string);
  case MPV_FORMAT_FLAG:
    return bool(n.u.flag);
  case MPV_FORMAT_INT64:
    return QVariant::fromValue<qlonglong>(n.u.int64);
  case MPV_FORMAT_DOUBLE:
    return n.u.double_;
  case MPV_FORMAT_NODE_ARRAY: {
    QVariantList a;
    for (int i = 0; i < n.u.list->num; ++i)
      a.append(nodeValue(n.u.list->values[i]));
    return a;
  }
  case MPV_FORMAT_NODE_MAP: {
    QVariantMap m;
    for (int i = 0; i < n.u.list->num; ++i)
      m.insert(QString::fromUtf8(n.u.list->keys[i]),
               nodeValue(n.u.list->values[i]));
    return m;
  }
  default:
    return {};
  }
}
Player::Player(QWidget *parent, const QString &output,
               const QMap<QString, QString> &startupOptions, int interval)
    : QWidget(parent), swapInterval(interval == 0 ? 0 : 1) {
  handle = mpv_create();
  if (!handle)
    throw std::runtime_error("libmpv: mpv_create failed");
  // Do not read ~/.config/mpv or use its input bindings, scripts, or
  // watch-later directory.
  for (const auto &p :
       {std::pair{"config", "no"},
        {"vo", "libmpv"},
        {"idle", "yes"},
        {"keep-open", "no"},
        {"input-default-bindings", "no"},
        {"input-vo-keyboard", "no"},
        {"input-terminal", "no"},
        {"terminal", "no"},
        {"osc", "no"},
        {"hwdec", "auto-copy"},
        // Qt paints on its UI thread. Keep render() from waiting
        // for an early frame's target time in either backend.
        {"video-timing-offset", "0"},
        {"audio-client-name", "FoxicMP Qt"}})
    mpv_set_option_string(handle, p.first, p.second);
  for (auto option = startupOptions.cbegin(); option != startupOptions.cend();
       ++option) {
    const QByteArray name = option.key().toUtf8();
    const QByteArray value = option.value().toUtf8();
    if (mpv_set_option_string(handle, name.constData(), value.constData()) <
        0) {
      mpv_terminate_destroy(handle);
      handle = nullptr;
      throw std::runtime_error(
          QString(uiTr("Недопустимая настройка libmpv: %1"))
              .arg(option.key())
              .toStdString());
    }
  }
  mpv_set_option_string(handle, "opengl-swapinterval",
                        swapInterval ? "1" : "0");
  const int result = mpv_initialize(handle);
  if (result < 0) {
    mpv_terminate_destroy(handle);
    handle = nullptr;
    throw std::runtime_error(mpv_error_string(result));
  }
  progressive = std::make_unique<ProgressiveStreams>(handle);
  stdinStream = std::make_unique<StdinStream>(handle);
  mpv_request_log_messages(handle,
      qEnvironmentVariableIsSet("FOXICMP_MPV_LOG") ? "debug" : "warn");
  for (const auto *name :
       {"time-pos", "duration", "pause", "volume", "mute", "speed",
        "track-list", "chapter-list", "media-title", "eof-reached",
        "paused-for-cache", "idle-active", "hwdec-current", "seeking",
        "video-params", "video-out-params"})
    mpv_observe_property(handle, 0, name, MPV_FORMAT_NODE);
  mpv_set_wakeup_callback(handle, wakeup, this);
  setAttribute(Qt::WA_OpaquePaintEvent);
  auto *renderTimer = new QTimer(this);
  renderTimer->setObjectName("foxicmpRenderTimer");
  renderTimer->setTimerType(Qt::PreciseTimer);
  renderTimer->setInterval(16);
  connect(renderTimer, &QTimer::timeout, this, &Player::scheduleFrame);
  renderTimer->start();
  if (output == "opengl") {
    mpv_set_property_string(handle, "gpu-sw", "yes");
    surface = new GlSurface(this);
    surface->setGeometry(rect());
    surface->lower();
    QTimer::singleShot(2500, this, [this] {
      if (surface && !renderer)
        fallbackToSoftware(uiTr("OpenGL недоступен"));
    });
  } else
    createSoftwareRenderer();
}
void Player::createSoftwareRenderer() {
  mpv_render_param params[] = {
      {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW)},
      {MPV_RENDER_PARAM_INVALID, nullptr}};
  int result = mpv_render_context_create(&renderer, handle, params);
  if (result < 0)
    throw std::runtime_error(mpv_error_string(result));
  mpv_render_context_set_update_callback(renderer, redraw, this);
  QTimer::singleShot(0, this, &Player::rendererReady);
}
void Player::rendererReady() {
  emit initialized();
  if (!pendingLoad.isEmpty()) {
    auto args = pendingLoad;
    auto options = pendingOptions;
    pendingLoad.clear();
    pendingOptions.clear();
    if (options.isEmpty())
      command(args);
    else
      loadSource(args[1], options);
  }
}
QString Player::outputName() const { return surface ? "OpenGL" : "Software"; }
bool Player::captureFrame(quint64 id, bool subtitles) {
  if (frameRequests.contains(id))
    return false;
  frameRequests.insert(id);
  if (command({"screenshot-raw", subtitles ? "subtitles" : "video"}, id))
    return true;
  frameRequests.remove(id);
  return false;
}
static QImage captureImage(const mpv_node &node) {
  if (node.format != MPV_FORMAT_NODE_MAP || !node.u.list)
    return {};
  qint64 w = 0, h = 0, stride = 0;
  const mpv_byte_array *data = nullptr;
  QByteArray format;
  for (int i = 0; i < node.u.list->num; ++i) {
    const QByteArray key(node.u.list->keys[i]);
    const auto &value = node.u.list->values[i];
    if (value.format == MPV_FORMAT_INT64) {
      if (key == "w")
        w = value.u.int64;
      if (key == "h")
        h = value.u.int64;
      if (key == "stride")
        stride = value.u.int64;
    } else if (key == "format" && value.format == MPV_FORMAT_STRING)
      format = value.u.string;
    else if (key == "data" && value.format == MPV_FORMAT_BYTE_ARRAY)
      data = value.u.ba;
  }
  // mpv owns the result until the next event. Copy bounded positive-stride
  // bgr0 data now; unsupported layouts fail instead of aliasing freed memory.
  if (w <= 0 || h <= 0 || w > 16384 || h > 16384 || w * h > 33554432 ||
      stride < w * 4 || stride > 256 * 1024 || !data || !data->data ||
      quint64(stride * (h - 1) + w * 4) > data->size || format != "bgr0")
    return {};
  QImage image(int(w), int(h), QImage::Format_RGB32);
  if (image.isNull())
    return {};
  for (int y = 0; y < h; ++y) {
    const auto *src =
        static_cast<const unsigned char *>(data->data) + y * stride;
    auto *dst = reinterpret_cast<QRgb *>(image.scanLine(y));
    for (int x = 0; x < w; ++x)
      dst[x] = qRgb(src[x * 4 + 2], src[x * 4 + 1], src[x * 4]);
  }
  return image;
}
void Player::fallbackToSoftware(const QString &reason) {
  if (!surface)
    return;
  surface->release();
  delete surface;
  surface = nullptr;
  try {
    createSoftwareRenderer();
    emit property("render-fallback", reason);
    update();
  } catch (const std::exception &e) {
    emit error(QString::fromUtf8(e.what()));
  }
}
Player::~Player() {
  stdinStream->clear();
  progressive->clear();
  mpv_set_wakeup_callback(handle, nullptr, nullptr);
  if (surface) {
    delete surface;
    surface = nullptr;
  } else if (renderer) {
    mpv_render_context_set_update_callback(renderer, nullptr, nullptr);
    mpv_render_context_free(renderer);
  }
  mpv_terminate_destroy(handle);
}
void Player::setRenderInterval(int milliseconds) {
  const int interval = std::clamp(milliseconds, 1, 33);
  if (auto *timer = findChild<QTimer *>("foxicmpRenderTimer"))
    if (timer->interval() != interval)
      timer->setInterval(interval);
  setProperty("foxicmpRenderInterval", interval);
}
void Player::recordRender(qint64 nanoseconds) {
  ++renderCalls;
  renderTimes.enqueue(nanoseconds / 1000000.);
  if (renderTimes.size() > 120)
    renderTimes.dequeue();
}
void Player::resetRenderStatistics() {
  renderCalls = 0;
  renderTimes.clear();
  for (const auto *key : {"frame-drop-count", "decoder-frame-drop-count",
                          "mistimed-frame-count", "vo-delayed-frame-count"})
    counterBaseline[key] = get(key).toLongLong();
}
QVariantMap Player::renderStatistics() {
  double total = 0, peak = 0;
  for (const auto value : renderTimes) {
    total += value;
    peak = std::max(peak, value);
  }
  QVariantMap stats{
      {"renderer", outputName()},
      {"renderCalls", QVariant::fromValue<qulonglong>(renderCalls)},
      {"samples", renderTimes.size()},
      {"lastMs", renderTimes.isEmpty() ? 0. : renderTimes.back()},
      {"averageMs", renderTimes.isEmpty() ? 0. : total / renderTimes.size()},
      {"peakMs", peak}};
  for (const auto *key : {"frame-drop-count", "decoder-frame-drop-count",
                          "mistimed-frame-count", "vo-delayed-frame-count"}) {
    const auto current = get(key);
    if (!current.isValid()) {
      stats[key] = QVariant();
      continue;
    }
    const auto value = current.toLongLong();
    if (value < counterBaseline.value(key))
      counterBaseline[key] = 0;
    stats[key] = value - counterBaseline.value(key);
  }
  for (const auto *key :
       {"video-codec", "hwdec-current", "video-params", "video-out-params",
        "estimated-vf-fps", "estimated-display-fps", "display-sync-active",
        "video-sync", "avsync", "vsync-jitter", "vo-passes"})
    stats[key] = get(key);
  stats["glDriver"] = QObject::property("foxicmpGlDriver");
  stats["swapIntervalRequested"] = requestedSwapInterval();
  stats["swapIntervalReported"] =
      QObject::property("foxicmpSwapIntervalReported");
  return stats;
}
void Player::setSoftwarePrecision(int bits) {
  softwarePrecision = bits == 16 ? 16 : 8;
  update();
}
void Player::renderFrame() {
  const QSize pixels = (size() * devicePixelRatioF()).expandedTo(QSize(1, 1));
  const auto format =
      softwarePrecision == 16 ? QImage::Format_RGBX64 : QImage::Format_RGBX8888;
  if (frame.size() != pixels || frame.format() != format)
    frame = QImage(pixels, format);
  if (frame.isNull())
    return;
  int dimensions[]{frame.width(), frame.height()};
  size_t stride = static_cast<size_t>(frame.bytesPerLine());
  mpv_render_param params[]{
      {MPV_RENDER_PARAM_SW_SIZE, dimensions},
      {MPV_RENDER_PARAM_SW_FORMAT,
       const_cast<char *>(softwarePrecision == 16 ? "rgba64" : "rgb0")},
      {MPV_RENDER_PARAM_SW_STRIDE, &stride},
      {MPV_RENDER_PARAM_SW_POINTER, frame.bits()},
      {MPV_RENDER_PARAM_INVALID, nullptr}};
  const int result = mpv_render_context_render(renderer, params);
  if (result < 0) {
    frame.fill(Qt::black);
    emit error(QString::fromUtf8(mpv_error_string(result)));
  }
}
void Player::paintEvent(QPaintEvent *) {
  if (surface) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    return;
  }
  if (!renderer)
    return;
  QElapsedTimer timer;
  timer.start();
  renderFrame();
  {
    QPainter p(this);
    p.drawImage(rect(), frame);
  }
  mpv_render_context_report_swap(renderer);
  recordRender(timer.nsecsElapsed());
}
QImage Player::grabFramebuffer() {
  if (surface)
    return surface->grabFramebuffer();
  if (!renderer)
    return {};
  renderFrame();
  return frame;
}
void Player::wakeup(void *self) {
  auto *p = static_cast<Player *>(self);
  QMetaObject::invokeMethod(p, &Player::drainEvents, Qt::QueuedConnection);
}
void Player::redraw(void *self) {
  // Render callbacks may come from mpv threads faster than the UI can paint.
  // Coalesce them so Qt's event queue stays responsive during a paused seek.
  static_cast<Player *>(self)->redrawPending.store(true,
                                                   std::memory_order_relaxed);
}
void Player::scheduleFrame() {
  if (!renderer || !redrawPending.exchange(false, std::memory_order_relaxed))
    return;
  if (window()->isMinimized() || !isVisible()) {
    if (surface) {
      surface->consumeHiddenFrame();
      return;
    }
    int skip = 1;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_SKIP_RENDERING, &skip},
                                 {MPV_RENDER_PARAM_INVALID, nullptr}};
    mpv_render_context_render(renderer, params);
    mpv_render_context_report_swap(renderer);
  } else if (surface)
    surface->update();
  else
    update();
}
void Player::drainEvents() {
  while (auto *event = mpv_wait_event(handle, 0)) {
    if (event->event_id == MPV_EVENT_NONE)
      break;
    if (event->event_id == MPV_EVENT_LOG_MESSAGE) {
      const auto *message = static_cast<mpv_event_log_message *>(event->data);
      if (message->log_level <= MPV_LOG_LEVEL_WARN)
        emit diagnosticMessage(QString::fromUtf8(message->prefix) + ": " +
                               QString::fromUtf8(message->text).left(2048));
      if (qEnvironmentVariableIsSet("FOXICMP_MPV_LOG"))
        qInfo().noquote() << message->prefix << message->text;
      continue;
    }
    if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
      auto *p = static_cast<mpv_event_property *>(event->data);
      emit property(QString::fromUtf8(p->name),
                    p->format == MPV_FORMAT_NODE && p->data
                        ? nodeValue(*static_cast<mpv_node *>(p->data))
                        : QVariant{});
    } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
      // PS/TS byte-based seeking can land at the following keyframe even for
      // an exact seek. Give the demuxer one second of preroll and let mpv
      // decode forward to the requested frame. File-local scope keeps this
      // extra work out of indexed MP4/MKV and resets it on the next source.
      const auto format = get("file-format").toString();
      if (format == "mpeg" || format == "mpegts")
        set("file-local-options/hr-seek-demuxer-offset", 1.0);
      resetRenderStatistics();
      emit loaded();
    } else if (event->event_id == MPV_EVENT_END_FILE) {
      cancelExternalTracks();
      auto *e = static_cast<mpv_event_end_file *>(event->data);
      if (e->reason == MPV_END_FILE_REASON_ERROR)
        emit error(QString::fromUtf8(mpv_error_string(e->error)));
      emit ended(e->reason == MPV_END_FILE_REASON_EOF);
      if (e->reason == MPV_END_FILE_REASON_ERROR)
        emit fileFailed(get("path").toString());
    } else if (event->event_id == MPV_EVENT_COMMAND_REPLY) {
      if (frameRequests.remove(event->reply_userdata)) {
        const auto *reply = static_cast<mpv_event_command *>(event->data);
        emit frameCaptured(event->reply_userdata,
                           event->error >= 0 && reply
                               ? captureImage(reply->result)
                               : QImage());
      }
      // External adds have a private request namespace. A canceled older
      // generation must neither advance the new queue nor report a user error.
      if ((event->reply_userdata >> 48) == 0x4558) {
        if (event->reply_userdata == externalRequest) {
          externalRequest = 0;
          if (event->error < 0)
            emit error(QString::fromUtf8(mpv_error_string(event->error)));
          pumpExternalTracks();
        }
        continue;
      }
      emit commandFinished(event->reply_userdata, event->error);
      if (event->error < 0)
        emit error(QString::fromUtf8(mpv_error_string(event->error)));
    }
  }
}
bool Player::command(const QStringList &arguments, quint64 requestId) {
  if (!arguments.isEmpty() &&
      (arguments.first() == "stop" || arguments.first() == "loadfile"))
    cancelExternalTracks();
  if (!arguments.isEmpty() && arguments.first() == "stop") {
    pendingLoad.clear();
    pendingOptions.clear();
    stdinStream->clear();
  }
  if (!arguments.isEmpty() && arguments.first() == "loadfile" && arguments.size() > 1)
    stdinStream->replacing(arguments[1]);
  if (!renderer && !arguments.isEmpty() && arguments.first() == "loadfile") {
    pendingLoad = arguments;
    pendingOptions.clear();
    return true;
  }
  QList<QByteArray> utf8;
  for (const auto &a : arguments)
    utf8.append(a.toUtf8());
  QList<const char *> args;
  for (const auto &a : utf8)
    args.append(a.constData());
  args.append(nullptr);
  int result = mpv_command_async(handle, requestId, args.data());
  if (result < 0)
    emit error(QString::fromUtf8(mpv_error_string(result)));
  return result >= 0;
}
bool Player::addExternalTrack(const QString &type, const QString &source,
                              bool select) {
  if ((type != "audio" && type != "sub") || source.isEmpty() || !ready())
    return false;
  // A later repeat replaces the queued request and moves it to the end of
  // the user's selection order. The active add completes before any lookup.
  for (int i = externalTracks.size() - 1; i >= 0; --i)
    if (externalTracks[i].type == type && externalTracks[i].source == source)
      externalTracks.removeAt(i);
  if (externalTracks.size() >= 256)
    return false;
  externalTracks.enqueue({type, source, select});
  pumpExternalTracks();
  return true;
}
void Player::pumpExternalTracks() {
  while (!externalRequest && !externalTracks.isEmpty()) {
    const auto next = externalTracks.dequeue();
    QString existing;
    for (const auto &v : get("track-list").toList()) {
      const auto track = v.toMap();
      if (track.value("type") == next.type && track.value("external").toBool() &&
          track.value("external-filename").toString() == next.source)
        existing = track.value("id").toString();
    }
    if (!existing.isEmpty()) {
      if (next.select) set(next.type == "audio" ? "aid" : "sid", existing);
      continue;
    }
    // Completion, not a snapshot of track-list at submission time, permits the
    // next add. This also keeps the final selected track independent of I/O speed.
    externalRequest = (quint64(0x4558) << 48) | ++externalSequence;
    if (!command({next.type == "audio" ? "audio-add" : "sub-add", next.source,
                  next.select ? "select" : "auto"}, externalRequest))
      externalRequest = 0;
  }
}
void Player::cancelExternalTracks() {
  externalTracks.clear();
  if (externalRequest) mpv_abort_async_command(handle, externalRequest);
  externalRequest = 0;
}
QString Player::attachStdin(int fd, QString *error) { return stdinStream->attach(fd, error); }
void Player::progressiveSource(
    const std::shared_ptr<ProgressiveSource> &source) {
  progressive->activate(source);
}
bool Player::loadSource(const QString &url,
                        const QMap<QString, QString> &options) {
  cancelExternalTracks();
  stdinStream->replacing(url);
  if (!renderer) {
    pendingLoad = {"loadfile", url};
    pendingOptions = options;
    return true;
  }
  // Named arguments avoid loadfile's positional-argument change in mpv 0.38.
  std::vector<QByteArray> names, texts;
  std::vector<char *> keys;
  std::vector<mpv_node> values;
  for (auto it = options.cbegin(); it != options.cend(); ++it) {
    names.push_back(it.key().toUtf8());
    texts.push_back(it.value().toUtf8());
  }
  for (size_t i = 0; i < names.size(); ++i) {
    keys.push_back(names[i].data());
    mpv_node value{};
    value.format = MPV_FORMAT_STRING;
    value.u.string = texts[i].data();
    values.push_back(value);
  }
  mpv_node_list optionList{int(values.size()), values.data(), keys.data()};
  QByteArray nativeUrl = url.toUtf8();
  char name[] = "loadfile", flags[] = "replace";
  mpv_node args[4]{};
  for (int i = 0; i < 3; ++i)
    args[i].format = MPV_FORMAT_STRING;
  args[0].u.string = name;
  args[1].u.string = nativeUrl.data();
  args[2].u.string = flags;
  args[3].format = MPV_FORMAT_NODE_MAP;
  args[3].u.list = &optionList;
  char nameKey[] = "name", urlKey[] = "url", flagsKey[] = "flags",
       optionsKey[] = "options";
  char *argKeys[]{nameKey, urlKey, flagsKey, optionsKey};
  mpv_node_list arguments{4, args, argKeys};
  mpv_node root{};
  root.format = MPV_FORMAT_NODE_MAP;
  root.u.list = &arguments;
  const int result = mpv_command_node_async(handle, 0, &root);
  if (result < 0)
    emit error(QString::fromUtf8(mpv_error_string(result)));
  return result >= 0;
}
bool Player::set(const char *name, const QVariant &value) {
  QByteArray text = value.typeId() == QMetaType::Bool
                        ? QByteArray(value.toBool() ? "yes" : "no")
                        : value.toString().toUtf8();
  int result = mpv_set_property_string(handle, name, text.constData());
  if (result < 0)
    emit error(QString("%1: %2").arg(name, mpv_error_string(result)));
  return result >= 0;
}
bool Player::restartAudioDecoder(double position) {
  const int track = get("aid").toInt();
  if (track <= 0 || get("idle-active").toBool())
    return true;
  // Run both selection changes on the mpv core in one synchronous command
  // list. Restore the demux/decoder position after discarding buffered audio;
  // track re-selection alone can resume at the next decoded packet or EOF.
  QByteArray commands =
      "no-osd set aid no; no-osd set aid " + QByteArray::number(track);
  if (get("seekable").toBool() && std::isfinite(position))
    commands += "; no-osd seek " +
                QByteArray::number(std::max(0., position), 'f', 9) +
                " absolute+exact";
  const auto result = mpv_command_string(handle, commands.constData());
  if (result < 0)
    emit error(QString("audio decoder: %1").arg(mpv_error_string(result)));
  return result >= 0 && get("aid").toInt() == track;
}
bool Player::setAudioGraph(const QString &graph, const QString &format,
                           int sampleRate) {
  auto bytes = graph.toUtf8();
  char lavfi[] = "lavfi", label[] = "foxicmp-audio";
  char nameKey[] = "name", labelKey[] = "label", paramsKey[] = "params",
       graphKey[] = "graph";
  mpv_node text{};
  text.format = MPV_FORMAT_STRING;
  text.u.string = bytes.data();
  char *paramKeys[]{graphKey};
  mpv_node_list params{1, &text, paramKeys};
  mpv_node fields[3]{};
  fields[0].format = fields[1].format = MPV_FORMAT_STRING;
  fields[0].u.string = lavfi;
  fields[1].u.string = label;
  fields[2].format = MPV_FORMAT_NODE_MAP;
  fields[2].u.list = &params;
  char *keys[]{nameKey, labelKey, paramsKey};
  mpv_node_list filter{3, fields, keys};
  mpv_node item{};
  item.format = MPV_FORMAT_NODE_MAP;
  item.u.list = &filter;
  auto formatBytes = format.toUtf8();
  auto rateBytes = QByteArray::number(sampleRate);
  char formatName[] = "format", rateName[] = "srate",
       formatLabel[] = "foxicmp-audio-format";
  char *formatKeys[]{formatName, rateName};
  mpv_node formatValues[2]{};
  formatValues[0].format = formatValues[1].format = MPV_FORMAT_STRING;
  formatValues[0].u.string = formatBytes.data();
  formatValues[1].u.string = rateBytes.data();
  mpv_node_list formatParams{};
  formatParams.num = (!format.isEmpty() ? 1 : 0) + (sampleRate > 0 ? 1 : 0);
  formatParams.keys = formatKeys + (format.isEmpty() ? 1 : 0);
  formatParams.values = formatValues + (format.isEmpty() ? 1 : 0);
  mpv_node formatFields[3]{};
  formatFields[0].format = formatFields[1].format = MPV_FORMAT_STRING;
  formatFields[0].u.string = formatName;
  formatFields[1].u.string = formatLabel;
  formatFields[2].format = MPV_FORMAT_NODE_MAP;
  formatFields[2].u.list = &formatParams;
  mpv_node_list formatFilter{3, formatFields, keys};
  mpv_node formatNode{};
  formatNode.format = MPV_FORMAT_NODE_MAP;
  formatNode.u.list = &formatFilter;
  mpv_node filters[2]{};
  int count = 0;
  if (!graph.isEmpty())
    filters[count++] = item;
  if (formatParams.num)
    filters[count++] = formatNode;
  mpv_node_list list{count, filters, nullptr};
  mpv_node root{};
  root.format = MPV_FORMAT_NODE_ARRAY;
  root.u.list = &list;
  const auto result = mpv_set_property(handle, "af", MPV_FORMAT_NODE, &root);
  if (result < 0)
    emit error(QString("af: %1").arg(mpv_error_string(result)));
  return result >= 0;
}
bool Player::setList(const char *name, const QStringList &values) {
  QList<QByteArray> strings;
  QList<mpv_node> nodes;
  strings.reserve(values.size());
  nodes.reserve(values.size());
  for (const auto &value : values)
    strings.append(value.toUtf8());
  for (auto &value : strings) {
    mpv_node node{};
    node.format = MPV_FORMAT_STRING;
    node.u.string = value.data();
    nodes.append(node);
  }
  mpv_node_list list{};
  list.num = nodes.size();
  list.values = nodes.data();
  mpv_node node{};
  node.format = MPV_FORMAT_NODE_ARRAY;
  node.u.list = &list;
  const int result = mpv_set_property(handle, name, MPV_FORMAT_NODE, &node);
  if (result < 0)
    emit error(QString("%1: %2").arg(name, mpv_error_string(result)));
  return result >= 0;
}
QVariant Player::get(const char *name) const {
  mpv_node n{};
  if (mpv_get_property(handle, name, MPV_FORMAT_NODE, &n) < 0)
    return {};
  QVariant v = nodeValue(n);
  mpv_free_node_contents(&n);
  return v;
}
void Player::resizeEvent(QResizeEvent *e) {
  QWidget::resizeEvent(e);
  if (surface)
    surface->setGeometry(rect());
}
void Player::mouseDoubleClickEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton)
    emit doubleClicked();
}
