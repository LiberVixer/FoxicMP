#include "devicesource.h"
#include "i18n.h"
#include "mediaexports.h"
#include "mediasource.h"
#include "player.h"
#include "window.h"
#include <QImageWriter>
#include <QSaveFile>
#include <QtWidgets>

namespace {
class ReportProcess : public QProcess {
public:
  using QProcess::QProcess;
  ~ReportProcess() override {
    if (state() != NotRunning) {
      kill();
      waitForFinished(1000);
    }
  }
};
QString format(QSettings &s) {
  const auto wanted = s.value("capture/format", "png").toString();
  return MediaExports::formats().contains(wanted) ? wanted : "png";
}
QString fileFilter() { return MediaExports::fileFilters(); }
QString selectedFilter(const QString &suffix) {
  for (const auto &f : fileFilter().split(";;"))
    if (f.contains("*." + suffix))
      return f;
  return "PNG (*.png)";
}
QString ensureSuffix(QString path, const QString &filter) {
  if (!path.isEmpty() && QFileInfo(path).suffix().isEmpty()) {
    const auto match = QRegularExpression("\\*\\.(\\w+)").match(filter);
    path += "." + (match.hasMatch() ? match.captured(1) : "png");
  }
  return path;
}
} // namespace
QString Window::captureName() const {
  QString base = DeviceSource::isDevice(current) ? DeviceSource::label(current)
                 : MediaSource::isUrl(current)
                     ? QUrl(current).fileName()
                     : QFileInfo(current).completeBaseName();
  base.replace(QRegularExpression("[/\\\\:*?\"<>|\\x00-\\x1f]"), "_");
  if (base.isEmpty())
    base = "FoxicMP";
  return base.left(100) + "-" +
         QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
}
void Window::finishCapture(const QImage &image, const QString &path,
                           bool clipboard) {
  bool ok = !image.isNull();
  QString error;
  if (ok && clipboard)
    QApplication::clipboard()->setImage(image);
  else if (ok)
    ok = MediaExports::writeImage(
        image, path, profile.settings.value("capture/quality", 90).toInt(),
        &error);
  showOsd(ok ? (clipboard
                    ? uiTr("Кадр скопирован в буфер обмена.")
                    : uiTr("Кадр сохранён: ") + QFileInfo(path).fileName())
             : uiTr("Не удалось получить или сохранить изображение. ") + error);
  if (!clipboard)
    emit frameSaved(path, ok);
  emit imageCaptured(ok);
}
bool Window::captureImage(const QString &path, bool clipboard,
                          bool displayArea) {
  if (stopped || player->get("video-params").toMap().isEmpty() ||
      (!clipboard && path.isEmpty()) || imageRequests.size() >= 4) {
    showOsd(uiTr("Нет видеокадра или очередь снимков занята."));
    return false;
  }
  if (!clipboard &&
      QFileInfo(path).canonicalFilePath() ==
          QFileInfo(current).canonicalFilePath() &&
      QFileInfo(current).exists()) {
    showOsd(uiTr("Нельзя заменить исходный медиафайл снимком."));
    return false;
  }
  if (displayArea) {
    finishCapture(player->grab().toImage(), path, clipboard);
    return true;
  }
  const auto id = ++screenshotId;
  imageRequests[id] = {path, clipboard};
  if (player->captureFrame(
          id, profile.settings.value("ui/screenshotSubtitles", true).toBool()))
    return true;
  imageRequests.remove(id);
  return false;
}
void Window::quickSnapshot() {
  const auto dir = profile.settings
                       .value("files/screenshotDirectory",
                              profile.directory + "/Screenshots")
                       .toString();
  if (!QDir().mkpath(dir)) {
    showOsd(uiTr("Не удалось создать папку снимков."));
    return;
  }
  QString path;
  do {
    path =
        QDir(dir).filePath(captureName() + "-" +
                           QUuid::createUuid().toString(QUuid::Id128).left(8) +
                           "." + format(profile.settings));
  } while (QFileInfo::exists(path));
  captureImage(path);
}
void Window::displaySnapshotDialog() {
  if (stopped)
    return;
  const auto dir = profile.settings
                       .value("files/screenshotDirectory",
                              profile.directory + "/Screenshots")
                       .toString();
  auto filter = selectedFilter(format(profile.settings));
  auto path = QFileDialog::getSaveFileName(
      this, uiTr("Сохранить область видео с OSD"),
      QDir(dir).filePath(captureName() + "." + format(profile.settings)),
      fileFilter(), &filter);
  path = ensureSuffix(path, filter);
  if (!path.isEmpty()) {
    profile.settings.setValue("files/screenshotDirectory",
                              QFileInfo(path).absolutePath());
    captureImage(path, false, true);
  }
}
void Window::captureSettingsDialog() {
  QDialog dialog(this);
  dialog.setObjectName("captureSettingsDialog");
  dialog.setWindowTitle(uiTr("Снимки и лист миниатюр"));
  auto *layout = new QFormLayout(&dialog);
  auto *formats = new QComboBox;
  formats->setObjectName("capture/format");
  for (const auto &f : MediaExports::formats())
    formats->addItem(f.toUpper(), f);
  formats->setCurrentIndex(formats->findData(format(profile.settings)));
  layout->addRow(uiTr("Формат изображения:"), formats);
  const auto spin = [&](const QString &key, const QString &label, int def,
                        int low, int high) {
    auto *s = new QSpinBox;
    s->setObjectName(key);
    s->setRange(low, high);
    s->setValue(profile.settings.value(key, def).toInt());
    layout->addRow(label, s);
    return s;
  };
  auto *quality =
      spin("capture/quality", uiTr("Качество (1–100):"), 90, 1, 100);
  auto *columns = spin("capture/columns", uiTr("Столбцов миниатюр:"), 4, 1, 6);
  auto *rows = spin("capture/rows", uiTr("Строк миниатюр:"), 4, 1, 10);
  auto *width = spin("capture/thumbWidth", uiTr("Ширина миниатюры, пикселей:"),
                     240, 80, 480);
  auto *subs = new QCheckBox(uiTr("Включать субтитры в снимок кадра"));
  subs->setChecked(
      profile.settings.value("ui/screenshotSubtitles", true).toBool());
  subs->setObjectName("ui/screenshotSubtitles");
  layout->addRow(subs);
  auto *note = new QLabel(
      uiTr("Кадр — изображение выбранной видеодорожки, с субтитрами по выбору. "
           "Область видео включает масштаб, поля и OSD Qt. Лист миниатюр "
           "извлекает исходную выбранную внутреннюю видеодорожку локального "
           "файла через FFmpeg, без субтитров, фильтров и ICC. Позиция и пауза "
           "плеера не меняются. Качество влияет на кодирование выбранного "
           "формата; PNG/BMP сохраняют пиксели без потерь."));
  note->setWordWrap(true);
  note->setMaximumWidth(640);
  layout->addRow(note);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                           QDialogButtonBox::RestoreDefaults);
  layout->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons->button(QDialogButtonBox::RestoreDefaults),
          &QPushButton::clicked, &dialog, [=] {
            formats->setCurrentIndex(formats->findData("png"));
            quality->setValue(90);
            columns->setValue(4);
            rows->setValue(4);
            width->setValue(240);
            subs->setChecked(true);
          });
  if (dialog.exec() == QDialog::Accepted) {
    profile.settings.setValue("capture/format", formats->currentData());
    for (auto *s : {quality, columns, rows, width})
      profile.settings.setValue(s->objectName(), s->value());
    profile.settings.setValue("ui/screenshotSubtitles", subs->isChecked());
  }
}
QVariantMap Window::mediaProperties() const {
  QVariantMap out{{"source", current},
                  {"renderer", player->outputName()},
                  {"stopped", stopped}};
  if (stopped)
    return out;
  for (const auto *key :
       {"media-title",      "file-format",      "file-size",
        "duration",         "time-pos",         "seekable",
        "metadata",         "track-list",       "chapter-list",
        "video-params",     "video-out-params", "video-codec",
        "video-codec-name", "estimated-vf-fps", "video-bitrate",
        "audio-codec-name", "audio-params",     "audio-out-params",
        "audio-bitrate",    "hwdec-current",    "demuxer-cache-state"})
    out[key] = player->get(key);
  const auto video = out.value("video-params").toMap();
  const auto audio = out.value("audio-params").toMap();
  out["resolution"] = video.isEmpty()
                          ? QVariant()
                          : QVariant(QString("%1 × %2")
                                         .arg(video.value("w").toInt())
                                         .arg(video.value("h").toInt()));
  out["audio-rate"] = audio.value("samplerate");
  out["audio-channels"] = audio.value("channels");
  if (out.value("video-codec-name").toString().isEmpty()) {
    for (const auto &value : out.value("track-list").toList()) {
      const auto track = value.toMap();
      if (track.value("type").toString() == "video" &&
          track.value("selected").toBool()) {
        out["video-codec-name"] = track.value("codec");
        break;
      }
    }
    if (out.value("video-codec-name").toString().isEmpty())
      out["video-codec-name"] = out.value("video-codec");
  }
  return out;
}
void Window::showMediaProperties() {
  if (auto *open = findChild<QDialog *>("mediaPropertiesDialog")) {
    open->show();
    open->raise();
    return;
  }
  auto *dialog = new QDialog(this);
  dialog->setObjectName("mediaPropertiesDialog");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(uiTr("Свойства текущего источника"));
  dialog->resize(900, 680);
  auto *layout = new QVBoxLayout(dialog);
  auto *tabs = new QTabWidget;
  layout->addWidget(tabs);
  auto *general = new QTableWidget(0, 2);
  general->setObjectName("mediaPropertiesGeneral");
  general->setHorizontalHeaderLabels({uiTr("Параметр"), uiTr("Значение")});
  general->setEditTriggers(QAbstractItemView::NoEditTriggers);
  general->horizontalHeader()->setStretchLastSection(true);
  tabs->addTab(general, uiTr("Основное"));
  auto *tracks = new QTableWidget(0, 7);
  tracks->setObjectName("mediaPropertiesTracks");
  tracks->setHorizontalHeaderLabels({uiTr("Тип"), "ID", uiTr("Язык"),
                                     uiTr("Кодек"), uiTr("Название"),
                                     uiTr("Выбрана"), uiTr("Внешняя")});
  tracks->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tracks->horizontalHeader()->setStretchLastSection(true);
  tabs->addTab(tracks, uiTr("Дорожки"));
  auto *json = new QPlainTextEdit;
  json->setReadOnly(true);
  json->setObjectName("mediaPropertiesJson");
  tabs->addTab(json, uiTr("Теги и диагностика JSON"));
  auto *report = new QPlainTextEdit;
  report->setReadOnly(true);
  report->setObjectName("mediaPropertiesReport");
  tabs->addTab(report, "MediaInfo / FFprobe");
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  auto *refresh =
      buttons->addButton(uiTr("Обновить"), QDialogButtonBox::ActionRole);
  refresh->setObjectName("mediaPropertiesRefresh");
  auto *probe = buttons->addButton(uiTr("Получить MediaInfo"),
                                   QDialogButtonBox::ActionRole);
  probe->setObjectName("mediaPropertiesProbe");
  auto *copy =
      buttons->addButton(uiTr("Копировать JSON"), QDialogButtonBox::ActionRole);
  auto *copyReport = buttons->addButton(uiTr("Копировать отчёт"),
                                        QDialogButtonBox::ActionRole);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  connect(copy, &QPushButton::clicked, dialog,
          [json] { QApplication::clipboard()->setText(json->toPlainText()); });
  connect(copyReport, &QPushButton::clicked, dialog, [report] {
    QApplication::clipboard()->setText(report->toPlainText());
  });
  auto source = std::make_shared<QString>();
  auto *process = new ReportProcess(dialog);
  auto *deadline = new QTimer(process);
  deadline->setSingleShot(true);
  connect(deadline, &QTimer::timeout, process, &QProcess::kill);
  connect(dialog, &QDialog::finished, process, [process] {
    if (process->state() != QProcess::NotRunning)
      process->kill();
  });
  auto output = std::make_shared<QByteArray>();
  connect(process, &QProcess::readyReadStandardOutput, dialog, [=] {
    output->append(process->readAllStandardOutput());
    if (output->size() > 1024 * 1024)
      process->kill();
  });
  connect(process, &QProcess::errorOccurred, dialog, [=] {
    deadline->stop();
    probe->setEnabled(true);
    report->setPlainText(process->errorString());
  });
  connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          dialog, [=](int code, QProcess::ExitStatus status) {
            deadline->stop();
            output->append(process->readAllStandardOutput());
            const auto text = QString::fromUtf8(output->left(1024 * 1024));
            report->setPlainText(
                code == 0 && status == QProcess::NormalExit &&
                        output->size() <= 1024 * 1024
                    ? text
                    : uiTr("Отчёт не завершён или превышает 1 МиБ. ") + text);
            probe->setEnabled(true);
          });
  const auto update = [=] {
    if (process->state() != QProcess::NotRunning)
      return;
    const auto values = mediaProperties();
    *source = current;
    json->setPlainText(
        QString::fromUtf8(QJsonDocument::fromVariant(values).toJson()));
    report->setPlainText(uiTr("MediaInfo/FFprobe запускается только по кнопке "
                              "для локального файла."));
    const QList<QPair<QString, QString>> fields{
        {"source", uiTr("Источник")},
        {"media-title", uiTr("Название")},
        {"file-format", uiTr("Контейнер")},
        {"file-size", uiTr("Размер, байт")},
        {"duration", uiTr("Длительность, с")},
        {"time-pos", uiTr("Позиция, с")},
        {"estimated-vf-fps", "FPS"},
        {"resolution", uiTr("Разрешение")},
        {"video-codec-name", uiTr("Видеокодек")},
        {"video-bitrate", uiTr("Видеобитрейт, бит/с")},
        {"audio-codec-name", uiTr("Аудиокодек")},
        {"audio-bitrate", uiTr("Аудиобитрейт, бит/с")},
        {"audio-rate", uiTr("Частота звука, Гц")},
        {"audio-channels", uiTr("Каналы звука")},
        {"renderer", uiTr("Вывод")},
        {"hwdec-current", uiTr("Аппаратный декодер")}};
    general->setRowCount(fields.size());
    for (int i = 0; i < fields.size(); ++i) {
      general->setItem(i, 0, new QTableWidgetItem(fields[i].second));
      const auto v = values.value(fields[i].first);
      QString text = v.toString();
      if (v.typeId() == QMetaType::Double)
        text = QString::number(v.toDouble(), 'f', 3);
      general->setItem(
          i, 1,
          new QTableWidgetItem(text.isEmpty() ? QString::fromUtf8("—") : text));
    }
    general->resizeColumnToContents(0);
    general->setColumnWidth(0, std::max(220, general->columnWidth(0)));
    tracks->setRowCount(0);
    for (const auto &value : values.value("track-list").toList()) {
      const auto t = value.toMap();
      int col = 0, row = tracks->rowCount();
      tracks->insertRow(row);
      for (const auto *key :
           {"type", "id", "lang", "codec", "title", "selected", "external"})
        tracks->setItem(row, col++,
                        new QTableWidgetItem(t.value(key).toString()));
    }
    tracks->resizeColumnsToContents();
  };
  connect(refresh, &QPushButton::clicked, dialog, update);
  update();
  connect(probe, &QPushButton::clicked, dialog, [=] {
    if (process->state() != QProcess::NotRunning)
      return;
    if (MediaSource::isUrl(*source) || !QFileInfo(*source).isFile()) {
      report->setPlainText(
          uiTr("Отчёт доступен только для локального медиафайла."));
      return;
    }
    const auto mediaInfo = QStandardPaths::findExecutable("mediainfo");
    const auto executable = mediaInfo.isEmpty()
                                ? QStandardPaths::findExecutable("ffprobe")
                                : mediaInfo;
    if (executable.isEmpty()) {
      report->setPlainText(uiTr("Для отчёта установите mediainfo или ffmpeg."));
      return;
    }
    output->clear();
    report->setPlainText(uiTr("Чтение: ") + *source);
    probe->setEnabled(false);
    tabs->setCurrentWidget(report);
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->start(executable,
                   mediaInfo.isEmpty()
                       ? QStringList{"-v", "error", "-protocol_whitelist",
                                     "file,crypto,data", "-show_format",
                                     "-show_streams", "-of", "json", "-i",
                                     *source}
                       : QStringList{*source});
    deadline->start(10000);
  });
  dialog->show();
}
void Window::contactSheetDialog() {
  if (auto *open = findChild<QProgressDialog *>("thumbnailSheetProgress")) {
    open->show();
    open->raise();
    return;
  }
  if (stopped || progressive || MediaSource::isUrl(current) ||
      !QFileInfo(current).isFile() || duration <= 0) {
    showOsd(uiTr("Лист миниатюр доступен для локального видео с известной "
                 "длительностью."));
    return;
  }
  int stream = -1;
  for (const auto &v : player->get("track-list").toList()) {
    const auto t = v.toMap();
    if (t.value("type") == "video" && t.value("selected").toBool() &&
        !t.value("external").toBool())
      stream = t.value("ff-index", -1).toInt();
  }
  if (stream < 0) {
    showOsd(uiTr("Не выбрана внутренняя видеодорожка."));
    return;
  }
  const auto source = current;
  const auto length = duration;
  auto filter = selectedFilter(format(profile.settings));
  auto target = QFileDialog::getSaveFileName(
      this, uiTr("Сохранить лист миниатюр"),
      QDir(profile.directory + "/Screenshots")
          .filePath(captureName() + "-sheet." + format(profile.settings)),
      fileFilter(), &filter);
  target = ensureSuffix(target, filter);
  if (target.isEmpty())
    return;
  if (QFileInfo(target).canonicalFilePath() ==
      QFileInfo(current).canonicalFilePath())
    return;
  auto *progress = new QProgressDialog(uiTr("Создание листа миниатюр…"),
                                       uiTr("Отмена"), 0, 100, this);
  progress->setObjectName("thumbnailSheetProgress");
  progress->setAttribute(Qt::WA_DeleteOnClose);
  progress->setAutoClose(false);
  progress->setAutoReset(false);
  auto *job = new ThumbnailSheet(progress);
  connect(progress, &QProgressDialog::canceled, job, &ThumbnailSheet::cancel);
  connect(progress, &QProgressDialog::canceled, progress, &QWidget::close);
  connect(
      job, &ThumbnailSheet::progress, progress,
      [progress](int n, int total) { progress->setValue(n * 100 / total); });
  connect(job, &ThumbnailSheet::failed, progress, [=](const QString &error) {
    showOsd(error);
    progress->close();
  });
  const int quality = profile.settings.value("capture/quality", 90).toInt();
  connect(job, &ThumbnailSheet::ready, progress, [=](const QImage &image) {
    QString error;
    const bool ok = MediaExports::writeImage(image, target, quality, &error);
    showOsd(ok ? uiTr("Лист миниатюр сохранён.") : error);
    progress->close();
  });
  progress->show();
  if (!job->start(
          source, length,
          std::clamp(profile.settings.value("capture/columns", 4).toInt(), 1,
                     6),
          std::clamp(profile.settings.value("capture/rows", 4).toInt(), 1, 10),
          std::clamp(profile.settings.value("capture/thumbWidth", 240).toInt(),
                     80, 480),
          stream)) {
    showOsd(uiTr("Не удалось начать создание миниатюр; нужен FFmpeg."));
    progress->close();
  }
}
void Window::saveMediaCopyDialog() {
  if (auto *open = findChild<QProgressDialog *>("mediaCopyProgress")) {
    open->show();
    open->raise();
    return;
  }
  if (stopped || progressive || MediaSource::isUrl(current) ||
      !QFileInfo(current).isFile()) {
    showOsd(uiTr(
        "Копирование доступно только для готового локального медиафайла."));
    return;
  }
  const auto source = current;
  const QFileInfo info(source);
  const auto proposed =
      info.dir().filePath(info.completeBaseName() + "-copy." + info.suffix());
  const auto target =
      QFileDialog::getSaveFileName(this, uiTr("Сохранить копию медиафайла"),
                                   proposed, uiTr("Все файлы (*)"));
  if (target.isEmpty())
    return;
  auto *progress = new QProgressDialog(uiTr("Копирование медиафайла…"),
                                       uiTr("Отмена"), 0, 1000, this);
  progress->setObjectName("mediaCopyProgress");
  progress->setAttribute(Qt::WA_DeleteOnClose);
  progress->setAutoClose(false);
  progress->setAutoReset(false);
  auto *job = new MediaCopy(progress);
  connect(progress, &QProgressDialog::canceled, job, &MediaCopy::cancel);
  connect(progress, &QProgressDialog::canceled, progress, &QWidget::close);
  connect(job, &MediaCopy::progress, progress,
          [progress](qint64 n, qint64 total) {
            progress->setValue(total ? int(1000. * n / total) : 0);
          });
  connect(job, &MediaCopy::finished, progress, [=](bool, const QString &text) {
    showOsd(text);
    progress->close();
  });
  progress->show();
  if (!job->start(source, target)) {
    showOsd(uiTr("Не удалось начать копирование; проверьте путь назначения."));
    progress->close();
  }
}
