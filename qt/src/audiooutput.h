#pragma once
#include <QObject>
#include <QProcess>
#include <QString>
class Player;
class QSettings;
class AudioOutput : public QObject {
  Q_OBJECT
public:
  explicit AudioOutput(Player *);
  ~AudioOutput() override;
  static void apply(Player *, QSettings &);
  static QString primaryDevice(Player *, const QString &);

private:
  void configure(QSettings &);
  void start();
  void fallback();
  void status(const QString &);
  Player *player;
  QProcess process;
  QString primary, secondary, route;
  QByteArray lines;
  quint64 generation = 0;
  bool enabled = false, changing = false, closing = false;
};
