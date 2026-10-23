#include "playlistfile.h"
#include "i18n.h"
#include "mediasource.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <cmath>
namespace {
QString resolve(QString value, const QDir &base, QString *error) {
  value = value.trimmed();
  const QUrl url(value, QUrl::StrictMode);
  if (value.isEmpty()) {
    *error = uiTr("Пустой источник");
    return {};
  }
  if (MediaSource::isUrl(value))
    return MediaSource::normalize(value, error);
  if (value.contains("://") && !url.isLocalFile()) {
    *error = uiTr("Неподдерживаемый сетевой адрес: ") + value;
    return {};
  }
  if (QRegularExpression("^[A-Za-z]:[/\\\\]").match(value).hasMatch() ||
      value.startsWith("\\\\")) {
    *error = uiTr("Путь Windows необходимо заменить путём Linux: ") + value;
    return {};
  }
  if (url.isLocalFile())
    return MediaSource::normalize(url.toLocalFile(), error);
  if (!QFileInfo::exists(base.absoluteFilePath(value)))
    value.replace('\\', '/');
  return MediaSource::normalize(base.absoluteFilePath(value), error);
}
QString relative(const QString &source, const QDir &base) {
  QString path =
      MediaSource::isUrl(source) ? source : base.relativeFilePath(source);
  if (!MediaSource::isUrl(source) &&
      (path.startsWith('#') || path.contains(':')))
    path.prepend("./");
  return path;
}
QByteArray metadata(const PlaylistEntry &entry, const QDir &base) {
  auto data = entry.metadata;
  for (const auto &key : {"selectedAudioFile", "selectedSubtitleFile"})
    if (data.contains(key))
      data[key] = relative(data.value(key).toString(), base);
  if (entry.duration >= 0)
    data["duration"] = entry.duration;
  for (const auto &key : {"audioFiles", "subtitleFiles"}) {
    QStringList paths;
    for (const auto &source : data.value(key).toStringList())
      paths.append(relative(source, base));
    if (!paths.isEmpty())
      data[key] = paths;
  }
  return QJsonDocument(QJsonObject::fromVariantMap(data))
      .toJson(QJsonDocument::Compact)
      .toBase64();
}
bool decodeMetadata(PlaylistEntry &entry, const QString &value,
                    const QDir &base, QString *error) {
  if (value.isEmpty())
    return true;
  QJsonParseError parse;
  const auto document =
      QJsonDocument::fromJson(QByteArray::fromBase64(value.toLatin1()), &parse);
  if (parse.error != QJsonParseError::NoError || !document.isObject()) {
    *error = uiTr("Повреждены дополнительные данные записи");
    return false;
  }
  const auto data = document.object().toVariantMap();
  for (const auto &key : {"selectedAudioFile", "selectedSubtitleFile"}) {
    if (!data.contains(key))
      continue;
    const auto path = resolve(data.value(key).toString(), base, error);
    if (path.isEmpty())
      return false;
    entry.metadata[key] = path;
  }
  if (data.contains("duration")) {
    bool ok;
    const double duration = data.value("duration").toDouble(&ok);
    if (!ok || !std::isfinite(duration) || duration < 0) {
      *error = uiTr("Неверная длительность");
      return false;
    }
    entry.duration = duration;
  }
  for (const auto &key : {"aid", "sid"})
    if (data.contains(key)) {
      const QString selection = data.value(key).toString();
      if (!QRegularExpression("^(no|auto|[1-9][0-9]{0,5})$")
               .match(selection)
               .hasMatch()) {
        *error = uiTr("Неверный номер дорожки");
        return false;
      }
      entry.metadata[key] = selection;
    }
  for (const auto &key : {"audioFiles", "subtitleFiles"}) {
    QStringList paths;
    for (const auto &v : data.value(key).toList()) {
      const auto path = resolve(v.toString(), base, error);
      if (path.isEmpty())
        return false;
      paths.append(path);
    }
    if (!paths.isEmpty())
      entry.metadata[key] = paths;
  }
  QVariantList chapters;
  double previous = -1;
  for (const auto &v : data.value("chapters").toList()) {
    const auto chapter = v.toMap();
    bool ok;
    const double seconds = chapter.value("time").toDouble(&ok);
    if (!ok || !std::isfinite(seconds) || seconds < 0 || seconds <= previous) {
      *error = uiTr("Неверные времена глав");
      return false;
    }
    previous = seconds;
    chapters.append(QVariantMap{{"time", seconds},
                                {"title", chapter.value("title").toString()}});
  }
  if (!chapters.isEmpty())
    entry.metadata["chapters"] = chapters;
  return true;
}
double seconds(const QString &text) {
  const auto fields = text.split(':');
  double total = 0;
  for (const auto &field : fields) {
    bool ok;
    const double number = field.toDouble(&ok);
    if (!ok || number < 0 || !std::isfinite(number))
      return -1;
    total = total * 60 + number;
  }
  return total;
}
} // namespace
bool PlaylistFile::isPlaylist(const QString &path) {
  return !MediaSource::isUrl(path) &&
         QStringList{"m3u", "m3u8", "mpcpl", "pls", "asx", "cue"}.contains(
             QFileInfo(path).suffix().toLower());
}
bool PlaylistFile::readEntries(const QString &path,
                               QList<PlaylistEntry> *entries, QString *error,
                               bool skipInvalid) {
  QString localError;
  if (!error)
    error = &localError;
  error->clear();
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) {
    *error = input.errorString();
    return false;
  }
  if (input.size() > 4 * 1024 * 1024) {
    *error = uiTr("Плейлист слишком большой (не более 4 МБ)");
    return false;
  }
  const auto bytes = input.readAll();
  const QString extension = QFileInfo(path).suffix().toLower();
  if (extension == "m3u8" &&
      (bytes.startsWith("\xff\xfe") || bytes.startsWith("\xfe\xff"))) {
    *error = uiTr("M3U8 должен быть в кодировке UTF-8");
    return false;
  }
  QStringDecoder decoder(bytes.startsWith("\xff\xfe") ? QStringDecoder::Utf16LE
                         : bytes.startsWith("\xfe\xff")
                             ? QStringDecoder::Utf16BE
                             : QStringDecoder::Utf8);
  QString text = decoder(bytes);
  if (decoder.hasError()) {
    if (extension == "m3u8") {
      *error = uiTr("M3U8 должен быть в кодировке UTF-8");
      return false;
    }
    QStringDecoder legacy("Windows-1251");
    if (!legacy.isValid()) {
      *error = uiTr("Недоступна кодировка Windows-1251");
      return false;
    }
    text = legacy(bytes);
    if (legacy.hasError()) {
      *error = uiTr("Неизвестная кодировка плейлиста");
      return false;
    }
  }
  if (text.startsWith(QChar(0xfeff)))
    text.remove(0, 1);
  const QDir base(QFileInfo(path).absolutePath());
  QList<PlaylistEntry> result;
  const auto add = [&](PlaylistEntry entry) {
    entry.source = resolve(entry.source, base, error);
    if (entry.source.isEmpty())
      return skipInvalid;
    if (result.size() >= 10000) {
      *error = uiTr("В плейлисте более 10000 записей");
      return false;
    }
    if (!std::isfinite(entry.duration) || entry.duration < 0)
      entry.duration = -1;
    result.append(entry);
    return true;
  };
  const auto lines = text.split('\n');
  if (extension == "cue") {
    QString album, performer, file, title, trackPerformer;
    int track = 0;
    bool audio = false, indexed = false;
    QMap<QString, int> positions;
    for (const auto &raw : lines) {
      const QString line = raw.trimmed();
      const auto match =
          QRegularExpression(
              "^(FILE|TITLE|PERFORMER)\\s+(?:\"([^\"]*)\"|(\\S+))",
              QRegularExpression::CaseInsensitiveOption)
              .match(line);
      if (match.hasMatch()) {
        const auto key = match.captured(1).toUpper();
        const auto value =
            match.captured(2).isNull() ? match.captured(3) : match.captured(2);
        if (key == "FILE") {
          if (audio && !indexed) {
            *error = uiTr("В CUE отсутствует INDEX 01");
            return false;
          }
          file = value;
          track = 0;
          audio = false;
        } else if (key == "TITLE") {
          if (track)
            title = value;
          else
            album = value;
        } else if (track)
          trackPerformer = value;
        else
          performer = value;
        continue;
      }
      auto m = QRegularExpression("^TRACK\\s+([0-9]+)\\s+(\\S+)$",
                                  QRegularExpression::CaseInsensitiveOption)
                   .match(line);
      if (m.hasMatch()) {
        if (audio && !indexed) {
          *error = uiTr("В CUE отсутствует INDEX 01");
          return false;
        }
        track = m.captured(1).toInt();
        audio = m.captured(2).compare("AUDIO", Qt::CaseInsensitive) == 0;
        indexed = false;
        title.clear();
        trackPerformer.clear();
        continue;
      }
      m = QRegularExpression("^INDEX\\s+01\\s+([0-9]+):([0-9]{2}):([0-9]{2})$",
                             QRegularExpression::CaseInsensitiveOption)
              .match(line);
      if (m.hasMatch() && audio) {
        if (file.isEmpty() || track <= 0 || indexed ||
            m.captured(2).toInt() >= 60 || m.captured(3).toInt() >= 75) {
          *error = uiTr("Неверный INDEX 01 в CUE");
          return false;
        }
        const auto source = resolve(file, base, error);
        if (source.isEmpty() || MediaSource::isUrl(source)) {
          *error = uiTr("CUE требует локальный медиафайл");
          return false;
        }
        if (!positions.contains(source)) {
          positions[source] = result.size();
          PlaylistEntry entry;
          entry.source = source;
          entry.title = album.isEmpty()       ? MediaSource::displayName(source)
                        : performer.isEmpty() ? album
                                              : performer + " — " + album;
          if (!add(entry))
            return false;
        }
        auto &entry = result[positions[source]];
        auto chapters = entry.metadata.value("chapters").toList();
        const double time = m.captured(1).toDouble() * 60 +
                            m.captured(2).toInt() + m.captured(3).toInt() / 75.;
        if (!chapters.isEmpty() &&
            time <= chapters.last().toMap().value("time").toDouble()) {
          *error = uiTr("Главы CUE должны идти по возрастанию");
          return false;
        }
        QString label =
            title.isEmpty() ? QString(uiTr("Трек %1")).arg(track) : title;
        if (!trackPerformer.isEmpty())
          label = trackPerformer + " — " + label;
        if (chapters.size() >= 10000) {
          *error = uiTr("В CUE более 10000 глав");
          return false;
        }
        chapters.append(QVariantMap{{"time", time}, {"title", label}});
        entry.metadata["chapters"] = chapters;
        indexed = true;
      } else if (audio && line.startsWith("INDEX 01", Qt::CaseInsensitive)) {
        *error = uiTr("Неверное время CUE");
        return false;
      }
    }
    if (audio && !indexed) {
      *error = uiTr("В CUE отсутствует INDEX 01");
      return false;
    }
  } else if (extension == "asx") {
    QXmlStreamReader xml(text);
    bool root = false, inEntry = false;
    PlaylistEntry entry;
    while (!xml.atEnd()) {
      xml.readNext();
      if (xml.tokenType() == QXmlStreamReader::DTD) {
        *error = uiTr("DTD в ASX не поддерживается");
        return false;
      }
      const auto name = xml.name().toString().toLower();
      if (xml.isStartElement()) {
        if (!root) {
          if (name != "asx") {
            *error = uiTr("Нет корневого ASX");
            return false;
          }
          root = true;
        }
        if (name == "entry") {
          if (inEntry) {
            *error = uiTr("Вложенный ENTRY");
            return false;
          }
          inEntry = true;
          entry = {};
        } else if (inEntry && name == "title")
          entry.title = xml.readElementText();
        else if (inEntry &&
                 (name == "ref" || name == "param" || name == "duration")) {
          QMap<QString, QString> attrs;
          for (const auto &attr : xml.attributes())
            attrs[attr.name().toString().toLower()] = attr.value().toString();
          if (name == "ref" && entry.source.isEmpty()) {
            entry.source = attrs.value("href");
            const QUrl reference(entry.source);
            if (reference.scheme().isEmpty())
              entry.source = QUrl::fromPercentEncoding(entry.source.toUtf8());
          }
          if (name == "duration")
            entry.duration = seconds(attrs.value("value"));
          if (name == "param" &&
              attrs.value("name").compare("FoxicMP", Qt::CaseInsensitive) ==
                  0 &&
              !decodeMetadata(entry, attrs.value("value"), base, error))
            return false;
        }
      } else if (xml.isEndElement() && name == "entry") {
        if (!add(entry))
          return false;
        inEntry = false;
      }
    }
    if (xml.hasError() || !root || inEntry) {
      *error = uiTr("Повреждённый ASX: ") + xml.errorString();
      return false;
    }
  } else if (extension == "pls") {
    QMap<int, PlaylistEntry> items;
    QMap<int, QString> meta;
    bool header = false;
    int declared = -1;
    for (const auto &raw : lines) {
      const auto line = raw.trimmed();
      if (line.compare("[playlist]", Qt::CaseInsensitive) == 0) {
        header = true;
        continue;
      }
      if (line.isEmpty() || line.startsWith(';') || line.startsWith('#'))
        continue;
      auto m =
          QRegularExpression("^(File|Title|Length|X-FoxicMP)([0-9]+)=(.*)$",
                             QRegularExpression::CaseInsensitiveOption)
              .match(line);
      if (m.hasMatch()) {
        const int index = m.captured(2).toInt();
        if (index <= 0 || index > 10000) {
          *error = uiTr("Неверный индекс PLS");
          return false;
        }
        const auto key = m.captured(1).toLower(), value = m.captured(3);
        if (key == "file")
          items[index].source = value;
        else if (key == "title")
          items[index].title = value;
        else if (key == "length")
          items[index].duration = seconds(value);
        else
          meta[index] = value;
      } else if (line.startsWith("NumberOfEntries=", Qt::CaseInsensitive)) {
        bool ok;
        declared = line.mid(16).toInt(&ok);
        if (!ok || declared < 0) {
          *error = uiTr("Неверное NumberOfEntries");
          return false;
        }
      }
    }
    if (!header || (declared >= 0 && declared != items.size())) {
      *error = uiTr("Повреждённый PLS");
      return false;
    }
    for (auto it = items.begin(); it != items.end(); ++it)
      if (!decodeMetadata(it.value(), meta.value(it.key()), base, error) ||
          !add(it.value()))
        return false;
  } else if (extension == "mpcpl" || text.startsWith("MPCPLAYLIST")) {
    if (lines.first().trimmed() != "MPCPLAYLIST") {
      *error = uiTr("Нет заголовка MPCPLAYLIST");
      return false;
    }
    QMap<int, PlaylistEntry> items;
    QMap<int, int> types;
    for (const auto &raw : lines.mid(1)) {
      QString line = raw;
      if (line.endsWith('\r'))
        line.chop(1);
      const int first = line.indexOf(','),
                second = line.indexOf(',', first + 1);
      if (first < 0 || second < 0)
        continue;
      bool ok;
      const int index = line.left(first).toInt(&ok);
      if (!ok || index <= 0 || index > 10000) {
        *error = uiTr("Неверный индекс MPCPL");
        return false;
      }
      const auto key = line.mid(first + 1, second - first - 1),
                 value = line.mid(second + 1);
      auto &entry = items[index];
      if (key == "type") {
        bool valid;
        const int type = value.toInt(&valid);
        if (!valid) {
          *error = uiTr("Неверный тип MPCPL");
          return false;
        }
        types[index] = type;
      } else if (key == "filename") {
        if (entry.source.isEmpty())
          entry.source = value;
        else {
          auto paths = entry.metadata.value("audioFiles").toStringList();
          const auto source = resolve(value, base, error);
          if (source.isEmpty())
            return false;
          paths.append(source);
          entry.metadata["audioFiles"] = paths;
        }
      } else if (key == "subtitle") {
        auto paths = entry.metadata.value("subtitleFiles").toStringList();
        const auto source = resolve(value, base, error);
        if (source.isEmpty())
          return false;
        paths.append(source);
        entry.metadata["subtitleFiles"] = paths;
      } else if (key == "label")
        entry.title = value;
      else if (key == "time")
        entry.duration = seconds(value);
      else if (key == "foxicmp") {
        if (!decodeMetadata(entry, value, base, error))
          return false;
      }
    }
    for (auto it = items.begin(); it != items.end(); ++it) {
      if (types.value(it.key(), 0) != 0) {
        if (skipInvalid)
          continue;
        *error =
            uiTr("Устройства DirectShow из MPCPL не поддерживаются в Linux");
        return false;
      }
      if (!add(it.value()))
        return false;
    }
  } else {
    PlaylistEntry entry;
    for (auto line : lines) {
      if (line.endsWith('\r'))
        line.chop(1);
      if (line.trimmed().isEmpty())
        continue;
      if (line.startsWith("#EXTINF:", Qt::CaseInsensitive)) {
        const int comma = line.indexOf(',');
        if (comma >= 0) {
          entry.duration = seconds(line.mid(8, comma - 8));
          entry.title = line.mid(comma + 1);
        }
      } else if (line.startsWith("#FOXICMP:", Qt::CaseInsensitive)) {
        if (!decodeMetadata(entry, line.mid(9), base, error))
          return false;
      } else if (!line.startsWith('#')) {
        entry.source = line;
        if (!add(entry))
          return false;
        entry = {};
      }
    }
  }
  if (result.isEmpty()) {
    *error = uiTr("Плейлист не содержит доступных записей");
    return false;
  }
  *entries = result;
  error->clear();
  return true;
}
bool PlaylistFile::writeEntries(const QString &path,
                                const QList<PlaylistEntry> &entries,
                                QString *error) {
  QString localError;
  if (!error)
    error = &localError;
  if (entries.isEmpty()) {
    *error = uiTr("Плейлист пуст");
    return false;
  }
  const auto extension = QFileInfo(path).suffix().toLower();
  if (!QStringList{"m3u8", "m3u", "mpcpl", "pls", "asx"}.contains(extension)) {
    *error = uiTr("Сохранение поддерживает M3U8/M3U, MPCPL, PLS и ASX");
    return false;
  }
  const QDir base(QFileInfo(path).absolutePath());
  QByteArray output;
  QXmlStreamWriter xml(&output);
  if (extension == "asx") {
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("ASX");
    xml.writeAttribute("version", "3.0");
  } else if (extension == "mpcpl")
    output = "MPCPLAYLIST\n";
  else if (extension == "pls")
    output = "[playlist]\n";
  else
    output = "#EXTM3U\n";
  int index = 0;
  for (const auto &entry : entries) {
    const auto source = relative(entry.source, base);
    if (source.contains('\n') || source.contains('\r') ||
        entry.title.contains('\n') || entry.title.contains('\r')) {
      *error = uiTr("Перевод строки в пути или названии не поддерживается");
      return false;
    }
    const QByteArray number = QByteArray::number(++index),
                     name = entry.title.toUtf8(), file = source.toUtf8();
    const auto meta = metadata(entry, base);
    const QByteArray length =
        std::isfinite(entry.duration) && entry.duration >= 0
            ? QByteArray::number(entry.duration, 'f', 3)
            : QByteArray("-1");
    if (extension == "asx") {
      xml.writeStartElement("ENTRY");
      xml.writeTextElement("TITLE", entry.title);
      xml.writeEmptyElement("REF");
      QUrl reference;
      reference.setPath(source);
      xml.writeAttribute("HREF", MediaSource::isUrl(entry.source)
                                     ? entry.source
                                     : reference.toString(QUrl::FullyEncoded));
      xml.writeEmptyElement("PARAM");
      xml.writeAttribute("NAME", "FoxicMP");
      xml.writeAttribute("VALUE", QString::fromLatin1(meta));
      xml.writeEndElement();
    } else if (extension == "mpcpl") {
      output += number + ",type,0\n" + number + ",filename," + file + '\n';
      if (!name.isEmpty())
        output += number + ",label," + name + '\n';
      if (entry.duration >= 0) {
        const qint64 whole = qint64(entry.duration);
        const auto clock = QString("%1:%2:%3")
                               .arg(whole / 3600, 2, 10, QChar('0'))
                               .arg(whole / 60 % 60, 2, 10, QChar('0'))
                               .arg(whole % 60, 2, 10, QChar('0'));
        output += number + ",time," + clock.toUtf8() + '\n';
      }
      for (const auto &source :
           entry.metadata.value("audioFiles").toStringList())
        output +=
            number + ",filename," + relative(source, base).toUtf8() + '\n';
      for (const auto &source :
           entry.metadata.value("subtitleFiles").toStringList())
        output +=
            number + ",subtitle," + relative(source, base).toUtf8() + '\n';
      output += number + ",foxicmp," + meta + '\n';
    } else if (extension == "pls") {
      output += "File" + number + '=' + file + "\nTitle" + number + '=' + name +
                "\nLength" + number + '=' + length + "\nX-FoxicMP" + number +
                '=' + meta + '\n';
    } else
      output += "#EXTINF:" + length + ',' + name + "\n#FOXICMP:" + meta + '\n' +
                file + '\n';
  }
  if (extension == "asx") {
    xml.writeEndElement();
    xml.writeEndDocument();
  }
  if (extension == "pls")
    output += "NumberOfEntries=" + QByteArray::number(entries.size()) +
              "\nVersion=2\n";
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(output) != output.size() ||
      !file.commit()) {
    *error = file.errorString();
    return false;
  }
  error->clear();
  return true;
}
bool PlaylistFile::read(const QString &path, QStringList *files,
                        QString *error) {
  QList<PlaylistEntry> entries;
  if (!readEntries(path, &entries, error))
    return false;
  QStringList result;
  for (const auto &entry : entries)
    result.append(entry.source);
  *files = result;
  return true;
}
bool PlaylistFile::write(const QString &path, const QStringList &files,
                         QString *error) {
  QList<PlaylistEntry> entries;
  for (const auto &source : files) {
    PlaylistEntry entry;
    entry.source = source;
    entries.append(entry);
  }
  return writeEntries(path, entries, error);
}
