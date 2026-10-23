#include "i18n.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QTranslator>
namespace {
class Catalog : public QTranslator {
public:
  using QTranslator::QTranslator;
  QJsonObject entries;
  bool isEmpty() const override { return entries.isEmpty(); }
  QString translate(const char *context, const char *source, const char *,
                    int) const override {
    if (qstrcmp(context, "FoxicMP"))
      return {};
    const auto entry = entries.value(QString::fromUtf8(source));
    return entry.isString() ? entry.toString() : QString();
  }
};
QStringList placeholders(const QString &text) {
  QStringList result;
  auto matches = QRegularExpression("%L?[1-9][0-9]*|%n").globalMatch(text);
  while (matches.hasNext())
    result.append(matches.next().captured());
  result.sort();
  return result;
}
QPointer<Catalog> active;
QPointer<QTranslator> standard;
} // namespace
bool I18n::load(QSettings &settings, const QString &directory) {
  if (active) {
    QCoreApplication::removeTranslator(active);
    delete active;
  }
  if (standard) {
    QCoreApplication::removeTranslator(standard);
    delete standard;
  }
  const auto language = settings.value("appearance/language", "ru").toString();
  if (language == "ru") {
    standard = new QTranslator(qApp);
    if (standard->load("qtbase_ru",
                       QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
      QCoreApplication::installTranslator(standard);
    return true;
  }
  const auto path =
      language == "en"
          ? QString(":/translations/en.json")
          : QDir(directory).absoluteFilePath(
                settings.value("appearance/languageFile").toString());
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024)
    return false;
  const auto object = QJsonDocument::fromJson(file.readAll()).object();
  const auto entries = object.value("translations").toObject();
  if (entries.isEmpty())
    return false;
  for (auto entry = entries.begin(); entry != entries.end(); ++entry)
    if (!entry.value().isString() ||
        placeholders(entry.key()) != placeholders(entry.value().toString()))
      return false;
  active = new Catalog(qApp);
  active->entries = entries;
  return QCoreApplication::installTranslator(active);
}
