#include "../../shared/familydefaults.h"
#include "i18n.h"
#include "player.h"
#include "playlistpanel.h"
#include "timeline.h"
#include "window.h"
#include "windowtitle.h"
#include "classicstatus.h"
#include <QtWidgets>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif
namespace {
QString screenKey(QScreen *screen) {
  return screen->name() + "|" + screen->manufacturer() + "|" + screen->model() +
         "|" + screen->serialNumber();
}
QScreen *chooseScreen(const QString &key, QScreen *fallback) {
  const auto screens = QGuiApplication::screens();
  for (auto *screen : screens)
    if (screenKey(screen) == key)
      return screen;
  return screens.contains(fallback) ? fallback
                                    : QGuiApplication::primaryScreen();
}
} // namespace
void Window::buildWindowActions() {
  const auto add = [&](const QString &id, const QString &text,
                       std::function<void()> fn) {
    auto *a = new QAction(text, this);
    a->setObjectName("action_" + id);
    connect(a, &QAction::triggered, this, std::move(fn));
    actions[id] = a;
    defaultKeys[id] = "";
    addAction(a);
    return a;
  };
  for (const auto &entry : QList<QPair<QString, QString>>{
           {"showControls", uiTr("Кнопки управления")},
           {"showTimeline", uiTr("Шкала времени")},
           {"showStatus", uiTr("Состояние и время")}}) {
    const auto id = entry.first, key = "ui/" + id;
    auto *a = add(id, entry.second, [this, id, key] {
      profile.settings.setValue(key, actions[id]->isChecked());
      applyPanelVisibility();
    });
    a->setCheckable(true);
    a->setChecked(profile.settings.value(key, true).toBool());
  }
  auto *group = new QActionGroup(this);
  for (const auto &entry : QList<QPair<QString, QString>>{
           {"normal", uiTr("Обычный")},
           {"compact", uiTr("Компактный")},
           {"minimal", uiTr("Минимальный — только видео")}}) {
    const auto id = "window_" + entry.first, mode = entry.first;
    auto *a = add(id, entry.second, [this, mode] {
      const bool normal = mode == "normal", minimal = mode == "minimal";
      profile.settings.setValue("ui/windowMode", mode);
      profile.settings.setValue("ui/showMenu", normal);
      profile.settings.setValue("ui/showControls", !minimal);
      profile.settings.setValue("ui/showTimeline", !minimal);
      profile.settings.setValue("ui/showStatus", normal);
      profile.settings.setValue("ui/frameless", minimal);
      profile.settings.setValue("ui/frameStyle", normal ? "system" : "thin");
      if (!normal)
        dock->hide();
      applyWindowPreferences();
    });
    group->addAction(a);
    a->setCheckable(true);
    a->setChecked(
        profile.settings.value("ui/windowMode", "normal").toString() == mode);
  }
}
void Window::buildWindowMenu(QMenu *view) {
  auto *modes = view->addMenu(uiTr("Режим интерфейса"));
  modes->setObjectName("windowModesMenu");
  for (const auto &id : {"normal", "compact", "minimal"})
    modes->addAction(actions["window_" + QString(id)]);
  auto *panels = view->addMenu(uiTr("Панели"));
  panels->setObjectName("windowPanelsMenu");
  for (const auto &id :
       {"menu", "showControls", "showTimeline", "showStatus", "playlist"})
    panels->addAction(actions[id]);
}
void Window::applyPanelVisibility(bool reveal) {
  if (!controls)
    return;
  const bool full = isFullScreen();
  controls->setVisible(
      (!full || reveal) &&
      profile.settings
          .value(full ? "ui/fullscreenControls" : "ui/showControls", true)
          .toBool());
  timeline->setVisible(
      (!full || reveal) &&
      profile.settings
          .value(full ? "ui/fullscreenTimeline" : "ui/showTimeline", true)
          .toBool());
  for (const auto &entry : QList<QPair<QString, QString>>{{"classicInformationPanel", "ui/showInformation"}, {"classicStatisticsPanel", "ui/showStatistics"}})
    if (auto *panel = findChild<QFrame *>(entry.first)) panel->setVisible(!full && profile.settings.value(entry.second, false).toBool());
  const bool status = profile.settings.value("ui/showStatus", true).toBool();
  // On a portrait HiDPI screen the normal 560-DIP toolbar may be wider
  // than the display. Keep its transport/mute buttons, with the clock and
  // volume slider restored automatically on leaving this narrow fullscreen.
  const bool narrowFull = full && screen() && screen()->geometry().width() < 560;
  statusArea->setProperty("showStatus", status && !narrowFull);
  static_cast<ClassicStatusArea *>(statusArea)->arrange();
  volume->setVisible(!narrowFull);
  menuBar()->setVisible(!full &&
                        profile.settings.value("ui/showMenu", true).toBool());
  setMinimumSize(full || controls->isHidden() ? QSize(240, 135) : QSize(560, 200));
}
void Window::applyWindowPreferences() {
  updateWindowTitle();
  applyDesktopPreferences();
  for (const auto &id : {"showControls", "showTimeline", "showStatus"})
    actions[id]->setChecked(
        profile.settings.value("ui/" + QString(id), true).toBool());
  actions["menu"]->setChecked(
      profile.settings.value("ui/showMenu", true).toBool());
  actions["frameless"]->setChecked(
      profile.settings.value("ui/frameless", false).toBool());
  actions["onTop"]->setChecked(
      profile.settings.value("ui/onTop", false).toBool());
  for (const auto &id : {"normal", "compact", "minimal"})
    actions["window_" + QString(id)]->setChecked(
        profile.settings.value("ui/windowMode", "normal").toString() == id);
  if (hideControls)
    hideControls->setInterval(std::clamp(
        profile.settings.value("ui/fullscreenHideMs", 2500).toInt(), 0, 60000));
  applyWindowMode();
  showFullscreenControls();
}
QSize Window::videoSize() const {
  auto params = player->get("video-out-params").toMap();
  if (params.isEmpty())
    params = player->get("video-params").toMap();
  int w = params.value("dw", params.value("w")).toInt(),
      h = params.value("dh", params.value("h")).toInt();
  int rotation = params.value("rotate").toInt();
  if (rotation == 90 || rotation == 270)
    std::swap(w, h);
  // video-out-params supplies decoder/filter display dimensions. The explicit
  // user aspect override is applied by the VO, after those parameters.
  const auto aspect =
      profile.settings.value("video/aspect", "no").toString().split(':');
  if (aspect.size() == 2 && aspect[1].toDouble() > 0)
    w = qRound(h * aspect[0].toDouble() / aspect[1].toDouble());
  return w > 0 && h > 0 ? QSize(w, h) : QSize();
}
void Window::resizeVideoArea(const QSize &area) {
  if (!area.isValid() || isFullScreen())
    return;
  if (layout())
    layout()->activate();
  const QSize chrome = size() - player->size();
  const double minimumScale =
      std::max({1., double(minimumWidth() - chrome.width()) / area.width(),
                double(minimumHeight() - chrome.height()) / area.height()});
  auto wanted = (area * minimumScale + chrome).expandedTo(minimumSize());
  if (screen()) {
    const QSize maximum = screen()->availableGeometry().size() - QSize(12, 36);
    if (wanted.width() > maximum.width() ||
        wanted.height() > maximum.height()) {
      const auto available = maximum - chrome;
      wanted = area.scaled(available, Qt::KeepAspectRatio) + chrome;
      wanted = wanted.expandedTo(minimumSize())
                   .boundedTo(maximum.expandedTo(minimumSize()));
    }
  }
  const bool previous = changingWindow;
  changingWindow = true;
  resize(wanted);
  changingWindow = previous;
}
void Window::windowMediaLoaded() {
  if (!beforePlaybackGeometry.isValid())
    beforePlaybackGeometry = isFullScreen() ? beforeFullscreenRect : geometry();
  applyWindowMode();
  QTimer::singleShot(0, this, [this] {
    applyWindowMode();
    autoSizeVideo();
  });
}
void Window::autoSizeVideo() {
  if (!presentationReady || stopped || isFullScreen() || isMaximized() ||
      isMinimized() || autoSizedGeneration == playbackGeneration)
    return;
  const auto video = videoSize();
  if (video.isEmpty())
    return;
  const auto scale = profile.settings.value("ui/autoScale", 0).toInt();
  autoSizedGeneration = playbackGeneration;
  if (scale > 0)
    resizeVideoArea(video * (std::clamp(scale, 25, 200) / 100.));
}
void Window::restoreClosedGeometry() {
  if (profile.settings.value("ui/restoreAfterClose", false).toBool() &&
      beforePlaybackGeometry.isValid() && !isMaximized()) {
    changingWindow = true;
    setGeometry(beforePlaybackGeometry);
    changingWindow = false;
  }
  beforePlaybackGeometry = {};
  applyWindowMode();
}
void Window::exitFullscreenAtEnd() {
  if (isFullScreen() &&
      profile.settings.value("ui/exitFullscreenAtEnd", false).toBool())
    toggleFullscreen();
  applyWindowMode();
}
void Window::setupWindowPresentation() {
  // RandR may leave an otherwise valid window outside the new work area.
  // Debounce the related signals so intermediate monitor layouts are ignored.
  auto *layoutChanged = new QTimer(this);
  layoutChanged->setSingleShot(true);
  layoutChanged->setInterval(150);
  connect(layoutChanged, &QTimer::timeout, this, &Window::keepWindowOnScreen);
  const auto watchScreen = [this, layoutChanged](QScreen *screen) {
    connect(screen, &QScreen::geometryChanged, this,
            [layoutChanged] { layoutChanged->start(); });
    connect(screen, &QScreen::availableGeometryChanged, this,
            [layoutChanged] { layoutChanged->start(); });
    connect(screen, &QScreen::logicalDotsPerInchChanged, this,
            [layoutChanged] { layoutChanged->start(); });
  };
  for (auto *screen : QGuiApplication::screens())
    watchScreen(screen);
  connect(qApp, &QGuiApplication::screenAdded, this,
          [watchScreen, layoutChanged](QScreen *screen) {
            watchScreen(screen);
            layoutChanged->start();
          });
  connect(qApp, &QGuiApplication::screenRemoved, this,
          [layoutChanged] { layoutChanged->start(); });
  applyWindowPreferences();
  const auto startup =
      profile.settings.value("ui/startSize", "remember").toString();
  if (startup != "remember") {
    showNormal();
    const QSize wanted = startup == "default" ? QSize(1060, 680) : QSize(
        std::clamp(profile.settings.value("ui/startWidth", 1060).toInt(), 240, 16384),
        std::clamp(profile.settings.value("ui/startHeight", 680).toInt(), 135, 16384));
    changingWindow = true;
    if (startup == "video")
      resizeVideoArea(wanted);
    else {
      const QSize maximum =
          screen() ? screen()->availableGeometry().size() - QSize(12, 36)
                   : wanted;
      resize(wanted.expandedTo(minimumSize())
                 .boundedTo(maximum.expandedTo(minimumSize())));
    }
    changingWindow = false;
  }
  presentationReady = true;
  if (profile.settings.value("ui/startFullscreen", false).toBool() &&
      !isFullScreen())
    toggleFullscreen();
}
void Window::keepWindowOnScreen() {
  if (!presentationReady || changingWindow ||
      QGuiApplication::platformName() != "xcb")
    return;
  if (isMaximized() || isMinimized()) {
    screenRecoveryPending = true;
    return;
  }
  screenRecoveryPending = false;
  const auto screens = QGuiApplication::screens();
  if (screens.isEmpty())
    return;
  if (isFullScreen()) {
    auto *target = screens.contains(screen()) ? screen()
                                             : QGuiApplication::primaryScreen();
    applyPanelVisibility(controls->isVisible());
    if (target && geometry() != target->geometry())
      setGeometry(target->geometry());
    return;
  }
  const QRect frame = frameGeometry();
  QScreen *target = screens.contains(screen()) ? screen()
                                              : QGuiApplication::primaryScreen();
  qint64 largest = -1;
  for (auto *candidate : screens) {
    const auto area = candidate->availableGeometry();
    if (area.contains(frame))
      return;
    const auto overlap = area.intersected(frame);
    const qint64 pixels = qint64(overlap.width()) * overlap.height();
    if (pixels > largest) {
      largest = pixels;
      if (pixels > 0) target = candidate;
    }
  }
  if (!target)
    return;
  const auto area = target->availableGeometry();
  if (area.isEmpty())
    return;
  const QMargins borders = windowHandle() ? windowHandle()->frameMargins() : QMargins();
  const QSize decoration(borders.left() + borders.right(),
                         borders.top() + borders.bottom());
  const auto wanted = size().boundedTo((area.size() - decoration).expandedTo(minimumSize()));
  changingWindow = true;
  resize(wanted);
  // A monitor narrower than the minimum UI still keeps its title bar and
  // left-hand controls reachable; do not request an impossible size.
  const QSize outer = size() + decoration;
  const QPoint topLeft(std::clamp(frame.x(), area.left(),
                                  std::max(area.left(), area.right() - outer.width() + 1)),
                       std::clamp(frame.y(), area.top(),
                                  std::max(area.top(), area.bottom() - outer.height() + 1)));
  move(topLeft);
  changingWindow = false;
}
bool Window::event(QEvent *event) {
  // Commands such as fullscreen can show a hidden window directly. Its next
  // close must start a fresh tray transition rather than retain stale state.
  if (event->type() == QEvent::Show)
    hiddenToTray = false;
  const bool handled = QMainWindow::event(event);
#ifdef Q_OS_WIN
  if ((event->type() == QEvent::WinIdChange || event->type() == QEvent::Show ||
       event->type() == QEvent::WindowStateChange ||
       event->type() == QEvent::WindowActivate ||
       event->type() == QEvent::WindowDeactivate ||
       event->type() == QEvent::ApplicationPaletteChange) && internalWinId()) {
    // Windows 11's visible 1px DWM outline is independent of the resize frame.
    // Remove only its paint; keep the native caption, shadow and resize handles.
    const COLORREF noBorder = 0xfffffffe; // DWMWA_COLOR_NONE
    DwmSetWindowAttribute(reinterpret_cast<HWND>(internalWinId()),
                          static_cast<DWMWINDOWATTRIBUTE>(34), // BORDER_COLOR
                          &noBorder, sizeof(noBorder));
  }
#endif
  if (event->type() == QEvent::WindowStateChange && screenRecoveryPending &&
      !isMinimized() && !isMaximized())
    QTimer::singleShot(150, this, &Window::keepWindowOnScreen);
  if (event->type() == QEvent::WindowDeactivate && presentationReady &&
      !changingWindow && !hiddenToTray && isFullScreen() &&
      profile.settings.value("ui/exitFullscreenOnFocusLoss", false).toBool()) {
    QTimer::singleShot(150, this, [this] {
      if (!isFullScreen() || isActiveWindow() || changingWindow ||
          hiddenToTray || QApplication::activeModalWidget() ||
          QApplication::activePopupWidget())
        return;
      auto *active = QApplication::activeWindow();
      if (active && (active == this || isAncestorOf(active)))
        return;
      toggleFullscreen();
    });
  }
  return handled;
}
void Window::resizeEvent(QResizeEvent *event) {
  QMainWindow::resizeEvent(event);
  if (!presentationReady || changingWindow || resizePending || isFullScreen() ||
      isMaximized() || isMinimized() ||
      !profile.settings.value("ui/keepVideoAspect", FoxicMPFamily::KeepWindowVideoAspect).toBool() || stopped)
    return;
  const auto video = videoSize();
  if (!video.isValid())
    return;
  resizePending = true;
  const auto delta = event->size() - event->oldSize();
  QTimer::singleShot(0, this, [this, delta] {
    resizePending = false;
    if (!isFullScreen() && !isMaximized() && !stopped &&
        profile.settings.value("ui/keepVideoAspect", FoxicMPFamily::KeepWindowVideoAspect).toBool()) {
      const QSize v = videoSize(), chrome = size() - player->size();
      if (!v.isValid())
        return;
      auto area = player->size();
      if (std::abs(delta.width()) >= std::abs(delta.height()))
        area.setHeight(qRound(double(area.width()) * v.height() / v.width()));
      else
        area.setWidth(qRound(double(area.height()) * v.width() / v.height()));
      // Keep the video ratio even when the combined control bar imposes a
      // minimum width/height on the surrounding window.
      const double factor =
          std::max({1., double(minimumWidth() - chrome.width()) / area.width(),
                    double(minimumHeight() - chrome.height()) / area.height()});
      resizeVideoArea(area * factor);
    }
  });
}
void Window::moveEvent(QMoveEvent *event) {
  QMainWindow::moveEvent(event);
  if (!presentationReady || changingWindow || movePending || isFullScreen() ||
      isMaximized() || !profile.settings.value("ui/snapEdges", false).toBool())
    return;
  movePending = true;
  QTimer::singleShot(0, this, [this] {
    movePending = false;
    snapWindow();
  });
}
void Window::snapWindow() {
  if (changingWindow || isFullScreen() || isMaximized() || !screen())
    return;
  const auto work = screen()->availableGeometry(), frame = frameGeometry();
  QPoint delta;
  constexpr int threshold = 12;
  if (std::abs(frame.left() - work.left()) <= threshold)
    delta.setX(work.left() - frame.left());
  else if (std::abs(frame.right() - work.right()) <= threshold)
    delta.setX(work.right() - frame.right());
  if (std::abs(frame.top() - work.top()) <= threshold)
    delta.setY(work.top() - frame.top());
  else if (std::abs(frame.bottom() - work.bottom()) <= threshold)
    delta.setY(work.bottom() - frame.bottom());
  if (!delta.isNull()) {
    changingWindow = true;
    move(QWidget::pos() + delta);
    changingWindow = false;
  }
}
void Window::restoreDisplayMode() {
  QString error;
  if (!displayMode.restore(&error)) {
    state->setText(uiTr("Режим экрана: ") + error);
    showOsd(uiTr("Не удалось восстановить режим монитора"));
  }
}
void Window::toggleFullscreen() {
  if (changingWindow)
    return;
  changingWindow = true;
  if (isFullScreen()) {
    restoreDisplayMode();
    showNormal();
    if (!beforeFullscreenGeometry.isEmpty())
      restoreGeometry(beforeFullscreenGeometry);
    if (wasMaximized)
      showMaximized();
    else {
      // restoreGeometry may shift a valid window to leave room for a synthetic
      // title bar (notably without a WM). Preserve its actual previous position
      // while that rectangle still fits an available monitor.
      for (auto *available : QGuiApplication::screens())
        if (available->availableGeometry().contains(beforeFullscreenRect)) {
          setGeometry(beforeFullscreenRect);
          break;
        }
    }
    dock->setVisible(restorePlaylist);
    actions["fullscreen"]->setChecked(false);
    hideControls->stop();
  } else {
    beforeFullscreenGeometry = saveGeometry();
    beforeFullscreenRect = isMaximized() ? normalGeometry() : geometry();
    wasMaximized = isMaximized();
    restorePlaylist = dock->isVisible();
    auto *target = chooseScreen(
        profile.settings.value("ui/fullscreenScreen").toString(), screen());
    const auto requestedScreen = property("foxicmpLaunchScreen").toString();
    if (!requestedScreen.isEmpty())
      for (auto *candidate : QGuiApplication::screens())
        if (candidate->name() == requestedScreen) { target = candidate; break; }
    if (profile.settings.value("ui/changeDisplayMode", false).toBool() &&
        target) {
      QString error;
      if (!displayMode.enter(
              target->name(),
              profile.settings.value("ui/displayMode").toString(),
              profile.settings.value("ui/displayRate").toString(), &error)) {
        state->setText(uiTr("Режим экрана: ") + error);
        showOsd(uiTr("Выбранный режим экрана недоступен"));
      }
    }
    if (profile.settings.value("playlist/hideFullscreen", true).toBool())
      dock->hide();
    if (target) {
      setScreen(target);
      // Wayland compositors own the window rectangle. Requesting the target
      // screen is useful when supported, while forcing target->geometry()
      // would be ignored or could produce a stale restore rectangle.
      if (!QGuiApplication::platformName().startsWith("wayland",
                                                       Qt::CaseInsensitive))
        setGeometry(target->geometry());
    }
    showFullScreen();
    actions["fullscreen"]->setChecked(true);
  }
  changingWindow = false;
  applyPanelVisibility();
  showFullscreenControls();
  if (isFullScreen() && screen() && QGuiApplication::platformName() == "xcb")
    setGeometry(screen()->geometry());
}
void Window::attachWindowPreferences(QTabWidget *tabs,
                                     QList<std::function<void()>> *apply) {
  attachTitlePreferences(tabs, apply);
  auto *windowPage = new QWidget;
  auto *windowLayout = new QVBoxLayout(windowPage);
  windowLayout->setContentsMargins(0, 0, 0, 0);
  tabs->addTab(windowPage, uiTr("Поведение окна"));
  const auto check = [&](QFormLayout *form, const QString &label,
                         const QString &key, bool fallback) {
    auto *box = new QCheckBox(label);
    box->setObjectName(key);
    box->setChecked(profile.settings.value(key, fallback).toBool());
    form->addRow(box);
    apply->append(
        [this, box, key] { profile.settings.setValue(key, box->isChecked()); });
    return box;
  };
  const auto combo = [&](QFormLayout *form, const QString &label,
                         const QString &key,
                         const QList<QPair<QString, QVariant>> &entries,
                         const QVariant &fallback) {
    auto *box = new QComboBox;
    box->setObjectName(key);
    for (const auto &e : entries)
      box->addItem(e.first, e.second);
    box->setCurrentIndex(
        std::max(0, box->findData(profile.settings.value(key, fallback))));
    form->addRow(label, box);
    apply->append([this, box, key] {
      profile.settings.setValue(key, box->currentData());
    });
    return box;
  };
  auto *startupGroup = new QGroupBox(uiTr("Размер окна при запуске плеера"));
  startupGroup->setObjectName("classicStartupSizeGroup");
  auto *startupLayout = new QVBoxLayout(startupGroup);
  windowLayout->addWidget(startupGroup);
  auto *startup = new QComboBox(startupGroup);
  startup->setObjectName("ui/startSize"); startup->hide();
  for (const auto &entry : QList<QPair<QString, QString>>{
           {uiTr("Стандартный размер окна"), "default"},
           {uiTr("Запоминать размер окна"), "remember"},
           {uiTr("Задать размер окна"), "window"},
           {uiTr("Задать размер видеообласти"), "video"}})
    startup->addItem(entry.first, entry.second);
  const int startupIndex = startup->findData(profile.settings.value("ui/startSize", "remember"));
  startup->setCurrentIndex(startupIndex < 0 ? 1 : startupIndex);
  auto *startupButtons = new QButtonGroup(startupGroup);
  for (int i = 0; i < startup->count(); ++i) {
    auto *button = new QRadioButton(startup->itemText(i));
    button->setObjectName("classicStartup_" + startup->itemData(i).toString());
    startupButtons->addButton(button, i); startupLayout->addWidget(button);
    button->setChecked(i == startup->currentIndex());
  }
  connect(startupButtons, &QButtonGroup::idClicked, startup, &QComboBox::setCurrentIndex);
  connect(startup, &QComboBox::currentIndexChanged, startupButtons, [startupButtons](int index) {
    if (auto *button = startupButtons->button(index)) button->setChecked(true);
  });
  auto *dimensions = new QWidget;
  auto *dimensionsRow = new QHBoxLayout(dimensions);
  dimensionsRow->setContentsMargins(22, 0, 0, 0);
  QSpinBox *width = nullptr, *height = nullptr;
  for (const auto &entry : QList<QPair<QString, int>>{{"ui/startWidth", 1060}, {"ui/startHeight", 680}}) {
    auto *spin = new QSpinBox; spin->setObjectName(entry.first);
    spin->setRange(entry.first.endsWith("Width") ? 240 : 135, 16384);
    spin->setValue(profile.settings.value(entry.first, entry.second).toInt());
    spin->setFixedWidth(80);
    if (width) { dimensionsRow->addWidget(new QLabel(QString::fromUtf8("×"))); height = spin; }
    else width = spin;
    dimensionsRow->addWidget(spin);
    apply->append([this, spin, key = entry.first] { profile.settings.setValue(key, spin->value()); });
  }
  auto *current = new QPushButton(uiTr("Текущий размер"));
  current->setObjectName("classicCurrentWindowSize");
  dimensionsRow->addWidget(current); dimensionsRow->addStretch();
  startupLayout->addWidget(dimensions);
  const auto updateDimensions = [startup, dimensions] {
    dimensions->setEnabled(startup->currentData() == "window" || startup->currentData() == "video");
  };
  connect(startup, &QComboBox::currentIndexChanged, dimensions, updateDimensions);
  updateDimensions();
  connect(current, &QPushButton::clicked, dimensions, [this, startup, width, height] {
    const auto currentSize = startup->currentData() == "video" ? player->size() : size();
    width->setValue(currentSize.width()); height->setValue(currentSize.height());
  });
  apply->append([this, startup] { profile.settings.setValue("ui/startSize", startup->currentData()); });

  auto *playbackGroup = new QGroupBox(uiTr("После начала воспроизведения"));
  auto *playbackLayout = new QVBoxLayout(playbackGroup);
  windowLayout->addWidget(playbackGroup);
  auto *scale = new QComboBox(playbackGroup); scale->setObjectName("ui/autoScale"); scale->hide();
  for (int value : {0, 50, 100, 200}) scale->addItem(QString::number(value) + "%", value);
  scale->setCurrentIndex(std::max(0, scale->findData(profile.settings.value("ui/autoScale", 0))));
  auto *noResize = new QRadioButton(uiTr("не менять")); noResize->setObjectName("classicAutoScaleOff");
  auto *resize = new QRadioButton(uiTr("масштабировать по видео")); resize->setObjectName("classicAutoScaleOn");
  auto *scaleButtons = new QButtonGroup(playbackGroup); scaleButtons->addButton(noResize); scaleButtons->addButton(resize);
  auto *scaleChoice = new QComboBox; scaleChoice->setObjectName("classicAutoScalePercent");
  for (int value : {50, 100, 200}) scaleChoice->addItem(QString::number(value) + "%", value);
  scaleChoice->setCurrentIndex(scale->currentData().toInt() > 0 ? scaleChoice->findData(scale->currentData()) : 1);
  auto *scaleRow = new QHBoxLayout; scaleRow->addWidget(resize); scaleRow->addWidget(scaleChoice); scaleRow->addStretch();
  playbackLayout->addWidget(noResize); playbackLayout->addLayout(scaleRow);
  const auto updateScale = [scale, scaleChoice, noResize, resize] {
    const bool enabled = scale->currentData().toInt() > 0;
    noResize->setChecked(!enabled); resize->setChecked(enabled); scaleChoice->setEnabled(enabled);
    if (enabled) scaleChoice->setCurrentIndex(scaleChoice->findData(scale->currentData()));
  };
  connect(scale, &QComboBox::currentIndexChanged, playbackGroup, updateScale);
  connect(noResize, &QRadioButton::clicked, scale, [scale] { scale->setCurrentIndex(0); });
  connect(resize, &QRadioButton::clicked, scale, [scale, scaleChoice] { scale->setCurrentIndex(scale->findData(scaleChoice->currentData())); });
  connect(scaleChoice, &QComboBox::currentIndexChanged, scale, [scale, scaleChoice, resize] {
    if (resize->isChecked()) scale->setCurrentIndex(scale->findData(scaleChoice->currentData()));
  });
  updateScale();
  apply->append([this, scale] { profile.settings.setValue("ui/autoScale", scale->currentData()); });
  auto *playbackForm = new QFormLayout;
  playbackLayout->addLayout(playbackForm);
  check(playbackForm, uiTr("Вернуть начальное положение и размер окна после закрытия файла"), "ui/restoreAfterClose", false);
  auto *basic = new QFormLayout; windowLayout->addLayout(basic);
  check(basic, uiTr("Запоминать размер и положение"), "ui/rememberGeometry", FoxicMPFamily::RememberGeometry);
  check(basic, uiTr("Соотношение сторон по видеокадру"), "ui/keepVideoAspect", FoxicMPFamily::KeepWindowVideoAspect);
  check(basic, uiTr("Прилипать к границам экрана"), "ui/snapEdges", false);
  auto *details = new QToolButton; details->setText(uiTr("Дополнительно"));
  details->setObjectName("classicWindowDetails"); details->setCheckable(true);
  details->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); details->setArrowType(Qt::RightArrow);
  windowLayout->addWidget(details, 0, Qt::AlignLeft);
  auto *advanced = new QGroupBox;
  advanced->setProperty("settingsExpandButton", QVariant::fromValue<QObject *>(details));
  auto *window = new QFormLayout(advanced); windowLayout->addWidget(advanced); advanced->hide();
  connect(details, &QToolButton::toggled, advanced, [advanced, details](bool expanded) {
    advanced->setVisible(expanded); details->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
  });
  combo(window, uiTr("Условие «поверх окон»:"), "ui/onTopWhen",
        {{uiTr("Всегда"), "always"},
         {uiTr("При воспроизведении"), "playing"},
         {uiTr("При воспроизведении видео"), "video"}},
        "always");
  auto *onTopNote = new QLabel(
      uiTr("Включается флажком «Поверх остальных окон» или Ctrl+T. При "
           "паузе условные режимы снимают закрепление."));
  onTopNote->setWordWrap(true);
  window->addRow(onTopNote);
  combo(
      window, uiTr("Рамка:"), "ui/frameStyle",
      {{uiTr("Системная"), "system"}, {uiTr("Тонкая, без заголовка"), "thin"}},
      "system");
  for (const auto &e : QList<QPair<QString, QString>>{
           {"showControls", uiTr("Показывать кнопки управления")},
           {"showTimeline", uiTr("Показывать шкалу времени")},
           {"showStatus", uiTr("Показывать состояние и время")}})
    check(window, e.second, "ui/" + e.first, true);
  auto *note = new QLabel(uiTr(
      "Без рамки: Ctrl+B; перемещение — Alt + левая кнопка мыши. Режимы "
      "интерфейса и панели доступны в меню «Вид» и по правой кнопке. Пропорции "
      "окна по видеокадру по умолчанию отключены."));
  note->setWordWrap(true);
  window->addRow(note);
  windowLayout->addStretch();
  auto *fullPage = new QWidget;
  auto *full = new QFormLayout(fullPage);
  tabs->addTab(fullPage, uiTr("Полный экран"));
  QList<QPair<QString, QVariant>> screens{{uiTr("Текущий монитор"), ""}};
  for (auto *screen : QGuiApplication::screens())
    screens.append({screen->name() + " — " +
                        QString::number(screen->size().width()) + "×" +
                        QString::number(screen->size().height()),
                    screenKey(screen)});
  const auto savedScreen =
      profile.settings.value("ui/fullscreenScreen").toString();
  bool found = savedScreen.isEmpty();
  for (const auto &s : screens)
    found |= s.second.toString() == savedScreen;
  if (!found)
    screens.append({uiTr("Отключённый монитор (временно используется текущий)"),
                    savedScreen});
  auto *monitor =
      combo(full, uiTr("Монитор:"), "ui/fullscreenScreen", screens, "");
  check(full, uiTr("Запускать на полном экране"), "ui/startFullscreen", false);
  check(full, uiTr("Выходить после окончания очереди"),
        "ui/exitFullscreenAtEnd", false);
  check(full, uiTr("Выходить при потере фокуса"),
        "ui/exitFullscreenOnFocusLoss", false);
  check(full, uiTr("Показывать кнопки управления"), "ui/fullscreenControls",
        true);
  check(full, uiTr("Показывать шкалу времени"), "ui/fullscreenTimeline", true);
  auto *delay = new QSpinBox;
  delay->setObjectName("ui/fullscreenHideMs");
  delay->setRange(0, 60000);
  delay->setSuffix(uiTr(" мс"));
  delay->setSpecialValueText(uiTr("Не скрывать автоматически"));
  delay->setValue(profile.settings.value("ui/fullscreenHideMs", 2500).toInt());
  full->addRow(uiTr("Автоскрытие панелей и курсора:"), delay);
  apply->append([this, delay] {
    profile.settings.setValue("ui/fullscreenHideMs", delay->value());
  });
  auto *change = check(
      full, uiPlatformTr("Менять разрешение / частоту на время полного экрана (X11)", "Менять разрешение / частоту на время полного экрана (Windows)"),
      "ui/changeDisplayMode", false);
  QString error;
  const auto modes = displayMode.modes(&error);
  auto *mode = new QComboBox;
  mode->setObjectName("ui/displayMode");
  const auto savedMode = profile.settings.value("ui/displayMode").toString(),
             savedRate = profile.settings.value("ui/displayRate").toString();
  const auto populate = [=] {
    mode->clear();
    auto *selected = chooseScreen(monitor->currentData().toString(), screen());
    for (const auto &m : modes)
      if (selected && m.output == selected->name())
        mode->addItem(m.name + " @ " + m.rate + uiTr(" Гц") +
                          (m.current ? uiTr(" (текущий)") : ""),
                      QStringList{m.name, m.rate});
    const bool available = mode->count() > 0;
    int index = mode->findData(QStringList{savedMode, savedRate});
    if (index < 0 && !savedMode.isEmpty() && !savedRate.isEmpty()) {
      mode->addItem(savedMode + " @ " + savedRate +
                        uiTr(" Гц (сейчас недоступен)"),
                    QStringList{savedMode, savedRate});
      index = mode->count() - 1;
    }
    if (mode->count() == 0)
      mode->addItem(uiTr("Нет доступных режимов"), QStringList{});
    mode->setCurrentIndex(index >= 0 ? index : 0);
    change->setEnabled(available);
    mode->setEnabled(change->isEnabled() && change->isChecked());
  };
  connect(monitor, &QComboBox::currentIndexChanged, fullPage, populate);
  connect(change, &QCheckBox::toggled, mode, [=](bool checked) {
    mode->setEnabled(change->isEnabled() && checked);
  });
  populate();
  full->addRow(uiTr("Режим монитора:"), mode);
  apply->append([this, mode] {
    const auto value = mode->currentData().toStringList();
    if (value.size() == 2) {
      profile.settings.setValue("ui/displayMode", value[0]);
      profile.settings.setValue("ui/displayRate", value[1]);
    }
  });
  auto *modeNote = new QLabel(
      uiTr("Исходный режим восстанавливается при выходе из полного "
           "экрана и закрытии программы. Доступны только режимы "
           "активного монитора; новые режимы не создаются. На Wayland "
           "разрешение и частота задаются в настройках рабочего стола.") +
      (error.isEmpty() ? QString() : "\n" + error));
  modeNote->setWordWrap(true);
  full->addRow(modeNote);
}
