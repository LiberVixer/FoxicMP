#include "../../shared/familydefaults.h"
#include "devicesource.h"
#include "classicpreferences.h"
#include "classicstatus.h"
#include "framepreview.h"
#include "i18n.h"
#include "mediaexports.h"
#include "mediaoptions.h"
#include "osdlabel.h"
#include "player.h"
#include "timeline.h"
#include "window.h"
#include <QtWidgets>
#include <cmath>

void Window::setupPlaybackTools() {
  osd = new OsdLabel(player);
  osd->setObjectName("osd");
  osd->hide();
  osdInfo = new OsdLabel(player);
  osdInfo->setObjectName("osdInfo");
  osdInfo->hide();
  osdClock = new OsdLabel(player);
  osdClock->setObjectName("osdClock");
  osdClock->hide();
  auto *overlayTimer = new QTimer(this);
  overlayTimer->setInterval(1000);
  connect(overlayTimer, &QTimer::timeout, this, &Window::updateOverlays);
  overlayTimer->start();
  osdTimer = new QTimer(this);
  osdTimer->setSingleShot(true);
  osdTimer->setInterval(1600);
  connect(osdTimer, &QTimer::timeout, osd, &QWidget::hide);
  preview = new FramePreview(this);
  previewPopup = new QFrame(this);
  previewPopup->setObjectName("framePreview");
  previewPopup->setAttribute(Qt::WA_TransparentForMouseEvents);
  previewPopup->hide();
  previewPopup->setStyleSheet(
      "QFrame {background:#202428;border:1px solid #69727a;} QLabel "
      "{border:0;background:transparent;}");
  auto *layout = new QVBoxLayout(previewPopup);
  layout->setContentsMargins(6, 6, 6, 6);
  previewImage = new QLabel;
  previewImage->setAlignment(Qt::AlignCenter);
  previewImage->setMinimumSize(160, 90);
  layout->addWidget(previewImage);
  previewText = new QLabel;
  previewText->setAlignment(Qt::AlignCenter);
  layout->addWidget(previewText);
  connect(timeline, &Timeline::hovered, this, &Window::requestPreview);
  connect(timeline, &Timeline::chapterHovered, this, [this](const QString &chapter) {
    hoverStatus = chapter;
    updatePlayerStatus();
  });
  connect(timeline, &Timeline::hoverEnded, this, [this] {
    hoverStatus.clear();
    updatePlayerStatus();
    preview->cancel();
    previewPopup->hide();
  });
  connect(preview, &FramePreview::ready, this,
          [this](const QImage &image, double) {
            previewImage->setPixmap(QPixmap::fromImage(image));
            positionPreview();
          });
  connect(preview, &FramePreview::unavailable, this,
          [this](const QString &text) {
            previewImage->setText(text);
            positionPreview();
          });
  connect(player, &Player::commandFinished, this,
          [this](quint64 id, int status) {
            if (!screenshots.contains(id))
              return;
            const auto path = screenshots.take(id);
            showOsd(status >= 0
                        ? uiTr("Кадр сохранён: ") + QFileInfo(path).fileName()
                        : uiTr("Не удалось сохранить кадр"));
            emit frameSaved(path, status >= 0);
          });
  connect(player, &Player::frameCaptured, this,
          [this](quint64 id, const QImage &image) {
            if (!imageRequests.contains(id))
              return;
            const auto request = imageRequests.take(id);
            finishCapture(image, request.first, request.second);
          });
  applyMediaOptions();
  preview->setFilters(previewVideoFilters());
  timeline->previewEnabled =
      profile.settings.value("ui/previews", true).toBool();
  applyAppearance();
  applyWindowPreferences();
  refreshPlaybackControls();
}
void Window::positionPreview() {
  previewPopup->adjustSize();
  const QPoint anchor = mapFromGlobal(previewAnchor);
  const int x = std::clamp(anchor.x() - previewPopup->width() / 2, 0,
                           std::max(0, width() - previewPopup->width()));
  const int y = std::max(0, timeline->mapTo(this, QPoint()).y() -
                                previewPopup->height() - 6);
  previewPopup->move(x, y);
}
void Window::showOsd(const QString &text) {
  if (!osd || !profile.settings.value("ui/osd", FoxicMPFamily::Osd).toBool() || !isVisible())
    return;
  osd->setText(text);
  osd->setMaximumWidth(std::max(80, player->width() - 24));
  positionOverlays();
  osd->raise();
  osd->show();
  osdTimer->start();
}
void Window::updateClock() {
  updateWindowTitle();
  updatePlayerStatus();
  if (stopped) {
    time->clear();
    gpuIndicator->setProperty("decoder", QString());
    static_cast<ClassicStatusArea *>(statusArea)->arrange();
    return;
  }
  const bool remaining = duration > 0 && profile.settings.value("ui/remainingTime", false).toBool();
  const bool milliseconds = profile.settings.value("ui/showMilliseconds", false).toBool();
  const auto format = [milliseconds](double seconds, bool forceHours) {
    seconds = std::max(0., seconds);
    auto value = clockText(seconds);
    if (!forceHours && value.startsWith("00:")) value.remove(0, 3);
    if (milliseconds) value += QString(".%1").arg(qint64(std::floor(seconds * 1000)) % 1000, 3, 10, QChar('0'));
    return value;
  };
  const bool hours = duration >= 3600 && profile.settings.value("ui/showZeroHours", false).toBool();
  auto text = (remaining ? QString("- ") : QString()) + format(remaining ? duration - pos : pos, hours);
  if (duration > 0 && pos < duration) text += " / " + format(duration, false);
  time->setText(text);
  static_cast<ClassicStatusArea *>(statusArea)->arrange();
}
void Window::applyWindowMode() {
  if (changingWindow)
    return;
  applyPanelVisibility();
  const bool visible = isVisible(), maximized = isMaximized(),
             minimized = isMinimized(), full = isFullScreen();
  const auto previousState = windowState();
  const auto condition =
      profile.settings.value("ui/onTopWhen", "always").toString();
  bool video = false;
  for (const auto &track : player->get("track-list").toList()) {
    const auto t = track.toMap();
    video |=
        t.value("type").toString() == "video" && !t.value("albumart").toBool();
  }
  const bool playing = !stopped && !player->get("pause").toBool();
  const bool top =
      profile.settings.value("ui/onTop", false).toBool() &&
      (condition == "always" || (playing && (condition == "playing" ||
                                             (condition == "video" && video))));
  const bool frameless = profile.settings.value("ui/frameless", false).toBool();
  const bool thin =
      !frameless &&
      profile.settings.value("ui/frameStyle", "system").toString() == "thin";
  auto flags = windowFlags();
  flags.setFlag(Qt::WindowStaysOnTopHint, top);
  flags.setFlag(Qt::FramelessWindowHint, frameless);
  flags.setFlag(Qt::CustomizeWindowHint, thin);
  flags.setFlag(Qt::WindowTitleHint, !thin && !frameless);
  flags.setFlag(Qt::WindowSystemMenuHint, !thin && !frameless);
  flags.setFlag(Qt::WindowMinMaxButtonsHint, !thin && !frameless);
  flags.setFlag(Qt::WindowCloseButtonHint, !thin && !frameless);
  if (flags == windowFlags())
    return;
  const QRect restore = full || maximized ? normalGeometry() : geometry();
  changingWindow = true;
  setWindowFlags(flags);
  if (restore.isValid())
    setGeometry(restore);
  if (visible) {
    if (minimized) {
      setWindowState(previousState);
      showMinimized();
    } else if (full)
      showFullScreen();
    else if (maximized)
      showMaximized();
    else
      show();
  }
  changingWindow = false;
}
bool Window::saveFrame(const QString &path, bool includeSubtitles) {
  if (stopped || player->get("video-params").toMap().isEmpty()) {
    showOsd(uiTr("Нет видеокадра для сохранения"));
    return false;
  }
  const auto file = QFileInfo(path).absoluteFilePath();
  const quint64 id = ++screenshotId;
  screenshots.insert(id, file);
  if (!player->command({"screenshot-to-file", file,
                        includeSubtitles ? "subtitles" : "video"},
                       id)) {
    screenshots.remove(id);
    return false;
  }
  return true;
}
void Window::snapshotDialog() {
  if (stopped)
    return;
  const auto dir = profile.settings
                       .value("files/screenshotDirectory",
                              profile.directory + "/Screenshots")
                       .toString();
  QDir().mkpath(dir);
  QString base = DeviceSource::isDevice(current)
                     ? DeviceSource::label(current)
                     : QFileInfo(current).completeBaseName();
  base.replace(QRegularExpression("[/\\\\:*?\"<>|\\x00-\\x1f]"), "_");
  const auto wanted =
      profile.settings.value("capture/format", "png").toString();
  const auto suffix = MediaExports::formats().contains(wanted) ? wanted : "png";
  const auto name =
      base + "-" +
      QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + "." +
      suffix;
  QString selected = suffix == "jpg"    ? "JPEG (*.jpg *.jpeg)"
                     : suffix == "bmp"  ? "BMP (*.bmp)"
                     : suffix == "webp" ? "WebP (*.webp)"
                                        : "PNG (*.png)";
  auto path = QFileDialog::getSaveFileName(
      this, uiTr("Сохранить кадр"), QDir(dir).filePath(name),
      MediaExports::fileFilters(), &selected);
  if (path.isEmpty())
    return;
  if (QFileInfo(path).suffix().isEmpty())
    path += selected.startsWith("JPEG")   ? ".jpg"
            : selected.startsWith("BMP")  ? ".bmp"
            : selected.startsWith("WebP") ? ".webp"
                                          : ".png";
  profile.settings.setValue("files/screenshotDirectory",
                            QFileInfo(path).absolutePath());
  captureImage(path);
}
void Window::attachMediaPreferences(QTabWidget *tabs,
                                    QList<std::function<void()>> *apply) {
  auto page = [&](const QString &title) {
    auto *widget = new QWidget;
    auto *form = new QFormLayout(widget);
    tabs->addTab(widget, title);
    return form;
  };
  auto boolean = [&](QFormLayout *form, const QString &label,
                     const QString &key, bool def) {
    auto *c = new QCheckBox(label);
    c->setChecked(profile.settings.value(key, def).toBool());
    c->setObjectName(key);
    form->addRow(c);
    apply->append(
        [this, c, key] { profile.settings.setValue(key, c->isChecked()); });
    return c;
  };
  auto number = [&](QFormLayout *form, const QString &label, const QString &key,
                    int def, int min, int max) {
    auto *n = new QSpinBox;
    n->setRange(min, max);
    n->setValue(profile.settings.value(key, def).toInt());
    n->setObjectName(key);
    form->addRow(label, n);
    apply->append(
        [this, n, key] { profile.settings.setValue(key, n->value()); });
    return n;
  };
  auto color = [&](QFormLayout *form, const QString &label, const QString &key,
                   const QString &def) {
    auto *button = new QPushButton;
    QColor value(profile.settings.value(key, def).toString());
    if (!value.isValid())
      value = QColor(def);
    button->setProperty("color", value);
    button->setText(value.name(QColor::HexArgb));
    button->setObjectName(key);
    connect(button, &QPushButton::clicked, tabs, [button, tabs] {
      const auto value = QColorDialog::getColor(
          button->property("color").value<QColor>(), tabs,
          uiTr("Цвет субтитров"), QColorDialog::ShowAlphaChannel);
      if (value.isValid()) {
        button->setProperty("color", value);
        button->setText(value.name(QColor::HexArgb));
      }
    });
    form->addRow(label, button);
    apply->append([this, button, key] {
      profile.settings.setValue(
          key, button->property("color").value<QColor>().name(QColor::HexArgb));
    });
  };
  auto *subs = page(uiTr("Субтитры"));
  boolean(subs, uiTr("Автоматически искать рядом с фильмом"),
          "subtitles/autoLoad", true);
  boolean(subs, uiTr("Показывать субтитры"), "subtitles/visible", true);
  auto *font = new QFontComboBox;
  font->setCurrentFont(QFont(
      profile.settings.value("subtitles/font", "DejaVu Sans").toString()));
  font->setObjectName("subtitles/font");
  subs->addRow(uiTr("Шрифт"), font);
  apply->append([this, font] {
    profile.settings.setValue("subtitles/font", font->currentFont().family());
  });
  number(subs, uiTr("Размер"), "subtitles/size", 38, 12, 100);
  color(subs, uiTr("Цвет текста"), "subtitles/color", "#ffffff");
  color(subs, uiTr("Цвет обводки"), "subtitles/outlineColor", "#000000");
  number(subs, uiTr("Толщина обводки"), "subtitles/outlineSize", 2, 0, 8);
  number(subs, uiTr("Положение (0 — верх, 100 — низ)"), "subtitles/position",
         100, 0, 100);
  auto *override = boolean(subs, uiTr("Применять мой стиль также к ASS/SSA"),
                           "subtitles/overrideAss", false);
  override->setToolTip(
      uiTr("По умолчанию сохраняются авторские стили ASS/SSA"));
  if (!ClassicPreferences::loadingStep(qobject_cast<QDialog *>(tabs->window()))) return;
  auto *audio = page(uiTr("Звук"));
  auto *device = new QComboBox;
  device->setObjectName("audio/device");
  device->addItem(uiTr("Автоматически"), "auto");
  for (const auto &value : player->get("audio-device-list").toList()) {
    const auto item = value.toMap();
    const auto name = item.value("name").toString();
    if (name == "auto" || name.startsWith("pulse/foxicmp_dual_"))
      continue;
    device->addItem(item.value("description").toString(), name);
  }
  const auto saved = profile.settings.value("audio/device", "auto").toString();
  int index = device->findData(saved);
  if (index < 0) {
    device->addItem(saved + uiTr(" (сейчас недоступно)"), saved);
    index = device->count() - 1;
  }
  device->setCurrentIndex(index);
  audio->addRow(uiTr("Устройство"), device);
  apply->append([this, device] {
    profile.settings.setValue("audio/device", device->currentData());
  });
  auto *channels = new QComboBox;
  channels->setObjectName("audio/channels");
  channels->addItem(uiTr("Системный выбор"), "auto-safe");
  channels->addItem(uiTr("Моно"), "mono");
  channels->addItem(uiTr("Стерео"), "stereo");
  channels->addItem("4.0", "4.0");
  channels->addItem("5.0", "5.0");
  channels->addItem("5.1", "5.1");
  channels->addItem("7.1", "7.1");
  channels->setCurrentIndex(
      std::max(0, channels->findData(
                      profile.settings.value("audio/channels", "auto-safe"))));
  audio->addRow(uiTr("Каналы"), channels);
  apply->append([this, channels] {
    profile.settings.setValue("audio/channels", channels->currentData());
  });
  boolean(audio, uiTr("Выравнивать громкость (динамическая нормализация)"),
          "audio/normalize", false);
  if (!ClassicPreferences::loadingStep(qobject_cast<QDialog *>(tabs->window()))) return;
  auto *video = page(uiTr("Изображение"));
  auto *adjustments = new QGroupBox(uiTr("Управление цветом"));
  auto *colorForm = new QFormLayout(adjustments);
  colorForm->setContentsMargins(8, 12, 8, 8);
  video->addRow(adjustments);
  auto colorNumber = [&](const QString &label, const QString &key, int minimum, int maximum) {
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *number = new QSpinBox;
    number->setObjectName(key); number->setRange(minimum, maximum);
    number->setValue(profile.settings.value(key, 0).toInt());
    number->setFixedWidth(65);
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setObjectName(key + "Slider"); slider->setRange(minimum, maximum);
    slider->setValue(number->value()); slider->setTickPosition(QSlider::TicksBelow);
    slider->setTickInterval(maximum - minimum);
    connect(slider, &QSlider::valueChanged, number, &QSpinBox::setValue);
    connect(number, &QSpinBox::valueChanged, slider, &QSlider::setValue);
    layout->addWidget(number); layout->addWidget(slider, 1);
    colorForm->addRow(label, row);
    apply->append([this, number, key] { profile.settings.setValue(key, number->value()); });
    return number;
  };
  auto *brightness =
      colorNumber(uiTr("Яркость"), "video/brightness", -100, 100);
  auto *contrast =
      colorNumber(uiTr("Контрастность"), "video/contrast", -100, 100);
  auto *hue =
      colorNumber(uiTr("Оттенок"), "video/hue", -180, 180);
  auto *saturation =
      colorNumber(uiTr("Насыщенность"), "video/saturation", -100, 100);
  auto *gamma = number(video, uiTr("Гамма, −100…100 (0 = 1,0)"), "video/gamma",
                       0, -100, 100);
  auto *reset = new QPushButton(uiTr("Сброс"));
  reset->setObjectName("classicColorReset");
  auto *resetRow = new QWidget; auto *resetLayout = new QHBoxLayout(resetRow);
  resetLayout->setContentsMargins(0, 0, 0, 0);
  resetLayout->addStretch(); resetLayout->addWidget(reset); video->addRow(resetRow);
  connect(reset, &QPushButton::clicked, tabs,
          [brightness, contrast, saturation, hue, gamma] {
            brightness->setValue(0);
            contrast->setValue(0);
            saturation->setValue(0);
            hue->setValue(0);
            gamma->setValue(0);
          });
  auto *window = page(uiTr("Окно и кадры"));
  boolean(window, uiTr("Поверх остальных окон"), "ui/onTop", false);
  auto *frameless =
      boolean(window, uiTr("Без рамки (Alt + левая кнопка — перемещение)"),
              "ui/frameless", false);
  frameless->setToolTip(
      uiTr("Ctrl+0 показывает меню; Esc выходит из полного экрана"));
  boolean(window, uiTr("Сообщения поверх видео"), "ui/osd", FoxicMPFamily::Osd);
  boolean(window, uiTr("Показывать оставшееся время"), "ui/remainingTime",
          false);
  boolean(window, uiTr("Включать субтитры в снимок кадра"),
          "ui/screenshotSubtitles", true);
  attachTransformPreferences(tabs, apply);
  if (!ClassicPreferences::loadingStep(qobject_cast<QDialog *>(tabs->window()))) return;
  attachWindowPreferences(tabs, apply);
  if (!ClassicPreferences::loadingStep(qobject_cast<QDialog *>(tabs->window()))) return;
  attachAppearancePreferences(tabs, apply);
  if (!ClassicPreferences::loadingStep(qobject_cast<QDialog *>(tabs->window()))) return;
  attachSubtitlePreferences(tabs, apply);
  if (!ClassicPreferences::loadingStep(qobject_cast<QDialog *>(tabs->window()))) return;
  attachAudioProcessingPreferences(tabs, apply);
  if (!ClassicPreferences::loadingStep(qobject_cast<QDialog *>(tabs->window()))) return;
  attachVideoOutputPreferences(tabs, apply);
  if (!ClassicPreferences::loadingStep(qobject_cast<QDialog *>(tabs->window()))) return;
  attachVideoDisplayPreferences(tabs, apply);
  if (!ClassicPreferences::loadingStep(qobject_cast<QDialog *>(tabs->window()))) return;
  attachVideoShaderPreferences(tabs, apply);
}
void Window::showVideoSettings() {
  QDialog dialog(this);
  dialog.setWindowTitle(uiTr("Изображение и звук"));
  dialog.resize(700, 550);
  auto *layout = new QVBoxLayout(&dialog);
  auto *tabs = new QTabWidget;
  layout->addWidget(tabs);
  QList<std::function<void()>> apply;
  attachMediaPreferences(tabs, &apply);
  tabs->setCurrentIndex(2);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    if (validateAudioFilterPreferences(&dialog) &&
        validateVideoShaderPreferences(&dialog) &&
        validateVideoDisplayPreferences(&dialog))
      dialog.accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() == QDialog::Accepted) {
    for (auto &save : apply)
      save();
    applyMediaOptions();
    preview->setFilters(previewVideoFilters());
    timeline->previewEnabled =
        profile.settings.value("ui/previews", true).toBool();
    applyAppearance();
    applyWindowPreferences();
    actions["onTop"]->setChecked(
        profile.settings.value("ui/onTop", false).toBool());
    actions["frameless"]->setChecked(
        profile.settings.value("ui/frameless", false).toBool());
    if (!profile.settings.value("ui/osd", FoxicMPFamily::Osd).toBool())
      osd->hide();
    updateClock();
  }
}
