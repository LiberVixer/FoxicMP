#include "devicesource.h"
#include "i18n.h"
#include "player.h"
#include "playlistpanel.h"
#include "window.h"
#include <QtWidgets>

void Window::applyAudioSourceOptions() {
  QStringList paths;
  for (const auto &line :
       profile.settings.value("audio/searchPaths").toString().split('\n')) {
    const auto path = line.trimmed();
    if (!path.isEmpty() && paths.size() < 32)
      paths.append(path);
  }
  const QStringList modes{"no", "exact", "fuzzy", "all"};
  auto mode = profile.settings.value("audio/autoLoad", "no").toString();
  if (!modes.contains(mode))
    mode = "no";
  if (!player->QObject::property("foxicmpDefaultAudioExtensions").isValid())
    player->setProperty("foxicmpDefaultAudioExtensions",
                        player->get("audio-file-auto-exts"));
  QStringList extensions;
  for (const auto &part :
       profile.settings.value("audio/extensions")
           .toString()
           .split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts))
    if (QRegularExpression("^[A-Za-z0-9]{1,12}$").match(part).hasMatch() &&
        extensions.size() < 64)
      extensions.append(part.toLower());
  extensions.removeDuplicates();
  if (extensions.isEmpty())
    extensions = player->QObject::property("foxicmpDefaultAudioExtensions")
                     .toStringList();
  player->setList("audio-file-paths", paths);
  player->setList("audio-file-auto-exts", extensions);
  player->set("audio-file-auto",
              progressive || DeviceSource::isLive(current) ? "no" : mode);
  const auto stamp =
      paths.join('\n') + '\n' + mode + '\n' + extensions.join(',');
  if (!stopped && !progressive && !audioOptionsStamp.isEmpty() &&
      stamp != audioOptionsStamp)
    player->command({"rescan-external-files", "keep-selection"});
  audioOptionsStamp = stamp;
  enforceAudioPolicy();
}
void Window::enforceAudioPolicy() {
  if (stopped || progressive || audioUserSelection)
    return;
  const auto data = currentItem
                        ? currentItem->data(PlaylistPanel::Metadata).toMap()
                        : QVariantMap{};
  if (profile.settings.value("playlist/rememberTracks", true).toBool() &&
      (data.contains("aid") || data.contains("selectedAudioFile")))
    return;
  const auto policy =
      profile.settings.value("audio/selectionPolicy", "native").toString();
  if (policy != "external" && policy != "internal")
    return;
  const bool external = policy == "external";
  const auto tracks = player->get("track-list").toList();
  const auto selected = player->get("aid").toString();
  const auto languages = profile.settings.value("audio/languages")
                             .toString()
                             .split(',', Qt::SkipEmptyParts);
  int best = 100000;
  QString choice;
  for (const auto &entry : tracks) {
    const auto track = entry.toMap();
    if (track.value("type") != "audio" ||
        track.value("external").toBool() != external)
      continue;
    if (track.value("id").toString() == selected)
      return; // Keep libmpv's language/default choice within the preferred
              // group.
    const auto language = track.value("lang").toString().toLower();
    int score = 1000 + (track.value("default").toBool() ? 0 : 1);
    for (int i = 0; i < languages.size(); ++i) {
      const auto wanted = languages[i].trimmed().toLower();
      const auto locale = QLocale(wanted).language();
      if (language == wanted ||
          (locale != QLocale::C && QLocale(language).language() == locale)) {
        score = i;
        break;
      }
    }
    if (score < best) {
      best = score;
      choice = track.value("id").toString();
    }
  }
  if (!choice.isEmpty())
    player->set("aid", choice);
}
void Window::attachAudioSourcePreferences(QTabWidget *tabs,
                                          QList<std::function<void()>> *apply) {
  auto *content = new QWidget;
  auto *form = new QFormLayout(content);
  auto *scroll = new QScrollArea;
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setWidget(content);
  tabs->addTab(scroll, uiTr("Звук — файлы и выходы"));
  auto *mode = new QComboBox;
  mode->setObjectName("audio/autoLoad");
  for (const auto &entry : QList<QPair<QString, QString>>{
           {"no", uiTr("Не искать автоматически")},
           {"exact", uiTr("Точное имя видео")},
           {"fuzzy", uiTr("Имя видео входит в имя аудио")},
           {"all", uiTr("Все аудиофайлы в папках поиска")}})
    mode->addItem(entry.second, entry.first);
  mode->setCurrentIndex(std::max(
      0, mode->findData(profile.settings.value("audio/autoLoad", "no"))));
  form->addRow(uiTr("Поиск внешнего аудио:"), mode);
  auto *paths = new QPlainTextEdit(
      profile.settings.value("audio/searchPaths").toString());
  paths->setObjectName("audio/searchPaths");
  paths->setMaximumHeight(90);
  form->addRow(uiTr("Папки аудио (одна в строке, до 32):"), paths);
  auto *extensions =
      new QLineEdit(profile.settings.value("audio/extensions").toString());
  extensions->setObjectName("audio/extensions");
  extensions->setPlaceholderText("aac, ac3, dts, flac, m4a, mp3, ogg, wav");
  form->addRow(uiTr("Расширения (пусто — список libmpv):"), extensions);
  auto *policy = new QComboBox;
  policy->setObjectName("audio/selectionPolicy");
  policy->addItem(uiTr("Автовыбор libmpv"), "native");
  policy->addItem(uiTr("Предпочитать внешние дорожки"), "external");
  policy->addItem(uiTr("Предпочитать встроенные дорожки"), "internal");
  policy->setCurrentIndex(std::max(0, policy->findData(profile.settings.value(
                                          "audio/selectionPolicy", "native"))));
  form->addRow(uiTr("Приоритет аудио:"), policy);
  auto *note = new QLabel(
      uiTr("Относительные папки считаются от видео. При изменении поиска новые "
           "дорожки добавляются без удаления уже открытых. "
           "Ручной выбор и запомненная дорожка важнее приоритета. Приоритет не "
           "переключает звук обратно после выбора клавишей или мышью. "
           "В прогрессивном просмотре дополнительный поиск и приоритет не "
           "применяются."));
  note->setWordWrap(true);
  form->addRow(note);
  auto *dual = new QCheckBox(uiTr("Дублировать звук на второй выход"));
  dual->setObjectName("audio/dualOutput");
  dual->setChecked(profile.settings.value("audio/dualOutput", false).toBool());
  form->addRow(dual);
  auto *second = new QComboBox;
  second->setObjectName("audio/secondDevice");
  second->addItem(uiTr("Выберите второй выход"), "");
  for (const auto &entry : player->get("audio-device-list").toList()) {
    const auto device = entry.toMap();
    const auto name = device.value("name").toString();
    if (
#ifdef Q_OS_WIN
        name.startsWith("wasapi/")
#else
        name.startsWith("pulse/") && !name.startsWith("pulse/foxicmp_dual_")
#endif
       )
      second->addItem(device.value("description").toString(), name);
  }
  const auto saved = profile.settings.value("audio/secondDevice").toString();
  int index = second->findData(saved);
  if (index < 0) {
    second->addItem(saved + uiTr(" (сейчас недоступно)"), saved);
    index = second->count() - 1;
  }
  second->setCurrentIndex(index);
  form->addRow(uiTr("Второй выход:"), second);
  auto *routeNote = new QLabel(
#ifdef Q_OS_WIN
      uiTr("Второй выход WASAPI воспроизводит выбранную аудиодорожку отдельным "
           "декодером, следующем времени основного. Пауза, громкость, задержка "
           "и обработка звука связаны. При недоступности второго основной "
           "продолжает работу. Bitstream и одноразовый stdin не дублируются.")
#else
      uiTr("Основной выход задаётся в поле «Устройство». Два разных выхода "
           "PulseAudio/PipeWire получают один и тот же декодированный PCM. "
           "Создаётся временное объединённое устройство; системный выход по "
           "умолчанию и громкость других программ не меняются. "
           "Без доступного второго устройства остаётся основной. Синхронизацию "
           "выходов выполняет звуковой сервер; "
           "точность на Bluetooth/USB/HDMI требует проверки. Для bitstream "
           "этот способ не предназначен.")
#endif
      );
  routeNote->setWordWrap(true);
  routeNote->setObjectName("settingsAudioRouteHelp");
  form->addRow(routeNote);
  auto *status = new QLabel;
  status->setWordWrap(true);
  status->setObjectName("audioOutputStatus");
  const auto update = [this, status] {
    status->setText(
        player->QObject::property("foxicmpAudioOutputStatus").toString());
  };
  update();
  auto *timer = new QTimer(status);
  timer->setInterval(500);
  connect(timer, &QTimer::timeout, status, update);
  timer->start();
  form->addRow(status);
  apply->append([this, mode, paths, extensions, policy, dual, second] {
    profile.settings.setValue("audio/autoLoad", mode->currentData());
    profile.settings.setValue("audio/searchPaths", paths->toPlainText());
    profile.settings.setValue("audio/extensions", extensions->text().trimmed());
    profile.settings.setValue("audio/selectionPolicy", policy->currentData());
    profile.settings.setValue("audio/dualOutput", dual->isChecked());
    profile.settings.setValue("audio/secondDevice", second->currentData());
  });
}
