#include "subtitledocument.h"
#include "i18n.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>
#include <cmath>
#include <iconv.h>

namespace {
constexpr qint64 maxTime = 7LL * 24 * 3600 * 1000;
bool fail(QString *error, const QString &message) {
  if (error)
    *error = message;
  return false;
}
bool convert(const QByteArray &input, const QByteArray &from,
             const QByteArray &to, QByteArray *result) {
  iconv_t codec = iconv_open(to.constData(), from.constData());
  if (codec == reinterpret_cast<iconv_t>(-1))
    return false;
  QByteArray output(input.size() * 4 + 32, '\0');
  char *read = const_cast<char *>(input.constData());
  char *write = output.data();
  size_t left = input.size(), free = output.size();
  const bool ok =
      iconv(codec, &read, &left, &write, &free) != size_t(-1) && !left;
  iconv_close(codec);
  if (ok) {
    output.resize(output.size() - free);
    *result = output;
  }
  return ok;
}
QString plainText(QString text, bool fromAss) {
  if (fromAss) {
    text.remove(QRegularExpression("\\{[^}]*\\}"));
    text.replace("\\N", "\n").replace("\\n", "\n").replace("\\h", " ");
  }
  return text;
}
} // namespace
qint64 SubtitleDocument::parseTime(const QString &text, bool *ok) {
  static const QRegularExpression pattern(
      "^\\s*(?:(\\d{1,3}):)?(\\d{1,2}):(\\d{2})[.,](\\d{1,3})\\s*$");
  const auto match = pattern.match(text);
  bool valid = match.hasMatch();
  qint64 value = -1;
  if (valid) {
    const int hours = match.captured(1).toInt();
    const int minutes = match.captured(2).toInt();
    const int seconds = match.captured(3).toInt();
    const int fraction = match.captured(4).leftJustified(3, '0').toInt();
    value = ((hours * 60LL + minutes) * 60 + seconds) * 1000 + fraction;
    valid = minutes < 60 && seconds < 60 && value <= maxTime;
  }
  if (ok)
    *ok = valid;
  return valid ? value : -1;
}
QString SubtitleDocument::timeText(qint64 ms, bool ass) {
  if (ass)
    ms = (ms / 10) * 10;
  return QString("%1:%2:%3%4%5")
      .arg(ms / 3600000, ass ? 1 : 2, 10, QChar('0'))
      .arg(ms / 60000 % 60, 2, 10, QChar('0'))
      .arg(ms / 1000 % 60, 2, 10, QChar('0'))
      .arg(ass ? "." : ",")
      .arg(ass ? ms / 10 % 100 : ms % 1000, ass ? 2 : 3, 10, QChar('0'));
}
bool SubtitleDocument::load(const QString &path, const QString &encoding,
                            QString *error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return fail(error, file.errorString());
  if (file.size() > 16 * 1024 * 1024)
    return fail(error, uiTr("Файл субтитров больше 16 МиБ"));
  auto bytes = file.readAll();
  QByteArray codec = encoding.toLatin1();
  if (codec == "auto") {
    if (bytes.startsWith("\xff\xfe")) {
      codec = "UTF-16LE";
      bytes.remove(0, 2);
    } else if (bytes.startsWith("\xfe\xff")) {
      codec = "UTF-16BE";
      bytes.remove(0, 2);
    } else {
      QStringDecoder utf8(QStringDecoder::Utf8);
      const QString decoded = utf8.decode(bytes);
      Q_UNUSED(decoded);
      codec = utf8.hasError() ? "CP1251" : "UTF-8";
    }
  }
  if (bytes.startsWith("\xef\xbb\xbf"))
    bytes.remove(0, 3);
  QByteArray utf8;
  if (!convert(bytes, codec, "UTF-8", &utf8))
    return fail(error, uiTr("Не удалось декодировать выбранную кодировку"));
  QString text = QString::fromUtf8(utf8);
  if (text.startsWith(QChar(0xfeff)))
    text.remove(0, 1);
  auto suffix = QFileInfo(path).suffix().toLower();
  if (suffix == "ssa")
    suffix = "ass";
  return parse(text, suffix, error);
}
bool SubtitleDocument::parse(const QString &input, const QString &kind,
                             QString *error) {
  SubtitleDocument next;
  next.format = kind;
  QString text = input;
  text.replace("\r\n", "\n").replace('\r', '\n');
  const auto lines = text.split('\n');
  if (kind == "ass") {
    bool events = false;
    for (const auto &line : lines) {
      const auto clean = line.trimmed();
      if (clean.startsWith('['))
        events = clean.compare("[Events]", Qt::CaseInsensitive) == 0;
      if (events && clean.startsWith("Format:", Qt::CaseInsensitive)) {
        auto fields = clean.mid(7).toLower().split(',');
        for (auto &field : fields)
          field = field.trimmed();
        if (!next.assFormat.isEmpty() && next.assFormat != fields)
          return fail(error, uiTr("Неподдерживаемый Format событий ASS"));
        next.assFormat = fields;
      }
      if (events && clean.startsWith("Dialogue:", Qt::CaseInsensitive)) {
        if (next.assFormat.isEmpty())
          return fail(error, uiTr("В ASS отсутствует Format событий"));
        const int start = next.assFormat.indexOf("start"),
                  end = next.assFormat.indexOf("end");
        const int body = next.assFormat.indexOf("text");
        auto fields = line.mid(line.indexOf(':') + 1).trimmed().split(',');
        if (start < 0 || end < 0 || body != next.assFormat.size() - 1 ||
            fields.size() < next.assFormat.size())
          return fail(error, uiTr("Неподдерживаемый Format событий ASS"));
        while (fields.size() > next.assFormat.size()) {
          fields[body] += ',' + fields.takeAt(body + 1);
        }
        SubtitleCue cue;
        cue.start = parseTime(fields[start]);
        cue.end = parseTime(fields[end]);
        cue.text = fields[body];
        cue.assFields = fields;
        next.assLines.append({{}, int(next.cues.size())});
        next.cues.append(cue);
      } else
        next.assLines.append({line, -1});
    }
  } else if (kind == "srt" || kind == "vtt") {
    if (kind == "vtt" && !text.startsWith("WEBVTT"))
      return fail(error, uiTr("Не найден заголовок WEBVTT"));
    const QRegularExpression timing("^\\s*(\\S+)\\s+-->\\s+(\\S+)(.*)$");
    int i = 0;
    if (kind == "vtt") {
      QStringList header;
      while (i < lines.size() && !lines[i].trimmed().isEmpty())
        header << lines[i++];
      next.vttHeader = header.join('\n');
    }
    while (i < lines.size()) {
      while (i < lines.size() && lines[i].trimmed().isEmpty())
        ++i;
      if (i >= lines.size())
        break;
      const int begin = i;
      while (i < lines.size() && !lines[i].trimmed().isEmpty())
        ++i;
      const auto block = lines.mid(begin, i - begin);
      if (kind == "vtt" && (block[0].startsWith("NOTE") ||
                            block[0] == "STYLE" || block[0] == "REGION"))
        return fail(
            error,
            uiTr("VTT с NOTE/STYLE/REGION редактируется во внешнем редакторе"));
      int timestamp = block[0].contains("-->") ? 0 : 1;
      if (timestamp >= block.size())
        return fail(error, uiTr("Повреждён блок субтитров"));
      const auto match = timing.match(block[timestamp]);
      if (!match.hasMatch())
        return fail(error, uiTr("Неверное время реплики"));
      SubtitleCue cue;
      cue.start = parseTime(match.captured(1));
      cue.end = parseTime(match.captured(2));
      cue.settings = match.captured(3).trimmed();
      if (timestamp && kind == "vtt")
        cue.identifier = block[0];
      cue.text = block.mid(timestamp + 1).join('\n');
      next.cues.append(cue);
      if (next.cues.size() > 50000)
        return fail(error, uiTr("Больше 50000 реплик"));
    }
  } else
    return fail(error,
                uiTr("Редактор поддерживает SRT, ASS/SSA и простой WebVTT"));
  if (next.cues.isEmpty())
    return fail(error, uiTr("В файле нет текстовых реплик"));
  if (!next.valid(error))
    return false;
  *this = std::move(next);
  return true;
}
bool SubtitleDocument::valid(QString *error) const {
  if (cues.size() > 50000)
    return fail(error, uiTr("Больше 50000 реплик"));
  for (int i = 0; i < cues.size(); ++i) {
    const auto &cue = cues[i];
    if (cue.start < 0 || cue.end <= cue.start || cue.end > maxTime ||
        cue.text.contains(QChar(0)))
      return fail(error, uiTr("Реплика %1: конец должен быть позже начала, "
                              "время — от 0 до 7 суток")
                             .arg(i + 1));
  }
  return true;
}
bool SubtitleDocument::retime(double scale, double offset, QString *error) {
  if (!valid(error))
    return false;
  if (!std::isfinite(scale) || !std::isfinite(offset) || scale <= 0 ||
      scale > 10 || std::abs(offset) > maxTime)
    return fail(error, uiTr("Недопустимое преобразование времени"));
  auto copy = *this;
  for (auto &cue : copy.cues) {
    cue.start = std::llround(cue.start * scale + offset);
    cue.end = std::llround(cue.end * scale + offset);
  }
  if (!copy.valid(error))
    return false;
  *this = std::move(copy);
  return true;
}
bool SubtitleDocument::synchronize(qint64 s1, qint64 t1, qint64 s2, qint64 t2,
                                   QString *error) {
  if (s1 < 0 || t1 < 0 || s2 > maxTime || t2 > maxTime)
    return fail(error, uiTr("Точки должны находиться в пределах 7 суток"));
  if (s2 <= s1 || t2 <= t1)
    return fail(error, uiTr("Вторая точка должна быть позже первой"));
  const double scale = double(t2 - t1) / double(s2 - s1);
  return retime(scale, t1 - s1 * scale, error);
}
QString SubtitleDocument::serialize(const QString &kind, QString *error) const {
  if (!valid(error))
    return {};
  QStringList output;
  if (kind == "ass" && format == "ass") {
    const int start = assFormat.indexOf("start"),
              end = assFormat.indexOf("end"), body = assFormat.indexOf("text");
    for (const auto &line : assLines) {
      if (line.cue < 0)
        output << line.text;
      else {
        const auto &cue = cues[line.cue];
        auto fields = cue.assFields;
        fields[start] = timeText(cue.start, true);
        fields[end] = timeText(
            std::max((cue.end + 9) / 10 * 10, cue.start / 10 * 10 + 10), true);
        fields[body] = cue.text;
        fields[body].replace('\n', "\\N");
        output << "Dialogue: " + fields.join(',');
      }
    }
  } else if (kind == "srt" || kind == "vtt") {
    if (kind == "vtt")
      output << (vttHeader.isEmpty() ? "WEBVTT" : vttHeader) << "";
    for (int i = 0; i < cues.size(); ++i) {
      const auto &cue = cues[i];
      if (kind == "srt")
        output << QString::number(i + 1);
      else if (!cue.identifier.isEmpty())
        output << cue.identifier;
      QString start = timeText(cue.start), end = timeText(cue.end);
      if (kind == "vtt") {
        start.replace(',', '.');
        end.replace(',', '.');
      }
      output << start + " --> " + end +
                    (kind == "vtt" && !cue.settings.isEmpty()
                         ? " " + cue.settings
                         : "");
      output << plainText(cue.text, format == "ass") << "";
    }
  } else {
    fail(error,
         uiTr("Сохраните ASS в ASS либо экспортируйте текст в SRT/WebVTT"));
    return {};
  }
  return output.join('\n') + '\n';
}
bool SubtitleDocument::save(const QString &path, const QString &kind,
                            const QString &encoding, QString *error) const {
  const auto text = serialize(kind, error);
  if (text.isEmpty())
    return false;
  QByteArray bytes;
  if (!convert(text.toUtf8(), "UTF-8", encoding.toLatin1(), &bytes))
    return fail(error, uiTr("Текст не представим в выбранной кодировке"));
  if (encoding == "UTF-16LE")
    bytes.prepend("\xff\xfe");
  if (encoding == "UTF-16BE")
    bytes.prepend("\xfe\xff");
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
      !file.commit())
    return fail(error, file.errorString());
  return true;
}
