#include "videoshaders.h"
#include "i18n.h"
#include "player.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QRegularExpression>

QString VideoShaders::preset(const QString &id) {
  if (id == "gray")
    return "vec4 hook() {\n    vec4 c = HOOKED_tex(HOOKED_pos);\n    float y = "
           "dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));\n    return "
           "vec4(vec3(y), c.a);\n}\n";
  if (id == "invert")
    return "vec4 hook() {\n    vec4 c = HOOKED_tex(HOOKED_pos);\n    return "
           "vec4(vec3(1.0) - c.rgb, c.a);\n}\n";
  if (id == "sharpen")
    return "vec4 hook() {\n    vec4 c = HOOKED_tex(HOOKED_pos);\n    vec3 n = "
           "HOOKED_texOff(vec2(1,0)).rgb + HOOKED_texOff(vec2(-1,0)).rgb\n     "
           "      + HOOKED_texOff(vec2(0,1)).rgb + "
           "HOOKED_texOff(vec2(0,-1)).rgb;\n    return vec4(clamp(c.rgb * 1.4 "
           "- n * 0.1, 0.0, 1.0), c.a);\n}\n";
  return "vec4 hook() {\n    return HOOKED_tex(HOOKED_pos);\n}\n";
}
QString VideoShaders::validateStructure(const QList<Entry> &entries) {
  if (entries.size() > 16)
    return uiTr("Шейдеры: не более 16 элементов.");
  for (const auto &entry : entries) {
    if (entry.name.trimmed().isEmpty() || entry.name.size() > 80 ||
        entry.name.contains(QChar(0)) || entry.name.contains('\n') ||
        entry.name.contains('\r'))
      return uiTr("Шейдеры: имя от 1 до 80 символов без перевода строки.");
    if (entry.stage != "MAIN" && entry.stage != "SCALED")
      return uiTr("Шейдеры: этап MAIN или SCALED.");
    if (entry.source.toUtf8().size() > 65536 || entry.source.contains(QChar(0)))
      return uiTr("Шейдеры: исходник до 64 КиБ без NUL.");
    for (const auto &line : entry.source.split('\n'))
      if (line.trimmed().startsWith("//!") || line.trimmed().startsWith('#'))
        return uiTr("Шейдеры: вставьте GLSL с vec4 hook(), без директив и "
                    "метаданных mpv.");
  }
  if (encode(entries).size() > 2 * 1024 * 1024)
    return uiTr("Шейдеры: повреждённый или слишком большой список в INI.");
  return {};
}
QByteArray VideoShaders::encode(const QList<Entry> &entries) {
  QJsonArray array;
  for (const auto &entry : entries)
    array.append(QJsonObject{{"name", entry.name},
                             {"stage", entry.stage},
                             {"source", entry.source},
                             {"enabled", entry.enabled}});
  return QJsonDocument(array).toJson(QJsonDocument::Compact);
}
QList<VideoShaders::Entry> VideoShaders::read(QSettings &s, QString *error) {
  if (error)
    error->clear();
  const auto bytes = s.value("video/shadersV1").toByteArray();
  if (bytes.isEmpty())
    return {};
  QJsonParseError parse;
  const auto doc = bytes.size() <= 2 * 1024 * 1024
                       ? QJsonDocument::fromJson(bytes, &parse)
                       : QJsonDocument();
  QList<Entry> entries;
  QString failure;
  if (bytes.size() > 2 * 1024 * 1024 ||
      parse.error != QJsonParseError::NoError || !doc.isArray() ||
      doc.array().size() > 16)
    failure = uiTr("Шейдеры: повреждённый или слишком большой список в INI.");
  else {
    for (const auto &value : doc.array()) {
      const auto obj = value.toObject();
      entries.append(
          {obj.value("name").toString(), obj.value("stage").toString(),
           obj.value("source").toString(), obj.value("enabled").toBool()});
    }
    failure = validateStructure(entries);
  }
  if (!failure.isEmpty()) {
    if (error)
      *error = failure;
    return {};
  }
  return entries;
}
bool VideoShaders::importMpvFile(const QString &path, QList<Entry> *entries,
                                 QString *error) {
  if (entries)
    entries->clear();
  if (error)
    error->clear();
  if (!entries)
    return false;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error)
      *error = uiTr("GLSL: не удалось открыть файл для чтения.");
    return false;
  }
  if (file.size() > 512 * 1024) {
    if (error)
      *error = uiTr("GLSL: файл импорта не должен превышать 512 КиБ.");
    return false;
  }
  const auto bytes = file.readAll();
  if (bytes.contains('\0')) {
    if (error)
      *error = uiTr("GLSL: файл импорта содержит NUL.");
    return false;
  }
  QString text = QString::fromUtf8(bytes);
  if (text.contains(QChar::ReplacementCharacter)) {
    if (error)
      *error = uiTr("GLSL: файл импорта должен быть UTF-8.");
    return false;
  }
  const auto lines = text.split('\n');
  QList<Entry> imported;
  Entry current;
  bool active = false;
  QStringList body;
  const auto fail = [&](const QString &message) {
    if (error)
      *error = message;
    imported.clear();
    return false;
  };
  const auto finish = [&]() {
    if (!active)
      return true;
    current.source = body.join('\n').trimmed() + '\n';
    if (current.source.trimmed().isEmpty())
      return false;
    if (current.name.trimmed().isEmpty())
      current.name = QFileInfo(path).completeBaseName() +
                     QStringLiteral(" #%1").arg(imported.size() + 1);
    imported.append(current);
    body.clear();
    return imported.size() <= 16;
  };
  for (int i = 0; i < lines.size(); ++i) {
    QString line = lines.at(i);
    if (line.endsWith('\r'))
      line.chop(1);
    const QString trimmed = line.trimmed();
    if (trimmed.startsWith("//!")) {
      const QString directive = trimmed.mid(3).trimmed();
      const int space = directive.indexOf(QRegularExpression("\\s"));
      const QString key = (space < 0 ? directive : directive.left(space)).toUpper();
      const QString value = (space < 0 ? QString() : directive.mid(space).trimmed());
      if (key == "HOOK") {
        if (!finish())
          return fail(imported.size() >= 16
                          ? uiTr("GLSL: импорт ограничен 16 проходами.")
                          : uiTr("GLSL: проход //!HOOK не содержит исходник."));
        if (value != "MAIN" && value != "SCALED")
          return fail(uiTr("GLSL: поддерживаются только //!HOOK MAIN и SCALED."));
        current = Entry{};
        current.stage = value;
        current.enabled = true;
        active = true;
        continue;
      }
      if (!active)
        return fail(uiTr("GLSL: директива находится до первого //!HOOK."));
      if (key == "DESC") {
        current.name = value;
        continue;
      }
      if (key == "BIND" && value == "HOOKED")
        continue;
      return fail(uiTr("GLSL: неподдерживаемая многопроходная директива //!%1.").arg(key));
    }
    if (trimmed.startsWith('#'))
      return fail(uiTr("GLSL: #version и другие препроцессорные директивы убираются при импорте."));
    if (!active) {
      if (trimmed.isEmpty() || trimmed.startsWith("//"))
        continue;
      return fail(uiTr("GLSL: исходник должен начинаться с //!HOOK MAIN или SCALED."));
    }
    body.append(line);
  }
  if (!finish())
    return fail(imported.size() >= 16
                    ? uiTr("GLSL: импорт ограничен 16 проходами.")
                    : uiTr("GLSL: последний проход не содержит исходник."));
  if (imported.isEmpty())
    return fail(uiTr("GLSL: в файле не найдено ни одного //!HOOK MAIN или SCALED."));
  const auto structure = validateStructure(imported);
  if (!structure.isEmpty())
    return fail(structure);
  *entries = imported;
  return true;
}
QString VideoShaders::compile(const QString &source) {
  const auto helper = QDir(QCoreApplication::applicationDirPath())
                          .filePath("foxicmp-shader-check");
  QProcess process;
  process.start(helper, QStringList{});
  if (!process.waitForStarted(1000))
    return uiTr("Недоступен проверяющий модуль шейдеров.");
  process.write(source.toUtf8());
  process.closeWriteChannel();
  if (!process.waitForFinished(4000)) {
    process.kill();
    process.waitForFinished(1000);
    return uiTr("Проверка шейдера превысила 4 секунды.");
  }
  const auto obj =
      QJsonDocument::fromJson(process.readAllStandardOutput()).object();
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
      !obj.value("ok").toBool())
    return uiTr("GLSL: %1")
        .arg(obj.value("error")
                 .toString(uiTr("Проверка не завершена; нужен OpenGL 3.3."))
                 .left(1500));
  return {};
}
QString VideoShaders::nativeSource(const Entry &entry) {
  return "//!HOOK " + entry.stage +
         "\n//!BIND HOOKED\n//!DESC FoxicMP: " + entry.name + "\n" +
         entry.source;
}
bool VideoShaders::apply(Player *p, QSettings &s) {
  QString error;
  const auto entries = read(s, &error);
  QStringList files;
  if (error.isEmpty() && s.value("video/shadersEnabled", false).toBool()) {
    const auto directory = QFileInfo(s.fileName()).absolutePath();
    for (const auto &entry : entries) {
      if (!entry.enabled)
        continue;
      const auto bytes = nativeSource(entry).toUtf8();
      const auto file = QDir(directory).filePath(
          "Shaders/" +
          QString::fromLatin1(
              QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
                  .toHex()) +
          ".glsl");
      QFile previous(file);
      bool exists = previous.open(QIODevice::ReadOnly) &&
                    previous.size() == bytes.size() &&
                    previous.readAll() == bytes;
      previous.close();
      if (!exists) {
        QSaveFile out(file);
        if (!QDir(directory).mkpath("Shaders") ||
            !out.open(QIODevice::WriteOnly) ||
            out.write(bytes) != bytes.size() || !out.commit()) {
          error = uiTr("Не удалось записать шейдер в папку программы.");
          break;
        }
      }
      files << file;
    }
  }
  if (!error.isEmpty())
    files.clear();
  const auto current = p->get("glsl-shaders").toStringList();
  bool ok = error.isEmpty();
  if (current != files)
    ok = p->setList("glsl-shaders", files) && ok;
  p->setProperty("foxicmpShaderStatus",
                 !error.isEmpty() ? error
                 : !ok            ? uiTr("libmpv отклонил список шейдеров.")
                 : files.isEmpty()
                     ? uiTr("Шейдеры выключены.")
                     : uiTr("Передано шейдеров: %1. Действуют только в OpenGL; "
                            "проверьте проходы и изображение.")
                           .arg(files.size()));
  return ok;
}
