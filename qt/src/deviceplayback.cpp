#include "devicesource.h"
#include "dvbscanner.h"
#include "i18n.h"
#include "player.h"
#include "window.h"
#include <QtWidgets>

namespace {
QStringList audioInputs() {
  QStringList result;
  QProcess process;
  process.start("pactl", {"--format=json", "list", "sources"});
  if (process.waitForFinished(1500) && process.exitCode() == 0) {
    const auto raw = process.readAllStandardOutput();
    if (raw.size() <= 1024 * 1024)
      for (const auto &value : QJsonDocument::fromJson(raw).array()) {
        const auto input = value.toObject();
        const QString name = input["name"].toString();
        if (!name.isEmpty())
          result.append(name);
      }
  } else {
    process.kill();
    process.waitForFinished(500);
  }
  return result;
}
void errorText(QLabel *label, const QString &message) {
  label->setTextFormat(Qt::PlainText);
  label->setText(message);
  label->setWordWrap(true);
}
} // namespace

bool Window::openDeviceSource(const QString &source) {
  emit playbackIntentChanged();
  const auto plan = DeviceSource::resolve(
      source, player->get("protocol-list").toStringList(),
      player->get("demuxer-lavf-list").toStringList(), profile.directory);
  if (plan.url.isEmpty() || !plan.error.isEmpty()) {
    state->setText(uiTr("Диск / устройство: ") + plan.error);
    state->setToolTip(plan.error);
    return false;
  }
  openFiles({source});
  return true;
}
void Window::openDiscDialog() {
  QDialog dialog(this);
  dialog.setObjectName("openDiscDialog");
  dialog.setWindowTitle(uiTr("Открыть диск или ISO"));
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout;
  layout->addLayout(form);
  auto *type = new QComboBox;
  type->setObjectName("discType");
  type->addItem("DVD", "dvd");
  type->addItem("Blu-ray", "bluray");
  type->addItem("Audio CD", "cdda");
  type->setCurrentIndex(std::max(
      0, type->findData(profile.settings.value("devices/discKind", "dvd"))));
  form->addRow(uiTr("Тип диска"), type);
  auto *path = new QComboBox;
  path->setObjectName("discPath");
  path->setEditable(true);
  path->addItems(DeviceSource::opticalDrives());
  path->setCurrentText(
      profile.settings.value("devices/discPath", path->currentText())
          .toString());
  form->addRow(uiTr("Дисковод / образ / папка"), path);
  auto *browse = new QHBoxLayout;
  auto *image = new QPushButton(uiTr("Выбрать ISO…"));
  auto *folder = new QPushButton(uiTr("Выбрать папку…"));
  browse->addWidget(image);
  browse->addWidget(folder);
  layout->addLayout(browse);
  connect(image, &QPushButton::clicked, &dialog, [&] {
    const QString chosen = QFileDialog::getOpenFileName(
        &dialog, uiTr("Образ диска"), path->currentText(),
        uiTr("ISO (*.iso);;Все файлы (*)"));
    if (!chosen.isEmpty())
      path->setCurrentText(chosen);
  });
  connect(folder, &QPushButton::clicked, &dialog, [&] {
    const QString chosen = QFileDialog::getExistingDirectory(
        &dialog, uiTr("Папка диска"), path->currentText());
    if (!chosen.isEmpty())
      path->setCurrentText(chosen);
  });
  auto *title = new QSpinBox;
  title->setObjectName("discTitle");
  title->setRange(0, 99999);
  title->setSpecialValueText(uiTr("Автоматически — самый длинный"));
  auto *titleLabel = new QLabel;
  form->addRow(titleLabel, title);
  auto *angle = new QSpinBox;
  angle->setObjectName("discAngle");
  angle->setRange(1, 9);
  angle->setValue(profile.settings.value("devices/dvdAngle", 1).toInt());
  form->addRow(uiTr("Угол DVD"), angle);
  auto *status = new QLabel;
  layout->addWidget(status);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  auto refresh = [&] {
    const auto kind = type->currentData().toString();
    title->setMaximum(kind == "dvd" ? 99 : 99999);
    title->setEnabled(kind != "cdda");
    titleLabel->setText(kind == "bluray" ? uiTr("Номер плейлиста MPLS")
                                         : uiTr("Номер заголовка (с 1)"));
    angle->setEnabled(kind == "dvd");
    image->setEnabled(kind != "cdda");
    folder->setEnabled(kind != "cdda");
    const bool supported = player->get("protocol-list")
                               .toStringList()
                               .contains(kind == "bluray" ? "bd" : kind);
    buttons->button(QDialogButtonBox::Open)->setEnabled(supported);
    errorText(
        status,
        !supported
            ? uiTr("В этой сборке движка нет поддержки выбранного диска.")
        : kind == "dvd" ? uiTr("Главы, дорожки и заголовки доступны. "
                               "Интерактивное DVD-меню движок не поддерживает.")
        : kind == "bluray" ? uiTr("Открытие фильма или выбранного плейлиста. "
                                  "Интерактивное Blu-ray-меню не подключено.")
                           : uiTr("Треки CD выбираются через меню глав или "
                                  "Ctrl+PageUp / Ctrl+PageDown."));
  };
  connect(type, &QComboBox::currentIndexChanged, &dialog, refresh);
  refresh();
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    DeviceSource::Request request;
    request.kind = type->currentData().toString();
    request.path = QFileInfo(path->currentText().trimmed()).absoluteFilePath();
    request.title = request.kind == "cdda" ? 0 : title->value();
    request.angle = request.kind == "dvd" ? angle->value() : 1;
    if (!openDeviceSource(DeviceSource::encode(request))) {
      errorText(status, state->text());
      return;
    }
    profile.settings.setValue("devices/discKind", request.kind);
    profile.settings.setValue("devices/discPath", request.path);
    profile.settings.setValue("devices/dvdAngle", request.angle);
    dialog.accept();
  });
  dialog.exec();
}
void Window::changeDiscTitle() {
  DeviceSource::Request request;
  if (!DeviceSource::decode(current, &request) ||
      (request.kind != "dvd" && request.kind != "bluray"))
    return;
  bool ok = false;
  const int title = QInputDialog::getInt(
      this, uiTr("Выбор фильма на диске"),
      request.kind == "dvd" ? uiTr("Номер заголовка: 1–99; 0 — самый длинный")
                            : uiTr("Номер MPLS; 0 — самый длинный"),
      request.title, 0, request.kind == "dvd" ? 99 : 99999, 1, &ok);
  if (ok) {
    request.title = title;
    openDeviceSource(DeviceSource::encode(request));
  }
}
void Window::openCaptureDialog() {
  QDialog dialog(this);
  dialog.setObjectName("openCaptureDialog");
  dialog.setWindowTitle(uiTr("Устройство захвата"));
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout;
  layout->addLayout(form);
  auto *type = new QComboBox;
  type->setObjectName("captureType");
  for (const auto &entry : QList<QPair<QString, QString>>{
           {uiTr("Видео — камера / карта захвата"), "v4l2"},
           {uiTr("Звук — PulseAudio / PipeWire"), "pulse"},
           {uiTr("Звук — ALSA"), "alsa"},
           {uiTr("Тестовый видеосигнал"), "test-video"},
           {uiTr("Тестовый звук"), "test-audio"}})
    type->addItem(entry.first, entry.second);
  type->setCurrentIndex(std::max(0, type->findData(profile.settings.value(
                                        "devices/captureKind", "v4l2"))));
  form->addRow(uiTr("Источник"), type);
  auto *path = new QComboBox;
  path->setObjectName("capturePath");
  path->setEditable(true);
  form->addRow(uiTr("Устройство / вход"), path);
  auto *audio = new QComboBox;
  audio->setObjectName("captureAudio");
  audio->addItem(uiTr("Без отдельного аудиовхода"), "");
  audio->addItem(uiTr("Системный аудиовход"), "pulse:default");
  const QStringList inputs = audioInputs();
  for (const auto &input : inputs)
    audio->addItem(input, "pulse:" + input);
  audio->setCurrentIndex(std::max(
      0, audio->findData(profile.settings.value("devices/captureAudio", ""))));
  form->addRow(uiTr("Звук для видео"), audio);
  auto *status = new QLabel;
  layout->addWidget(status);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  auto refresh = [&] {
    const auto kind = type->currentData().toString();
    path->clear();
    if (kind == "v4l2")
      for (const auto &device : DeviceSource::videoDevices())
        path->addItem(device.second, device.first);
    else if (kind == "pulse") {
      path->addItem("default");
      path->addItems(inputs);
    } else if (kind == "alsa")
      path->addItem("default");
    const QString saved =
        profile.settings.value("devices/capturePath").toString();
    if (kind == profile.settings.value("devices/captureKind").toString() &&
        !saved.isEmpty())
      path->setEditText(saved);
    path->setEnabled(!kind.startsWith("test-"));
    audio->setEnabled(kind == "v4l2" || kind == "test-video");
    const auto formats = player->get("demuxer-lavf-list").toStringList();
    const bool supported =
        player->get("protocol-list").toStringList().contains("av") &&
        (formats.contains(kind.startsWith("test-") ? "lavfi" : kind) ||
         (kind == "v4l2" && formats.contains("video4linux2")));
    buttons->button(QDialogButtonBox::Open)->setEnabled(supported);
    errorText(
        status,
        !supported
            ? uiTr("В этой сборке движка нет выбранного способа захвата.")
        : kind == "v4l2" && path->count() == 0
            ? uiTr("Видеоустройства не найдены. Можно указать /dev/videoN или "
                   "проверить тестовый сигнал.")
            : uiTr("Просмотр прямого входа. Позиция не сохраняется; перемотка "
                   "недоступна."));
  };
  connect(type, &QComboBox::currentIndexChanged, &dialog, refresh);
  refresh();
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    DeviceSource::Request request;
    request.kind = type->currentData().toString();
    request.path = path->currentData().toString();
    if (request.path.isEmpty() ||
        path->currentText() != path->itemText(path->currentIndex()))
      request.path = path->currentText().trimmed();
    if (audio->isEnabled())
      request.audio = audio->currentData().toString();
    if (!openDeviceSource(DeviceSource::encode(request))) {
      errorText(status, state->text());
      return;
    }
    profile.settings.setValue("devices/captureKind", request.kind);
    profile.settings.setValue("devices/capturePath", request.path);
    profile.settings.setValue("devices/captureAudio", request.audio);
    dialog.accept();
  });
  dialog.exec();
}
void Window::openTelevisionDialog() {
  QDialog dialog(this);
  dialog.setObjectName("openTelevisionDialog");
  dialog.setWindowTitle(uiTr("Цифровое ТВ — DVB"));
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout;
  layout->addLayout(form);
  auto *card = new QSpinBox;
  card->setRange(0, 15);
  card->setValue(profile.settings.value("devices/dvbCard", 0).toInt());
  form->addRow(uiTr("Номер тюнера"), card);
  auto *channel = new QComboBox;
  channel->setObjectName("dvbChannel");
  form->addRow(uiTr("Канал"), channel);
  QString configName = profile.settings.value("devices/dvbFile").toString();
  auto *import = new QPushButton(uiTr("Импортировать список каналов…"));
  layout->addWidget(import);
  auto *scan = new QPushButton(uiTr("Найти каналы…"));
  scan->setObjectName("dvbScan");
  auto *cancelScan = new QPushButton(uiTr("Остановить поиск"));
  cancelScan->setEnabled(false);
  layout->addWidget(scan);
  layout->addWidget(cancelScan);
  DvbScanner scanner(profile.directory, &dialog);
  auto *status = new QLabel;
  layout->addWidget(status);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  auto refresh = [&] {
    channel->clear();
    QFile config(QDir(profile.directory).filePath(configName));
    if (!configName.isEmpty() &&
        QFileInfo(configName).fileName() == configName &&
        config.open(QIODevice::ReadOnly) && config.size() <= 1024 * 1024)
      channel->addItems(DeviceSource::channels(config.readAll()));
    const bool backend =
        player->get("protocol-list").toStringList().contains("dvb");
    const bool hardware = QFileInfo::exists(
        QString("/dev/dvb/adapter%1/frontend0").arg(card->value()));
    buttons->button(QDialogButtonBox::Open)
        ->setEnabled(backend && hardware && channel->count() &&
                     !scanner.running());
    scan->setEnabled(backend && hardware && !scanner.running());
    errorText(status,
              !backend    ? uiTr("В этой сборке движка нет поддержки DVB.")
              : !hardware ? uiTr("Выбранный тюнер не найден.")
              : channel->count()
                  ? uiTr("Список хранится в папке программы. Поддерживаются "
                         "списки VDR / zap для движка mpv.")
                  : uiTr("Импортируйте список каналов или найдите их по "
                         "начальному файлу частот DVBV5."));
  };
  connect(scan, &QPushButton::clicked, &dialog, [&] {
    const QString executable =
        profile.settings
            .value("devices/dvbScannerPath",
                   QStandardPaths::findExecutable("dvbv5-scan"))
            .toString();
    if (executable.isEmpty()) {
      errorText(status, uiTr("Для поиска каналов установите пакет dvb-tools."));
      return;
    }
    const QString input = QFileDialog::getOpenFileName(
        &dialog, uiTr("Начальные частоты DVBV5"), "/usr/share/dvb",
        uiTr("Файлы частот (*)"));
    if (input.isEmpty())
      return;
    DeviceSource::Request playing;
    if (DeviceSource::decode(current, &playing) && playing.kind == "dvb")
      closeCurrent();
    QString error;
    if (!scanner.start(executable, input, card->value(), &error)) {
      errorText(status, error);
      return;
    }
    scan->setEnabled(false);
    card->setEnabled(false);
    import->setEnabled(false);
    cancelScan->setEnabled(true);
    buttons->button(QDialogButtonBox::Open)->setEnabled(false);
    errorText(status, uiTr("Идёт поиск каналов…"));
  });
  connect(cancelScan, &QPushButton::clicked, &scanner, &DvbScanner::cancel);
  connect(&scanner, &DvbScanner::progress, &dialog,
          [&](const QString &text) { errorText(status, text); });
  connect(&scanner, &DvbScanner::finished, &dialog,
          [&](bool success, const QString &message, const QString &name) {
            card->setEnabled(true);
            import->setEnabled(true);
            cancelScan->setEnabled(false);
            if (success) {
              configName = name;
              profile.settings.setValue("devices/dvbFile", name);
            }
            refresh();
            errorText(status, message);
          });
  connect(card, &QSpinBox::valueChanged, &dialog, refresh);
  connect(import, &QPushButton::clicked, &dialog, [&] {
    const QString chosen =
        QFileDialog::getOpenFileName(&dialog, uiTr("Список DVB-каналов"), {},
                                     uiTr("Списки каналов (*);;Все файлы (*)"));
    if (chosen.isEmpty())
      return;
    QFile input(chosen);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 1024 * 1024) {
      errorText(status, uiTr("Список недоступен или больше 1 МБ"));
      return;
    }
    const auto data = input.readAll();
    QString error;
    if (DeviceSource::channels(data, &error).isEmpty()) {
      errorText(status, error.isEmpty() ? uiTr("В файле нет каналов VDR / zap")
                                        : error);
      return;
    }
    const QString suffix = QFileInfo(chosen).suffix().toLower();
    const QString importedName =
        "FoxicMPQt.channels.conf" +
        (QSet<QString>{"ter", "ter1", "sat", "sat1", "cbl", "atsc", "isdbt"}
                 .contains(suffix)
             ? "." + suffix
             : "");
    QSaveFile file(QDir(profile.directory).filePath(importedName));
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
        !file.commit()) {
      errorText(status, uiTr("Не удалось сохранить список в папке программы"));
      return;
    }
    configName = importedName;
    profile.settings.setValue("devices/dvbFile", configName);
    refresh();
  });
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    DeviceSource::Request request;
    request.kind = "dvb";
    request.channels = configName;
    request.channel = channel->currentText();
    request.card = card->value();
    if (!openDeviceSource(DeviceSource::encode(request))) {
      errorText(status, state->text());
      return;
    }
    profile.settings.setValue("devices/dvbCard", request.card);
    dialog.accept();
  });
  refresh();
  dialog.exec();
}
