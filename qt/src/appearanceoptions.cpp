#include "appearance.h"
#include "controls.h"
#include "framepreview.h"
#include "i18n.h"
#include "mediaoptions.h"
#include "mediasource.h"
#include "onlineoptions.h"
#include "osdlabel.h"
#include "player.h"
#include "playlistpanel.h"
#include "timeline.h"
#include "window.h"
#include <QtWidgets>
void Window::applyAppearance() {
  Appearance::apply(*qApp, profile.settings);
  if (auto *dialog = findChild<QDialog *>("preferencesDialog"))
    Appearance::stylePreferences(*dialog);
  const int iconSize = std::clamp(
      profile.settings.value("appearance/buttonSize", 16).toInt(), 12, 48);
  for (auto *button : controls->findChildren<QToolButton *>()) {
    button->setIconSize(QSize(iconSize, iconSize));
    button->setFixedSize(iconSize + 7, iconSize + 6);
    const auto id = button->defaultAction()->objectName().mid(7);
    const auto name = id == "play" ? (paused || stopped ? "play" : "pause")
                      : id == "mute"
                          ? (player->get("mute").toBool() ? "mute" : "volume")
                      : id == "frameNext" ? "frame"
                                          : id;
    button->defaultAction()->setIcon(controlIcon(name));
  }
  gpuIndicator->setPixmap(QIcon(":/classic-gpu.svg").pixmap(iconSize, iconSize));
  const bool darkStatus = qApp->palette().color(QPalette::Window).lightness() < 128;
  const auto classicColor = [this](int red, int green, int blue) {
    const auto channel = [this](int value, const char *key) {
      return std::clamp((value + profile.settings.value("appearance/themeBrightness", 15).toInt()) *
          std::clamp(profile.settings.value(key, 255).toInt(), 0, 255) / 255, 0, 255);
    };
    return QColor(channel(red, "appearance/themeRed"), channel(green, "appearance/themeGreen"), channel(blue, "appearance/themeBlue"));
  };
  QPalette statusPalette = qApp->palette();
  if (darkStatus)
    statusPalette.setColor(QPalette::WindowText, classicColor(165, 170, 175));
  for (auto *label : {state, time}) {
    label->setPalette(statusPalette);
    label->setStyleSheet(QString("background:transparent; color:%1;")
        .arg(statusPalette.color(QPalette::WindowText).name()));
  }
  // Classic's toolbar gradient and status colors use the same brightness/RGB.
  controls->setStyleSheet(darkStatus && profile.settings.value("appearance/theme", "dark").toString() == "dark"
      ? QString("QWidget#controls {background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %1,stop:1 %2);}")
          .arg(classicColor(50, 55, 60).name(), classicColor(20, 25, 30).name())
      : QString());
  if (auto *grip = controls->findChild<QSizeGrip *>("classicResizeGrip")) {
    grip->setProperty("shadow", classicColor(20, 25, 30));
    grip->setProperty("highlight", classicColor(115, 120, 125));
  }
  statusArea->setStyleSheet("QWidget#classicStatusArea {background:transparent;}");
  const QFont font = Appearance::playlistFont(profile.settings, qApp->font());
  for (auto *list : queuePanel->lists())
    list->setFont(font);
  logo->setObjectName("playerLogo");
  auto image = Appearance::logo(profile.settings, profile.directory);
  if (image.width() > 505 || image.height() > 300)
    image = image.scaled(505, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  logo->setPixmap(QPixmap::fromImage(image));
  for (auto *label : {osd, osdInfo, osdClock})
    if (label)
      static_cast<OsdLabel *>(label)->configure(profile.settings);
  if (osdTimer)
    osdTimer->setInterval(std::clamp(
        profile.settings.value("osd/durationMs", 1600).toInt(), 200, 10000));
  if (preview) {
    const int width = std::clamp(
        (screen() ? screen()->availableGeometry().width() : 1280) *
            std::clamp(
                profile.settings.value("preview/screenPercent", 25).toInt(), 5,
                50) /
            100,
        160, 960);
    const bool online =
        profile.settings.value("preview/online", false).toBool();
    preview->configure(
        QSize(width, qRound(width * 9. / 16)), online,
        OnlineOptions::resolveYtDlp(profile.settings),
        profile.settings.value("online/userAgent").toString(),
        std::clamp(profile.settings.value("preview/timeoutMs", 10000).toInt(),
                   1000, 20000));
    preview->setFilters(previewVideoFilters());
    previewImage->setMinimumSize(80, 45);
    previewImage->setMaximumSize(width, qRound(width * 9. / 16));
    preview->cancel();
    previewPopup->hide();
  }
  timeline->timeTooltipEnabled =
      profile.settings.value("ui/timeTooltip", true).toBool();
  timeline->tooltipBelow =
      profile.settings.value("ui/timeTooltipBelow", false).toBool();
  timeline->chapterMarkers =
      profile.settings.value("ui/chapterMarkers", true).toBool();
  QToolTip::hideText();
  const auto volumeColor = [this](const char *key, const char *fallback) {
    const QColor value(profile.settings.value(key, fallback).toString());
    return value.isValid() ? value.name() : QString(fallback);
  };
  volume->setStyleSheet("background:transparent;");
  volume->setProperty("volumeFill", volumeColor("appearance/volumeFill", "#ffffff"));
  volume->setProperty("volumeBackground", volumeColor("appearance/volumeBackground", "#808080"));
  volume->setProperty("themeBrightness", profile.settings.value("appearance/themeBrightness", 15));
  volume->setProperty("themeRed", profile.settings.value("appearance/themeRed", 255));
  volume->setProperty("themeGreen", profile.settings.value("appearance/themeGreen", 255));
  volume->setProperty("themeBlue", profile.settings.value("appearance/themeBlue", 255));
  volume->setProperty("themeDark", darkStatus);
  volume->update();
  timeline->setProperty(
      "appearance/progress",
      profile.settings.value("appearance/timelineColor", "#444b50"));
  timeline->update();
  updateOverlays();
}
void Window::positionOverlays() {
  if (!osd || !osdInfo || !osdClock)
    return;
  const int maxWidth = std::max(80, player->width() - 24);
  for (auto *label : {osd, osdInfo, osdClock}) {
    label->setMaximumWidth(maxWidth);
    label->adjustSize();
    label->raise();
  }
  osdInfo->move(12, 12);
  osd->move(12, osdInfo->isVisible() ? osdInfo->height() + 18 : 12);
  osdClock->move(std::max(12, player->width() - osdClock->width() - 12), 12);
  if (osdClock->isVisible() && osdInfo->isVisible()) {
    osdInfo->setMaximumWidth(
        std::max(80, player->width() - osdClock->width() - 36));
    osdInfo->adjustSize();
    if (osdInfo->geometry().intersects(osdClock->geometry()))
      osdClock->move(osdClock->x(), osdInfo->height() + 18);
  }
}
void Window::updateOverlays() {
  if (!osdInfo || !osdClock)
    return;
  const bool enabled = profile.settings.value("ui/osd", true).toBool();
  QStringList info;
  if (!stopped && profile.settings.value("osd/showName", false).toBool())
    info.append(MediaSource::displayName(current));
  if (!stopped && profile.settings.value("osd/showTime", false).toBool())
    info.append(clockText(pos) + " / " + clockText(duration));
  osdInfo->setText(info.join('\n'));
  osdInfo->setVisible(enabled && !info.isEmpty());
  osdClock->setText(QTime::currentTime().toString("HH:mm:ss"));
  osdClock->setVisible(enabled &&
                       profile.settings.value("osd/showClock", false).toBool());
  if (!enabled)
    osd->hide();
  positionOverlays();
}
void Window::requestPreview(double seconds, const QPoint &point,
                            const QString &text) {
  if (stopped || !profile.settings.value("ui/previews", true).toBool())
    return;
  previewText->setText(text);
  previewImage->clear();
  previewImage->setText(uiTr("Превью…"));
  previewAnchor = point;
  previewPopup->show();
  positionPreview();
  previewPopup->raise();
  if (progressive || duration <= 0) {
    preview->cancel();
    previewImage->setText(uiTr("Для этого источника превью недоступно"));
    return;
  }
  if (MediaSource::isUrl(current) &&
      !profile.settings.value("preview/online", false).toBool()) {
    preview->cancel();
    previewImage->setText(uiTr("Онлайн-превью отключено в параметрах"));
    return;
  }
  const int width = std::clamp(
      (screen() ? screen()->availableGeometry().width() : 1280) *
          std::clamp(
              profile.settings.value("preview/screenPercent", 25).toInt(), 5,
              50) /
          100,
      160, 960);
  preview->configure(
      QSize(width, qRound(width * 9. / 16)),
      profile.settings.value("preview/online", false).toBool(),
      OnlineOptions::resolveYtDlp(profile.settings),
      profile.settings.value("online/userAgent").toString(),
      profile.settings.value("preview/timeoutMs", 10000).toInt());
  previewImage->setMaximumSize(width, qRound(width * 9. / 16));
  preview->request(current,
                   std::clamp(seconds, 0., std::max(0., duration - .05)));
}
void Window::attachAppearancePreferences(QTabWidget *tabs,
                                         QList<std::function<void()>> *apply) {
  const auto page = [&](const QString &title) {
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *widget = new QWidget;
    auto *form = new QFormLayout(widget);
    scroll->setWidget(widget);
    tabs->addTab(scroll, title);
    return form;
  };
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
  const auto number = [&](QFormLayout *form, const QString &label,
                          const QString &key, int fallback, int low, int high) {
    auto *spin = new QSpinBox;
    spin->setObjectName(key);
    spin->setRange(low, high);
    spin->setValue(profile.settings.value(key, fallback).toInt());
    form->addRow(label, spin);
    apply->append(
        [this, spin, key] { profile.settings.setValue(key, spin->value()); });
    return spin;
  };
  const auto font = [&](QFormLayout *form, const QString &label,
                        const QString &key, const QString &fallback) {
    auto *box = new QFontComboBox;
    box->setObjectName(key);
    box->setCurrentFont(
        QFont(profile.settings.value(key, fallback).toString()));
    form->addRow(label, box);
    apply->append([this, box, key] {
      profile.settings.setValue(key, box->currentFont().family());
    });
  };
  const auto color = [&](QFormLayout *form, const QString &label,
                         const QString &key, const QString &fallback) {
    auto *edit =
        new QLineEdit(profile.settings.value(key, fallback).toString());
    edit->setObjectName(key);
    auto *widget = new QWidget;
    auto *row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(edit);
    auto *button = new QPushButton("…");
    button->setMaximumWidth(35);
    row->addWidget(button);
    connect(button, &QPushButton::clicked, tabs, [=] {
      const auto value =
          QColorDialog::getColor(QColor(edit->text()), tabs, uiTr("Цвет"));
      if (value.isValid())
        edit->setText(value.name());
    });
    form->addRow(label, widget);
    apply->append([this, edit, key, fallback] {
      const QColor value(edit->text());
      profile.settings.setValue(key, value.isValid() ? value.name() : fallback);
    });
  };
  auto *interface = page(uiTr("Оформление"));
  interface->setContentsMargins(0, 0, 0, 0);
  interface->setVerticalSpacing(8);
  auto *themeGroup = new QGroupBox;
  themeGroup->setObjectName("classicInterfaceTheme");
  auto *themeForm = new QFormLayout(themeGroup);
  themeForm->setContentsMargins(8, 8, 8, 8);
  themeForm->setVerticalSpacing(4);
  interface->addRow(themeGroup);
  auto *details = new QWidget;
  auto *appearance = new QFormLayout(details);
  appearance->setContentsMargins(0, 0, 0, 0);
  auto *dark = new QCheckBox(uiTr("Использовать \"тёмную\" тему оформления (по умолчанию)"));
  dark->setObjectName("classicDarkTheme");
  themeForm->addRow(dark);
  const auto themeSlider = [&](const QString &label, const QString &key,
                                int fallback, int maximum) {
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setObjectName(key);
    slider->setRange(0, maximum);
    slider->setValue(profile.settings.value(key, fallback).toInt());
    apply->append([this, slider, key] {
      profile.settings.setValue(key, slider->value());
    });
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel(label));
    layout->addWidget(slider, 1);
    return row;
  };
  auto *paletteRow = new QWidget;
  auto *paletteLayout = new QGridLayout(paletteRow);
  paletteLayout->setContentsMargins(0, 0, 0, 0);
  paletteLayout->setHorizontalSpacing(12);
  paletteLayout->setVerticalSpacing(2);
  paletteLayout->setColumnStretch(0, 3);
  paletteLayout->setColumnStretch(1, 1);
  paletteLayout->addWidget(themeSlider(uiTr("Яркость :"), "appearance/themeBrightness", 15, 100), 0, 0);
  paletteLayout->addWidget(new QLabel(uiTr("Настройка цвета индикатора громкости:")), 1, 0);
  auto *volumeColors = new QWidget;
  auto *volumeLayout = new QHBoxLayout(volumeColors);
  volumeLayout->setContentsMargins(0, 0, 0, 0);
  const auto swatch = [&](const QString &label, const QString &key,
                          const QString &fallback) {
    auto *edit = new QLineEdit(profile.settings.value(key, fallback).toString(), details);
    edit->setObjectName(key);
    edit->hide();
    auto *button = new QPushButton;
    button->setObjectName(key + "Button");
    button->setAccessibleName(label);
    button->setFixedSize(32, 23);
    const auto update = [edit, button] {
      const QColor value(edit->text());
      if (value.isValid()) {
        button->setStyleSheet("background-color:" + value.name() + ";border:1px solid #707070;");
        button->setToolTip(value.name());
      }
    };
    connect(edit, &QLineEdit::textChanged, button, update);
    connect(button, &QPushButton::clicked, tabs, [=] {
      const auto value = QColorDialog::getColor(QColor(edit->text()), tabs, label);
      if (value.isValid()) edit->setText(value.name());
    });
    apply->append([this, edit, key, fallback] {
      const QColor value(edit->text());
      profile.settings.setValue(key, value.isValid() ? value.name() : fallback);
    });
    volumeLayout->addWidget(new QLabel(label));
    volumeLayout->addWidget(button);
    update();
    return edit;
  };
  auto *volumeFill = swatch(uiTr("Заливка:"), "appearance/volumeFill", "#ffffff");
  auto *volumeBackground = swatch(uiTr("Фон:"), "appearance/volumeBackground", "#808080");
  auto *resetVolume = new QPushButton(uiTr("По умолчанию"));
  resetVolume->setObjectName("classicVolumeColorsReset");
  volumeLayout->addWidget(resetVolume);
  volumeLayout->addStretch();
  connect(resetVolume, &QPushButton::clicked, tabs, [=] {
    volumeFill->setText("#ffffff"); volumeBackground->setText("#808080");
  });
  paletteLayout->addWidget(volumeColors, 2, 0);
  for (int i = 0; i < 3; ++i)
    paletteLayout->addWidget(themeSlider(QString("%1:").arg(QChar::fromLatin1("RGB"[i])),
        QString("appearance/theme%1").arg(QStringList{"Red", "Green", "Blue"}[i]), 255, 255), i, 1);
  themeForm->addRow(paletteRow);
  check(themeForm, uiTr("Использовать тёмное меню"), "appearance/darkMenu", true);
  auto *iconSize = number(themeForm, uiTr("Размер кнопок управления:"),
      "appearance/buttonSize", 16, 12, 48);
  iconSize->setFixedWidth(68);
  auto *theme = new QComboBox;
  theme->setObjectName("appearance/theme");
  for (const auto &entry :
       QList<QPair<QString, QString>>{{uiTr("Тёмная FoxicMP"), "dark"},
                                      {uiTr("Светлая"), "light"},
                                      {uiTr("Системная"), "system"},
                                      {uiTr("Свои цвета"), "custom"}})
    theme->addItem(entry.first, entry.second);
  theme->setCurrentIndex(std::max(
      0, theme->findData(profile.settings.value("appearance/theme", "dark"))));
  dark->setChecked(theme->currentData() == "dark");
  connect(dark, &QCheckBox::toggled, theme, [theme](bool enabled) {
    theme->setCurrentIndex(theme->findData(enabled ? "dark" : "light"));
  });
  connect(theme, &QComboBox::currentIndexChanged, dark, [theme, dark, paletteRow] {
    const QSignalBlocker blocker(dark);
    dark->setChecked(theme->currentData() == "dark");
    paletteRow->setEnabled(theme->currentData() == "dark" || theme->currentData() == "custom");
  });
  paletteRow->setEnabled(theme->currentData() == "dark" || theme->currentData() == "custom");
  appearance->addRow(uiTr("Тема:"), theme);
  apply->append([this, theme] {
    profile.settings.setValue("appearance/theme", theme->currentData());
  });
  color(appearance, uiTr("Фон:"), "appearance/background", "#292e32");
  color(appearance, uiTr("Текст и значки:"), "appearance/foreground",
        "#e3e6e8");
  color(appearance, uiTr("Выделение:"), "appearance/accent", "#4a535b");
  color(appearance, uiTr("Пройденная часть шкалы:"), "appearance/timelineColor",
        "#444b50");
  font(appearance, uiTr("Шрифт плейлиста:"), "appearance/playlistFont",
       qApp->font().family());
  number(appearance, uiTr("Размер шрифта плейлиста, пункты:"),
         "appearance/playlistFontSize", qApp->font().pointSize(), 8, 28);
  auto *logoPage = page(uiTr("Логотип"));
  logoPage->setContentsMargins(0, 0, 0, 0);
  auto *logoPreview = new QLabel;
  logoPreview->setObjectName("classicLogoPreview");
  logoPreview->setAlignment(Qt::AlignCenter);
  logoPreview->setMinimumHeight(310);
  logoPage->addRow(logoPreview);
  auto *internal = new QRadioButton(uiTr("Встроенный:"));
  internal->setObjectName("classicLogoInternal");
  auto *external = new QRadioButton(uiTr("Внешний:"));
  external->setObjectName("classicLogoExternal");
  auto *modes = new QButtonGroup(logoPage->parentWidget());
  modes->addButton(internal); modes->addButton(external);
  auto *edit = new QLineEdit(profile.settings.value("appearance/logoPath").toString());
  edit->setObjectName("appearance/logoPath");
  auto *builtin = new QComboBox;
  builtin->setObjectName("appearance/logoId"); builtin->hide();
  builtin->addItem(uiTr("Без логотипа"), 0);
  builtin->addItem(uiTr("Стандартный"), 1);
  builtin->setCurrentIndex(profile.settings.value("appearance/logoId", 1).toInt() == 0 ? 0 : 1);
  auto *internalRow = new QWidget;
  auto *internalLayout = new QHBoxLayout(internalRow);
  internalLayout->setContentsMargins(0, 0, 0, 0); internalLayout->setSpacing(0);
  auto *previous = new QToolButton; previous->setArrowType(Qt::LeftArrow);
  previous->setObjectName("classicLogoPrevious");
  auto *next = new QToolButton; next->setArrowType(Qt::RightArrow);
  next->setObjectName("classicLogoNext");
  auto *author = new QLabel; author->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  author->setObjectName("classicLogoAuthor");
  for (auto *button : {previous, next}) button->setFixedSize(26, 22);
  internalLayout->addWidget(previous); internalLayout->addWidget(next);
  internalLayout->addStretch(); internalLayout->addWidget(author); internalLayout->addWidget(builtin);
  logoPage->addRow(internal, internalRow);
  auto *externalRow = new QWidget;
  auto *externalLayout = new QHBoxLayout(externalRow);
  externalLayout->setContentsMargins(0, 0, 0, 0);
  auto *browse = new QPushButton(uiTr("Выбрать…")); browse->setObjectName("classicLogoBrowse");
  externalLayout->addWidget(edit, 1); externalLayout->addWidget(browse);
  logoPage->addRow(external, externalRow);
  const auto updateLogo = [this, edit, builtin, internal, external, previous, next, author, logoPreview] {
    const bool useExternal = external->isChecked();
    previous->setEnabled(!useExternal); next->setEnabled(!useExternal);
    author->setText(!useExternal ? builtin->currentText() : QString());
    author->setMinimumWidth(author->fontMetrics().horizontalAdvance(author->text()) + 4);
    auto image = useExternal
        ? Appearance::image(Appearance::assetPath(profile.directory, edit->text()))
        : (builtin->currentData().toInt() == 0 ? QImage() : QImage(":/classic-logo.png"));
    // Invalid external paths have the same safe fallback as the player.
    if (useExternal && image.isNull() && builtin->currentData().toInt() != 0)
      image = QImage(":/classic-logo.png");
    if (image.width() > 420 || image.height() > 300)
      image = image.scaled(420, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    logoPreview->setPixmap(QPixmap::fromImage(image));
    internal->setProperty("logoSelection", builtin->currentData());
  };
  const bool useExternal = profile.settings.value("appearance/logoExternal", !edit->text().isEmpty()).toBool();
  internal->setChecked(!useExternal); external->setChecked(useExternal);
  connect(internal, &QRadioButton::toggled, logoPreview, updateLogo);
  connect(external, &QRadioButton::toggled, logoPreview, updateLogo);
  connect(edit, &QLineEdit::textChanged, logoPreview, updateLogo);
  connect(builtin, &QComboBox::currentIndexChanged, logoPreview, updateLogo);
  for (auto *button : {previous, next})
    connect(button, &QToolButton::clicked, builtin, [builtin, internal] {
      internal->setChecked(true); builtin->setCurrentIndex(1 - builtin->currentIndex());
    });
  connect(browse, &QPushButton::clicked, tabs, [=] {
    const auto path = QFileDialog::getOpenFileName(tabs, uiTr("Изображение"), profile.directory,
        uiTr("Изображения (*.png *.bmp *.jpg *.jpeg)"));
    if (!path.isEmpty()) {
      edit->setText(QDir(profile.directory).relativeFilePath(path)); external->setChecked(true);
    }
  });
  updateLogo();
  apply->append([this, edit, external, builtin] {
    profile.settings.setValue("appearance/logoPath", edit->text().trimmed());
    profile.settings.setValue("appearance/logoExternal", external->isChecked());
    profile.settings.setValue("appearance/logoId", builtin->currentData());
  });
  auto *toolbarPath = new QLineEdit(profile.settings.value("appearance/toolbarPath").toString());
  toolbarPath->setObjectName("appearance/toolbarPath");
  auto *toolbarRow = new QWidget; auto *toolbarLayout = new QHBoxLayout(toolbarRow);
  toolbarLayout->setContentsMargins(0, 0, 0, 0);
  auto *toolbarBrowse = new QPushButton(uiTr("Выбрать…"));
  toolbarLayout->addWidget(toolbarPath, 1); toolbarLayout->addWidget(toolbarBrowse);
  appearance->addRow(uiTr("Изображение кнопок:"), toolbarRow);
  connect(toolbarBrowse, &QPushButton::clicked, tabs, [=] {
    const auto path = QFileDialog::getOpenFileName(tabs, uiTr("Изображение"), profile.directory,
        uiTr("Изображения (*.png *.bmp *.jpg *.jpeg)"));
    if (!path.isEmpty()) toolbarPath->setText(QDir(profile.directory).relativeFilePath(path));
  });
  apply->append([this, toolbarPath] { profile.settings.setValue("appearance/toolbarPath", toolbarPath->text().trimmed()); });
  auto *note = new QLabel(
      uiTr("Пустой или недоступный файл — встроенное изображение. Пути "
           "относительны папке программы. Панель: PNG/BMP, ряд из 10 "
           "квадратных значков FoxicMP или 15/16 Classic; "
           "неподдерживаемые кнопки используют встроенный значок."));
  note->setWordWrap(true);
  appearance->addRow(note);
  auto *language = new QComboBox;
  language->setObjectName("appearance/language");
  language->addItem(uiTr("Русский"), "ru");
  language->addItem("English", "en");
  language->addItem(uiTr("Внешний языковой пакет"), "custom");
  language->setCurrentIndex(std::max(
      0,
      language->findData(profile.settings.value("appearance/language", "ru"))));
  appearance->addRow(uiTr("Язык (после перезапуска):"), language);
  apply->append([this, language] {
    profile.settings.setValue("appearance/language", language->currentData());
  });
  auto *languageFile = new QLineEdit(
      profile.settings.value("appearance/languageFile").toString());
  languageFile->setObjectName("appearance/languageFile");
  appearance->addRow(uiTr("Пакет перевода, JSON:"), languageFile);
  apply->append([this, languageFile] {
    profile.settings.setValue("appearance/languageFile",
                              languageFile->text().trimmed());
  });
  auto *osdPage = page("OSD");
  font(osdPage, uiTr("Шрифт:"), "osd/font", "DejaVu Sans");
  number(osdPage, uiTr("Размер шрифта, логические пиксели:"), "osd/fontSize",
         18, 10, 72);
  color(osdPage, uiTr("Текст:"), "osd/textColor", "#ffffff");
  color(osdPage, uiTr("Фон:"), "osd/backgroundColor", "#121518");
  color(osdPage, uiTr("Рамка:"), "osd/borderColor", "#69727a");
  number(osdPage, uiTr("Непрозрачность фона, 0–255:"), "osd/opacity", 210, 0,
         255);
  number(osdPage, uiTr("Ширина рамки:"), "osd/borderWidth", 0, 0, 8);
  number(osdPage, uiTr("Время сообщения, мс:"), "osd/durationMs", 1600, 200,
         10000);
  check(osdPage, uiTr("Тень текста"), "osd/shadow", true);
  check(osdPage, uiTr("Сглаживание текста"), "osd/antialias", true);
  check(osdPage, uiTr("Постоянно показывать имя файла"), "osd/showName", false);
  check(osdPage, uiTr("Постоянно показывать позицию и длительность"),
        "osd/showTime", false);
  check(osdPage, uiTr("Показывать часы"), "osd/showClock", false);
  auto *seekGroup = new QGroupBox;
  seekGroup->setObjectName("classicInterfaceSeek");
  auto *seekForm = new QFormLayout(seekGroup);
  seekForm->setContentsMargins(8, 8, 8, 8);
  seekForm->setVerticalSpacing(4);
  interface->addRow(seekGroup);
  auto *timeRow = new QWidget;
  auto *timeLayout = new QHBoxLayout(timeRow);
  timeLayout->setContentsMargins(0, 0, 0, 0);
  auto *timeTooltip = new QCheckBox(uiTr("Показывать время"));
  timeTooltip->setObjectName("ui/timeTooltip");
  timeTooltip->setChecked(profile.settings.value("ui/timeTooltip", true).toBool());
  auto *timePosition = new QComboBox;
  timePosition->setObjectName("ui/timeTooltipBelow");
  timePosition->addItem(uiTr("над полосой прокрутки"), false);
  timePosition->addItem(uiTr("под полосой прокрутки"), true);
  timePosition->setCurrentIndex(profile.settings.value("ui/timeTooltipBelow", false).toBool() ? 1 : 0);
  timeLayout->addWidget(timeTooltip); timeLayout->addStretch(); timeLayout->addWidget(timePosition);
  seekForm->addRow(timeRow);
  auto *previewOptions = new QWidget;
  auto *previewLayout = new QHBoxLayout(previewOptions);
  previewLayout->setContentsMargins(0, 0, 0, 0);
  auto *seekPreview = new QCheckBox(uiTr("Использовать предпросмотр при поиске видеофрагмента"));
  seekPreview->setObjectName("ui/previews");
  seekPreview->setChecked(profile.settings.value("ui/previews", true).toBool());
  auto *onlinePreview = new QCheckBox(uiTr("Онлайн"));
  onlinePreview->setObjectName("preview/online");
  onlinePreview->setChecked(profile.settings.value("preview/online", false).toBool());
  previewLayout->addWidget(seekPreview); previewLayout->addStretch(); previewLayout->addWidget(onlinePreview);
  seekForm->addRow(previewOptions);
  apply->append([this, seekPreview, onlinePreview] {
    profile.settings.setValue("ui/previews", seekPreview->isChecked());
    profile.settings.setValue("preview/online", onlinePreview->isChecked());
  });
  auto *previewSize = number(seekForm, uiTr("Размер относительно размера экрана:"),
         "preview/screenPercent", 25, 5, 50);
  previewSize->setSuffix(" %"); previewSize->setFixedWidth(68);
  check(seekForm, uiTr("Отображать метки глав на полосе прокрутки"), "ui/chapterMarkers", true);
  auto *fontPercent = number(seekForm, uiTr("Размер шрифта плейлиста:"),
      "appearance/playlistFontPercent", 100, 50, 200);
  fontPercent->setSuffix(" %"); fontPercent->setFixedWidth(68);
  const auto updateSeekControls = [=] {
    timeTooltip->setEnabled(!seekPreview->isChecked());
    timePosition->setEnabled(!seekPreview->isChecked() && timeTooltip->isChecked());
    onlinePreview->setEnabled(seekPreview->isChecked());
    previewSize->setEnabled(seekPreview->isChecked());
  };
  connect(seekPreview, &QCheckBox::toggled, tabs, updateSeekControls);
  connect(timeTooltip, &QCheckBox::toggled, tabs, updateSeekControls);
  updateSeekControls();
  apply->append([this, timeTooltip, timePosition] {
    profile.settings.setValue("ui/timeTooltip", timeTooltip->isChecked());
    profile.settings.setValue("ui/timeTooltipBelow", timePosition->currentData());
  });
  number(appearance, uiTr("Ожидание онлайн-превью, мс:"), "preview/timeoutMs",
         10000, 1000, 20000);
  auto *previewNote = new QLabel(uiTr(
      "Превью создаёт отдельный ffmpeg: оно не меняет позицию плеера. Для "
      "веб-страниц нужен yt-dlp из настроек веб-видео. Онлайн-превью передаёт "
      "дополнительные сетевые запросы; по умолчанию оно выключено. Прямые "
      "входы, неизвестная длительность и растущие источники не используются. "
      "Ширина ограничена 160–960 пикселями, кэш — 16 МиБ."));
  previewNote->setObjectName("settingsPreviewHelp");
  previewNote->setWordWrap(true);
  appearance->addRow(previewNote);
  auto *expand = new QToolButton;
  expand->setObjectName("classicInterfaceDetails");
  expand->setText(uiTr("Дополнительно"));
  expand->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  expand->setArrowType(Qt::RightArrow);
  expand->setCheckable(true);
  interface->addRow(expand);
  interface->addRow(details);
  details->hide();
  details->setProperty("settingsExpandButton", QVariant::fromValue<QObject *>(expand));
  connect(expand, &QToolButton::toggled, details, [=](bool enabled) {
    details->setVisible(enabled);
    expand->setArrowType(enabled ? Qt::DownArrow : Qt::RightArrow);
  });
}
