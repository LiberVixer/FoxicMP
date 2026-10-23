#include "windowtitle.h"
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QSettings>
#include <QUrl>
namespace {
QString clean(QString text) {
  text = text.left(2048);
  for (auto &c : text)
    if (c.category() == QChar::Other_Control ||
        c.category() == QChar::Separator_Line ||
        c.category() == QChar::Separator_Paragraph)
      c = QChar(' ');
  return text.trimmed();
}
QString expand(const QString &pattern, const QMap<QString, QString> &values) {
  QString out;
  // Scan only the template: braces inside metadata are always literal.
  for (int i = 0; i < pattern.size() && out.size() < 2048;) {
    if (pattern.mid(i, 2) == "{{" || pattern.mid(i, 2) == "}}") {
      out += pattern[i];
      i += 2;
    } else if (pattern[i] == '{') {
      const auto end = pattern.indexOf('}', i + 1);
      if (end >= 0 && values.contains(pattern.mid(i + 1, end - i - 1))) {
        out += values.value(pattern.mid(i + 1, end - i - 1));
        i = end + 1;
      } else
        out += pattern[i++];
    } else
      out += pattern[i++];
  }
  return clean(out);
}
} // namespace
QString WindowTitle::idle() { return "FoxicMP x64 0.1.66"; }
QString WindowTitle::path(const QString &source) {
  const QUrl url(source);
  if (url.isLocalFile())
    return QDir::toNativeSeparators(url.toLocalFile());
  if (!url.scheme().isEmpty())
    return url.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveQuery |
                               QUrl::RemoveFragment);
  return QDir::toNativeSeparators(QFileInfo(source).absoluteFilePath());
}
QString WindowTitle::name(const QString &source) {
  const QUrl url(source);
  const auto local = url.isLocalFile() ? url.toLocalFile()
                    : !url.scheme().isEmpty() ? url.path()
                                             : source;
  const auto file = QFileInfo(local).fileName();
  return file.isEmpty() ? path(source) : file;
}
QString WindowTitle::render(const QSettings &settings, const Media &media) {
  return format(media, settings.value("ui/titleMode", "title").toString(),
                settings.value("ui/titleTemplate", "{title} - {app}").toString(),
                settings.value("ui/titleState", false).toBool(),
                settings.value("ui/titlePosition", false).toBool());
}
QString WindowTitle::format(const Media &media, const QString &mode,
                           const QString &pattern, bool showState,
                           bool showPosition) {
  if (!media.active || media.source.isEmpty())
    return idle();
  const QString file = clean(name(media.source)),
                full = clean(path(media.source)),
                title = clean(media.title).isEmpty() ? file : clean(media.title);
  QString result;
  if (mode == "custom") {
    result = expand(pattern.left(2048),
                    {{"name", file}, {"path", full}, {"title", title},
                     {"state", clean(media.state)},
                     {"position", clean(media.position)},
                     {"duration", clean(media.duration)},
                     {"app", idle()}});
  } else {
    result = mode == "app" ? idle()
           : mode == "path" ? full : mode == "name" ? file : title;
    if (showState)
      result += " [" + clean(media.state) + "]";
    if (showPosition)
      result += " [" + clean(media.position) + " / " + clean(media.duration) + "]";
    if (mode != "app")
      result += " - " + idle();
  }
  result = clean(result);
  return result.isEmpty() ? clean(title + " - " + idle()) : result;
}
