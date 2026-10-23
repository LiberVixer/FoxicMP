#pragma once
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
class DvbScanner : public QObject {
  Q_OBJECT
public:
  explicit DvbScanner(const QString &profileDirectory,
                      QObject *parent = nullptr);
  ~DvbScanner() override;
  bool start(const QString &executable, const QString &initialFile, int card,
             QString *error);
  void cancel();
  bool running() const;
signals:
  void progress(const QString &text);
  void finished(bool success, const QString &message,
                const QString &configName);

private:
  QProcess process;
  QTemporaryDir temporary;
  QString directory;
  bool cancelled = false;
};
