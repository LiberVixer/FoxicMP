#include "webcontrol.h"
#include "window.h"
#include "mediaremote.h"
#include "player.h"
#include "playlistpanel.h"
#include "playlistfile.h"
#include "mediasource.h"
#include "i18n.h"
#include <QtWidgets>
#include <cmath>
QByteArray Window::webSnapshot() {
  if (stopped || !player || player->get("video-params").toMap().isEmpty()) return {};
  auto image = player->grabFramebuffer();
  if (image.isNull()) return {};
  constexpr int maximumWidth = 960;
  if (image.width() > maximumWidth) image = image.scaledToWidth(maximumWidth, Qt::SmoothTransformation);
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly) || !image.convertToFormat(QImage::Format_RGB888).save(&buffer, "JPEG", 78)) return {};
  return bytes;
}
QVariantMap Window::webState(int offset) const {
  const auto remote = mediaRemote->state();
  QVariantList queue;
  const int total = playlist ? playlist->count() : 0;
  offset = total ? std::min(std::max(0, offset) / 500 * 500, (total - 1) / 500 * 500) : 0;
  if (playlist)
    for (int i = offset; i < std::min(offset + 500, total); ++i) {
      const auto *item = playlist->item(i);
      queue.append(QVariantMap{{"id", item->data(PlaylistPanel::Id)},
                               {"title", item->text().left(512)},
                               {"current", item == currentItem}});
    }
  return {{"title", stopped ? QString("FoxicMP Qt") : player->get("media-title").toString().left(512)},
          {"status", remote.value("PlaybackStatus")},
          {"position", remote.value("Position").toLongLong() / 1000000.},
          {"duration", duration}, {"volume", volume->value()},
          {"muted", player->get("mute")}, {"rate", player->get("speed")},
          {"canSeek", remote.value("CanSeek")},
          {"canNext", remote.value("CanGoNext")},
          {"canPrevious", remote.value("CanGoPrevious")},
          {"queue", queue}, {"queueTotal", total}, {"queueOffset", offset}, {"queueLimit", 500},
          {"queueList", playlist ? playlist->property("tabId") : QVariant()},
          {"canEditQueue", playlist && !stdinPlayback}, {"canEnqueue", !stdinPlayback}};
}
bool Window::webCommand(const QString &name, const QVariant &value) {
  if (name == "queuePlay" || name == "queueRemove") {
    const auto target = value.toMap();
    if (stdinPlayback || !playlist || target.size() != 2 ||
        target.value("list").toString() != playlist->property("tabId").toString()) return false;
    const auto id = target.value("id").toString();
    if (id.isEmpty()) return false;
    if (name == "queueRemove") return queuePanel->removeById(playlist, id);
    for (int row = 0; row < playlist->count(); ++row)
      if (playlist->item(row)->data(PlaylistPanel::Id).toString() == id) {
        playIndex(row, -1, true, false, player->get("pause").toBool());
        return true;
      }
    return false;
  }
  if (name == "open" || name == "enqueue") {
    const QString input = value.toString().trimmed();
    if (input.isEmpty() || input.size() > 4096 || input.contains(QRegularExpression("[\\x00-\\x1f\\x7f]"))) return false;
    const QUrl url(input, QUrl::StrictMode);
    QString source;
    if (MediaSource::isNetwork(input)) source = MediaSource::normalize(input);
    else {
      if (!url.scheme().isEmpty() && !url.isLocalFile() && !QFileInfo(input).isAbsolute()) return false;
      if (url.isLocalFile() && !url.host().isEmpty() && url.host() != "localhost") return false;
      const auto path = url.isLocalFile() ? url.toLocalFile() : input;
      const QFileInfo file(path);
      if (!file.isAbsolute() || !file.isFile() || !file.isReadable() || PlaylistFile::isPlaylist(path)) return false;
      source = file.absoluteFilePath();
    }
    if (source.isEmpty()) return false;
    if (name == "open") { openFiles({source}, true); return true; }
    if (stdinPlayback) return false;
    if (!playlist) selectPlaybackList(queuePanel->activeList());
    queuePanel->append(playlist, {source}, false);
    return true;
  }
  const auto status = mediaRemote->state();
  const QMap<QString, QString> basic{{"play", "Play"}, {"pause", "Pause"},
      {"toggle", "PlayPause"}, {"stop", "Stop"}, {"next", "Next"},
      {"previous", "Previous"}};
  if (basic.contains(name)) {
    if ((name == "next" && !status.value("CanGoNext").toBool()) ||
        (name == "previous" && !status.value("CanGoPrevious").toBool()) ||
        (name == "play" && !status.value("CanPlay").toBool()) ||
        ((name == "pause" || (name == "stop" && !stdinPlayback)) && stopped))
      return false;
    mediaRemote->invoke(basic.value(name)); return true;
  }
  const double number = value.toDouble();
  if (name == "seek") {
    if (!std::isfinite(number) || number < 0 || number >= duration || !status.value("CanSeek").toBool()) return false;
    return seekTo(number);
  }
  if (name == "volume" && std::isfinite(number) && number >= 0 && number <= 100) {
    mediaRemote->invoke("Volume", {number / 100.}); return true;
  }
  if (name == "rate" && !stopped && status.value("MaximumRate").toDouble() > 1 &&
      std::isfinite(number) && number >= .125 && number <= 16) {
    mediaRemote->invoke("Rate", {number}); return true;
  }
  if (name == "mute") { actions["mute"]->trigger(); return true; }
  return false;
}
bool Window::replaceWebControl(quint16 port, const QHostAddress &address, QString *error) {
  if (webControl->running() && webControl->port() == port && webControl->address() == address)
    return true;
  // Bind first. A failed mode/address/port change keeps the active listener,
  // its code, connected clients and saved settings intact.
  auto *replacement = new WebControl([this](int offset) { return webState(offset); },
      [this](const QString &command, const QVariant &value) { return webCommand(command, value); }, this,
      [this] { return webSnapshot(); });
  if (!replacement->start(port, address)) {
    if (error) *error = replacement->errorString();
    delete replacement;
    return false;
  }
  delete webControl;
  webControl = replacement;
  webControl->setObjectName("webControl");
  return true;
}
bool Window::setTemporaryWebPort(quint16 port) {
  QString error;
  if (!replaceWebControl(port, QHostAddress::LocalHost, &error)) {
    const auto message = uiTr("Не удалось включить временный веб-порт: %1").arg(error);
    diagnosticMessages.append(message);
    while (diagnosticMessages.size() > 100) diagnosticMessages.removeFirst();
    showOsd(message);
    return false;
  }
  setProperty("foxicmpTemporaryWebPort", true);
  emit webControlChanged();
  return true;
}
void Window::setupWebControl(int temporaryPort) {
  webControl = new WebControl([this](int offset) { return webState(offset); },
      [this](const QString &command, const QVariant &value) { return webCommand(command, value); }, this,
      [this] { return webSnapshot(); });
  webControl->setObjectName("webControl");
  if (temporaryPort >= 1024 && temporaryPort <= 65535) {
    setTemporaryWebPort(quint16(temporaryPort));
    return;
  }
  if (profile.settings.value("web/enabled", false).toBool()) {
    bool valid = false;
    const int port = profile.settings.value("web/port", 13579).toInt(&valid);
    const bool lan = profile.settings.value("web/lanEnabled", false).toBool();
    auto address = lan ? QHostAddress(profile.settings.value("web/lanAddress").toString()) : QHostAddress(QHostAddress::LocalHost);
    if (lan && !WebControl::isPrivateAddress(address)) address = QHostAddress();
    if (!valid || port < 1024 || port > 65535 || !webControl->start(quint16(port), address))
      diagnosticMessages.append(uiTr("Веб-сервер не запущен: %1").arg(
          valid && port >= 1024 && port <= 65535 ? webControl->errorString() : uiTr("неверный порт")));
  }
}
void Window::webControlDialog() {
  if (auto *existing = findChild<QDialog *>("webControlDialog")) {
    existing->show(); existing->raise();
    if (existing->windowHandle())
      existing->windowHandle()->requestActivate();
    return;
  }
  auto *dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setObjectName("webControlDialog");
  dialog->setWindowTitle(uiTr("Веб-управление FoxicMP Qt"));
  dialog->resize(820, 490);
  auto *layout = new QVBoxLayout(dialog);
  auto *note = new QLabel(uiTr("Управление из браузера на этом компьютере или в доверенной локальной сети. Сетевой доступ включается явно и использует выбранный частный IPv4-адрес. Ссылка с кодом даёт управление текущим окном; HTTP не шифрует соединение."));
  note->setWordWrap(true); layout->addWidget(note);
  auto *form = new QFormLayout;
  layout->addLayout(form);
  auto *port = new QSpinBox;
  port->setObjectName("webPort"); port->setRange(1024,65535);
  port->setValue(std::clamp(profile.settings.value("web/port",13579).toInt(),1024,65535));
  form->addRow(uiTr("Порт:"),port);
  auto *lan = new QCheckBox(uiTr("Разрешить управление из локальной сети"));
  lan->setObjectName("webLanEnabled");
  lan->setChecked(profile.settings.value("web/lanEnabled", false).toBool());
  form->addRow(lan);
  auto *addresses = new QComboBox;
  addresses->setObjectName("webLanAddress");
  const auto fillAddresses = [=](const QString &selected) {
    addresses->clear();
    for (const auto &entry : WebControl::lanAddresses())
      addresses->addItem(entry.address.toString() + " — " + entry.name, entry.address.toString());
    if (!selected.isEmpty() && addresses->findData(selected) < 0)
      addresses->addItem(uiTr("Недоступный адрес: %1").arg(selected), selected);
    if (!selected.isEmpty()) addresses->setCurrentIndex(addresses->findData(selected));
    addresses->setEnabled(lan->isChecked());
  };
  fillAddresses(profile.settings.value("web/lanAddress").toString());
  auto *addressRow = new QHBoxLayout;
  addressRow->addWidget(addresses, 1);
  auto *reload = new QPushButton(uiTr("Обновить адреса"));
  reload->setObjectName("webRefreshAddresses");addressRow->addWidget(reload);
  form->addRow(uiTr("Сетевой интерфейс:"), addressRow);
  connect(lan, &QCheckBox::toggled, addresses, &QComboBox::setEnabled);
  connect(reload, &QPushButton::clicked, dialog, [=] { fillAddresses(addresses->currentData().toString()); });
  auto *url = new QLineEdit;
  url->setObjectName("webUrl"); url->setReadOnly(true);
  form->addRow(uiTr("Ссылка с кодом доступа:"),url);
  auto *status = new QLabel;
  status->setObjectName("webStatus"); status->setWordWrap(true); status->setTextFormat(Qt::PlainText);
  layout->addWidget(status);
  auto *row = new QHBoxLayout;
  layout->addLayout(row);
  auto *start = new QPushButton(uiTr("Применить и сохранить")); start->setObjectName("webStart");
  auto *stop = new QPushButton(uiTr("Выключить и сохранить")); stop->setObjectName("webStop");
  auto *open = new QPushButton(uiTr("Открыть в браузере")); open->setObjectName("webOpen");
  auto *copy = new QPushButton(uiTr("Копировать ссылку")); copy->setObjectName("webCopy");
  for (auto *button : {start,stop,open,copy}) row->addWidget(button);
  auto *limits = new QLabel(uiTr("Доступны воспроизведение, пауза, стоп, предыдущий/следующий файл, перемотка, громкость, звук и скорость. Можно выбирать и удалять записи очереди, открывать файл на компьютере плеера или URL и добавлять источник в очередь. Удаление записи не удаляет файл. Для другого окна выберите другой порт. После включения сервер запускается вместе с этим профилем."));
  limits->setWordWrap(true); layout->addWidget(limits);
  layout->addStretch();
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  layout->addWidget(buttons);
  const auto refresh = [=] {
    const bool active = webControl->running();
    if (active) {
      port->setValue(webControl->port()); lan->setChecked(webControl->lan());
      if (webControl->lan()) fillAddresses(webControl->address().toString());
    }
    port->setEnabled(true); start->setEnabled(true); stop->setEnabled(active || profile.settings.value("web/enabled",false).toBool());
    open->setEnabled(active);copy->setEnabled(active);url->setText(webControl->url());url->setCursorPosition(0);
    status->setText(active ? uiTr("Сервер работает на %1:%2").arg(webControl->address().toString()).arg(webControl->port())
        : webControl->errorString().isEmpty() ? uiTr("Сервер выключен") : uiTr("Не удалось запустить сервер: %1").arg(webControl->errorString()));
    if (active && property("foxicmpTemporaryWebPort").toBool())
      status->setText(status->text() + "\n" + uiTr("Порт задан командой запуска; сохранённые настройки не изменены."));
  };
  connect(this, &Window::webControlChanged, dialog, refresh);
  connect(start,&QPushButton::clicked,dialog,[=] {
    auto address = lan->isChecked() ? QHostAddress(addresses->currentData().toString()) : QHostAddress(QHostAddress::LocalHost);
    if (lan->isChecked() && !WebControl::isPrivateAddress(address)) address = QHostAddress();
    QString error;
    if (!replaceWebControl(quint16(port->value()), address, &error)) {
      status->setText(uiTr("Не удалось запустить сервер: %1").arg(error));
      return;
    }
    setProperty("foxicmpTemporaryWebPort", false);
    profile.settings.setValue("web/enabled", true);
    profile.settings.setValue("web/port", port->value());
    profile.settings.setValue("web/lanEnabled", lan->isChecked());
    if (lan->isChecked()) profile.settings.setValue("web/lanAddress", address.toString());
    profile.settings.sync();
    emit webControlChanged();
    refresh();
    if (profile.settings.status()!=QSettings::NoError) status->setText(uiTr("Не удалось сохранить параметры в профиль."));
  });
  connect(stop,&QPushButton::clicked,dialog,[=] {
    setProperty("foxicmpTemporaryWebPort", false);
    webControl->stop();profile.settings.setValue("web/enabled",false);profile.settings.sync();refresh();
    if (profile.settings.status()!=QSettings::NoError) status->setText(uiTr("Не удалось сохранить параметры в профиль."));
  });
  connect(open,&QPushButton::clicked,dialog,[=] { QDesktopServices::openUrl(QUrl(webControl->url())); });
  connect(copy,&QPushButton::clicked,dialog,[=] { QApplication::clipboard()->setText(webControl->url()); });
  connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);
  refresh(); dialog->show();
}
