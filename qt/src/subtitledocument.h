#pragma once
#include <QStringList>
#include <QVector>

struct SubtitleCue {
  qint64 start = 0, end = 0;
  QString text, settings, identifier;
  QStringList assFields;
};
// Editable text subtitles. ASS keeps non-dialogue lines and event fields.
class SubtitleDocument {
public:
  QVector<SubtitleCue> cues;
  QString format = "srt";
  static qint64 parseTime(const QString &text, bool *ok = nullptr);
  static QString timeText(qint64 milliseconds, bool ass = false);
  bool load(const QString &path, const QString &encoding, QString *error);
  bool parse(const QString &text, const QString &format, QString *error);
  QString serialize(const QString &outputFormat, QString *error) const;
  bool save(const QString &path, const QString &outputFormat,
            const QString &encoding, QString *error) const;
  bool retime(double scale, double offsetMs, QString *error);
  bool synchronize(qint64 source1, qint64 target1, qint64 source2,
                   qint64 target2, QString *error);
  bool valid(QString *error) const;

private:
  struct AssLine {
    QString text;
    int cue = -1;
  };
  QVector<AssLine> assLines;
  QStringList assFormat;
  QString vttHeader;
};
