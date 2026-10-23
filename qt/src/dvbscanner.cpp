#include "dvbscanner.h"
#include "devicesource.h"
#include "i18n.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>
#include <QTimer>
DvbScanner::DvbScanner(const QString &profileDirectory, QObject *parent)
    : QObject(parent), directory(profileDirectory) {
  auto output = [this] {
    const auto raw =
        (process.readAllStandardOutput() + process.readAllStandardError())
            .right(8192);
    if (!raw.isEmpty())
      emit progress(QString::fromUtf8(raw).trimmed().right(2048));
  };
  connect(&process, &QProcess::readyReadStandardOutput, this, output);
  connect(&process, &QProcess::readyReadStandardError, this, output);
  connect(&process, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
              emit finished(false, process.errorString(), {});
          });
  connect(
      &process, &QProcess::finished, this,
      [this](int exitCode, QProcess::ExitStatus status) {
        if (cancelled || exitCode || status != QProcess::NormalExit) {
          emit finished(false,
                        cancelled ? uiTr("Поиск каналов отменён")
                                  : uiTr("Поиск каналов завершился с ошибкой"),
                        {});
          return;
        }
        QFile result(temporary.filePath("channels.conf"));
        if (!result.open(QIODevice::ReadOnly) || result.size() > 1024 * 1024) {
          emit finished(false,
                        uiTr("Сканер не создал допустимый список каналов"), {});
          return;
        }
        const auto data = result.readAll();
        QString error;
        const auto names = DeviceSource::channels(data, &error);
        if (names.isEmpty()) {
          emit finished(
              false, error.isEmpty() ? uiTr("Каналы не найдены") : error, {});
          return;
        }
        const QString name = "FoxicMPQt.channels.conf";
        QSaveFile saved(QDir(directory).filePath(name));
        if (!saved.open(QIODevice::WriteOnly) ||
            saved.write(data) != data.size() || !saved.commit()) {
          emit finished(
              false, uiTr("Не удалось сохранить каналы в папке программы"), {});
          return;
        }
        emit finished(
            true, QString(uiTr("Найдено каналов: %1")).arg(names.size()), name);
      });
}
DvbScanner::~DvbScanner() {
  process.disconnect(this);
  if (running()) {
    process.kill();
    process.waitForFinished(1000);
  }
}
bool DvbScanner::running() const {
  return process.state() != QProcess::NotRunning;
}
bool DvbScanner::start(const QString &executable, const QString &initialFile,
                       int card, QString *error) {
  auto fail = [&](const QString &message) {
    *error = message;
    return false;
  };
  if (running() || card < 0 || card > 15 || !temporary.isValid())
    return fail(uiTr("Невозможно начать поиск каналов"));
  const QFileInfo tool(executable);
  if (!tool.isFile() || !tool.isExecutable())
    return fail(uiTr("Сканер недоступен. Установите пакет dvb-tools"));
  QFile input(initialFile);
  if (!input.open(QIODevice::ReadOnly) || input.size() <= 0 ||
      input.size() > 1024 * 1024)
    return fail(uiTr("Файл начальных частот недоступен или больше 1 МБ"));
  const auto data = input.readAll();
  QStringDecoder decoder(QStringDecoder::Utf8);
  const QString text = decoder(data);
  if (decoder.hasError() || text.contains(QChar(0)))
    return fail(uiTr("Файл начальных частот должен быть UTF-8"));
  const QString tuning = QDir(directory).filePath("FoxicMPQt.dvb-tuning.conf");
  QSaveFile stored(tuning);
  if (!stored.open(QIODevice::WriteOnly) || stored.write(data) != data.size() ||
      !stored.commit())
    return fail(
        uiTr("Не удалось сохранить начальные частоты в папке программы"));
  QFile::remove(temporary.filePath("channels.conf"));
  cancelled = false;
  process.start(tool.absoluteFilePath(),
                {"-a", QString::number(card), "-f", "0", "-I", "DVBV5", "-O",
                 "VDR", "-o", temporary.filePath("channels.conf"), tuning});
  process.closeWriteChannel();
  return true;
}
void DvbScanner::cancel() {
  if (!running())
    return;
  cancelled = true;
  process.terminate();
  QTimer::singleShot(1500, this, [this] {
    if (cancelled && running())
      process.kill();
  });
}
