#include "appearance.h"
#include "branding.h"
#include "theme.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QWidget>
#include <algorithm>
namespace {
QImage toolbar;
}
void Appearance::stylePreferences(QWidget &dialog) {
  QPalette palette = qApp->palette();
  const QColor background = palette.color(QPalette::Window);
  const bool dark = background.lightness() < 128;
  QColor text = palette.color(QPalette::WindowText);
  if (std::abs(text.lightness() - background.lightness()) < 100)
    text = QColor(dark ? "#e3e6e8" : "#202428");
  const QColor field = dark ? background.darker(120) : QColor("#ffffff");
  const QColor button = dark ? background.lighter(120) : QColor("#fafafa");
  const QColor border = dark ? background.lighter(170) : QColor("#b8b8b8");
  const QColor disabled = dark ? QColor("#929ba2") : QColor("#808080");
  const QColor accent = dark ? QColor("#558db5") : QColor("#0078d4");
  const QColor selection = dark ? QColor("#40576b") : QColor("#c2d6ed");
  for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
    palette.setColor(role, text);
    palette.setColor(QPalette::Disabled, role, disabled);
  }
  palette.setColor(QPalette::Base, field);
  palette.setColor(QPalette::Button, button);
  palette.setColor(QPalette::AlternateBase, background);
  palette.setColor(QPalette::Highlight, selection);
  palette.setColor(QPalette::HighlightedText, text);
  palette.setColor(QPalette::PlaceholderText, disabled);
  dialog.setPalette(palette);
  dialog.setStyleSheet(QString(
      "QWidget {background:%1;color:%2;} "
      "QLineEdit,QComboBox,QAbstractSpinBox,QPlainTextEdit,QTextEdit,QTableWidget,QTreeWidget,QListWidget {background:%3;color:%2;selection-background-color:%7;selection-color:%2;} "
      "QWidget:disabled {color:%6;} "
      "QTreeWidget::item:selected,QTableWidget::item:selected,QListWidget::item:selected {background:%7;color:%2;} "
      "QGroupBox {border:1px solid %5;margin-top:7px;} QGroupBox::title {subcontrol-origin:margin;left:6px;} "
      "QLabel#classicPageHeading {background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 %7,stop:1 %1);border:0;padding:1px;} "
      "QPushButton {background:%4;color:%2;border:1px solid %5;border-radius:3px;padding:3px 10px;} "
      "QPushButton:hover,QPushButton:default {border-color:%8;} QPushButton:disabled {color:%6;} "
      "QHeaderView::section {background:%4;color:%2;border:1px solid %5;padding:3px;} "
      "QSlider::groove:horizontal {height:3px;background:%3;border:1px solid %5;} "
      "QSlider::handle:horizontal {width:8px;background:%8;border:0;margin:-5px 0;} "
      "QSlider::handle:horizontal:disabled {background:%6;} "
      "QToolButton#classicLogoPrevious,QToolButton#classicLogoNext {background:%4;border:1px solid %5;border-radius:0;} "
      "QToolButton#classicLogoPrevious:disabled,QToolButton#classicLogoNext:disabled {color:%6;} "
      "QToolTip {background:%3;color:%2;border:1px solid %5;}")
      .arg(background.name(), text.name(), field.name(), button.name(),
           border.name(), disabled.name(), selection.name(), accent.name()));
}
QFont Appearance::playlistFont(QSettings &settings, const QFont &fallback) {
  QFont font(settings.value("appearance/playlistFont", fallback.family()).toString(),
      std::clamp(settings.value("appearance/playlistFontSize", fallback.pointSize()).toInt(), 8, 28));
  font.setPointSizeF(font.pointSizeF() *
      std::clamp(settings.value("appearance/playlistFontPercent", 100).toInt(), 50, 200) / 100.);
  return font;
}
QString Appearance::assetPath(const QString &directory, const QString &path) {
  return path.isEmpty() ? QString() : QDir(directory).absoluteFilePath(path);
}
QImage Appearance::image(const QString &path, int maximum) {
  if (path.isEmpty())
    return {};
  QImageReader reader(path);
  const auto size = reader.size();
  if (!size.isValid() || size.width() > maximum || size.height() > maximum ||
      qint64(size.width()) * size.height() > 16 * 1024 * 1024)
    return {};
  reader.setAutoTransform(true);
  return reader.read();
}
QImage Appearance::builtinLogo(const QSettings &settings, bool preview) {
  const auto theme = settings.value("appearance/theme", "dark").toString();
  // Video remains black in every UI theme; the settings preview follows the
  // dialog palette instead. Keep the wordmark readable on both surfaces.
  const bool light = preview && (theme == "light" ||
      (theme == "system" && qApp->palette().color(QPalette::Window).lightness() >= 128) ||
      (theme == "custom" && QColor(settings.value("appearance/background", "#292e32").toString()).lightness() >= 128));
  return QImage(Branding::logo(light));
}
QImage Appearance::logo(QSettings &settings, const QString &directory) {
  const auto path = settings.value("appearance/logoPath").toString();
  // Profiles created before the explicit mode switch used a nonempty path
  // to select the external image. Keep that choice when upgrading.
  if (settings.value("appearance/logoExternal", !path.isEmpty()).toBool()) {
    const auto external = image(assetPath(directory, path));
    if (!external.isNull()) return external;
  }
  return settings.value("appearance/logoId", 1).toInt() == 0
      ? QImage() : builtinLogo(settings);
}
QImage Appearance::toolbarCell(const QString &name) {
  if (toolbar.isNull())
    return {};
  const int count = toolbar.width() / toolbar.height();
  const QStringList order =
      count == 10
          ? QStringList{"play",  "pause",    "stop", "previous", "next",
                        "frame", "playlist", "open", "volume",   "mute"}
          : QStringList{"play",      "pause", "stop",   "",    "previous",
                        "next",      "frame", "open",   "",    "audio",
                        "subtitles", "",      "volume", "mute"};
  const int index = order.indexOf(name);
  return index < 0 ? QImage()
                   : toolbar.copy(index * toolbar.height(), 0, toolbar.height(),
                                  toolbar.height());
}
void Appearance::apply(QApplication &app, QSettings &settings) {
  static const QPalette systemPalette = app.style()->standardPalette();
  const auto theme = settings.value("appearance/theme", "dark").toString();
  if (theme == "system") {
    app.setStyleSheet({});
    app.setPalette(systemPalette);
  } else {
    const bool light = theme == "light", custom = theme == "custom";
    const auto color = [&](const char *key, const char *fallback) {
      const QColor c(settings.value(key, fallback).toString());
      return c.isValid() ? c : QColor(fallback);
    };
    QColor base = custom ? color("appearance/background", "#292e32")
                               : QColor(light ? "#eef0f2" : "#292e32");
    if (!light) {
      const int brightness = std::clamp(settings.value("appearance/themeBrightness", 15).toInt(), 0, 100) - 15;
      const auto channel = [&](int value, const char *key) {
        return std::clamp(value + brightness, 0, 255) *
            std::clamp(settings.value(key, 255).toInt(), 0, 255) / 255;
      };
      base.setRgb(channel(base.red(), "appearance/themeRed"),
                  channel(base.green(), "appearance/themeGreen"),
                  channel(base.blue(), "appearance/themeBlue"));
    }
    const QColor text = custom ? color("appearance/foreground", "#e3e6e8")
                               : QColor(light ? "#202428" : "#e3e6e8");
    const QColor accent = custom ? color("appearance/accent", "#4a535b")
                                 : QColor(light ? "#b4d9f2" : "#4a535b");
    QPalette palette = systemPalette;
    for (auto role : {QPalette::Window, QPalette::Base, QPalette::Button})
      palette.setColor(role, base);
    for (auto role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
      palette.setColor(role, text);
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, text);
    app.setPalette(palette);
    const auto css =
        QString(
            "QWidget {background:%1;color:%2;} QMenuBar,QMenu {background:%1;} "
            "QMenu::item:disabled {color:#808080;} QMenu::item:selected,QListWidget::item:selected,QTreeWidget::item:"
            "selected {background:%3;} QToolButton "
            "{border:0;border-radius:2px;} QWidget#controls "
            "{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %4,stop:1 "
            "%1);border-top:1px solid %5;} QWidget#controls "
            "QLabel,QWidget#controls QToolButton,QWidget#controls QSlider "
            "{background:transparent;} QToolButton:hover {background:%3;} "
            "QWidget#controls QToolButton:hover {border:1px solid %2;} "
            "QWidget#controls QToolButton:pressed,QWidget#controls QToolButton:checked "
            "{background:%6;border:1px solid %2;} "
            "QSlider::groove:horizontal {height:5px;background:%5;} "
            "QSlider::handle:horizontal {width:7px;background:%2;margin:-4px "
            "0;} QDockWidget {min-width:200px;} QToolTip "
            "{color:%2;background:%4;border:1px solid %5;}")
            .arg(base.name(), text.name(), accent.name(),
                 light ? base.darker(108).name() : base.lighter(145).name(),
                 base.darker(140).name(), accent.darker(light ? 115 : 135).name());
    const bool darkMenu = settings.value("appearance/darkMenu", true).toBool();
    app.setStyleSheet(css + (darkMenu ? QString() : QString(
        "QMenuBar,QMenu,QMenu::item {background:#eef0f2;color:#202428;} "
        "QMenu::item:selected,QMenuBar::item:selected {background:#b4d9f2;} "
        "QMenu::item:disabled {color:#808080;}")));
  }
  toolbar =
      image(assetPath(QFileInfo(settings.fileName()).absolutePath(),
                      settings.value("appearance/toolbarPath").toString()),
            4096);
  if (!toolbar.isNull() &&
      (toolbar.height() < 8 || toolbar.width() % toolbar.height() ||
       !QList<int>{10, 15, 16}.contains(toolbar.width() / toolbar.height())))
    toolbar = {};
  if (toolbar.isNull()) toolbar = QImage(":/classic-toolbar.png");
  if (!toolbar.isNull() && !toolbar.hasAlphaChannel()) {
    toolbar = toolbar.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < toolbar.height(); ++y)
      for (int x = 0; x < toolbar.width(); ++x) {
        const auto c = toolbar.pixelColor(x, y);
        if (c == QColor(255, 0, 255) || c == QColor(192, 192, 192))
          toolbar.setPixelColor(x, y, Qt::transparent);
      }
  }
}
