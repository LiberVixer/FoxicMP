#include "devicesource.h"
#include "i18n.h"
#include "player.h"
#include "playlistpanel.h"
#include "subtitleeditor.h"
#include "subtitlestyle.h"
#include "window.h"
#include <QtWidgets>

bool Window::subtitleTrackAllowed(const QVariantMap &track) const {
  return track.value("type").toString() != "sub" ||
         !profile.settings.value("subtitles/blockEmbedded", false).toBool() ||
         track.value("external").toBool();
}
QVariantMap Window::selectedSubtitleTrack() const {
  const auto id = player->get("sid").toString();
  for (const auto &entry : player->get("track-list").toList()) {
    const auto track = entry.toMap();
    if (track.value("type").toString() == "sub" &&
        track.value("id").toString() == id)
      return track;
  }
  return {};
}
void Window::enforceSubtitlePolicy() {
  if (stopped)
    return;
  const auto selected = selectedSubtitleTrack();
  const auto metadata = currentItem
                            ? currentItem->data(PlaylistPanel::Metadata).toMap()
                            : QVariantMap{};
  const bool remembered =
      profile.settings.value("playlist/rememberTracks", true).toBool() &&
      (metadata.contains("sid") || metadata.contains("selectedSubtitleFile"));
  const bool blocked = !selected.isEmpty() && !subtitleTrackAllowed(selected);
  const bool prefer =
      !subtitleUserSelection && !remembered &&
      profile.settings.value("subtitles/preferExternal", false).toBool() &&
      !selected.value("external").toBool();
  if (blocked || prefer) {
    QString external;
    for (const auto &entry : player->get("track-list").toList()) {
      const auto track = entry.toMap();
      if (track.value("type").toString() == "sub" &&
          track.value("external").toBool()) {
        external = track.value("id").toString();
        break;
      }
    }
    if (!external.isEmpty())
      player->set("sid", external);
    else if (blocked)
      player->set("sid", "no");
  }
  if (profile.settings.value("subtitles/blockEmbedded", false).toBool()) {
    const auto secondary = player->get("secondary-sid").toString();
    for (const auto &entry : player->get("track-list").toList()) {
      const auto track = entry.toMap();
      if (track.value("id").toString() == secondary &&
          !subtitleTrackAllowed(track))
        player->set("secondary-sid", "no");
    }
  }
}
bool Window::loadExternalSubtitles(const QString &file, bool select) {
  if (stopped || !QFileInfo(file).isFile()) {
    showOsd(uiTr("Сначала откройте видео и выберите локальный файл субтитров"));
    return false;
  }
  const auto path = QFileInfo(file).absoluteFilePath();
  if (!player->addExternalTrack("sub", path, select))
    return false;
  if (select) {
    subtitleUserSelection = true;
    pendingTrackSelections.remove("sid");
  }
  if (currentItem &&
      profile.settings.value("playlist/rememberTracks", true).toBool()) {
    auto data = currentItem->data(PlaylistPanel::Metadata).toMap();
    auto files = data.value("subtitleFiles").toStringList();
    if (!files.contains(path))
      files.append(path);
    data["subtitleFiles"] = files;
    if (select) {
      data["selectedSubtitleFile"] = path;
      data.remove("sid");
    }
    currentItem->setData(PlaylistPanel::Metadata, data);
    syncPlaylist();
  }
  return true;
}
void Window::setupSubtitleTools() {
  auto *timer = new QTimer(this);
  timer->setInterval(750);
  connect(timer, &QTimer::timeout, this, &Window::watchSubtitleFile);
  timer->start();
}
void Window::applySubtitleOptions() {
  QStringList paths;
  for (const auto &line :
       profile.settings.value("subtitles/searchPaths").toString().split('\n')) {
    const auto path = line.trimmed();
    if (!path.isEmpty() && paths.size() < 32)
      paths << path;
  }
  const auto codepage =
      profile.settings.value("subtitles/codepage", "auto").toString();
  const QStringList encodings{"auto",     "UTF-8",    "CP1251",    "KOI8-R",
                              "UTF-16LE", "UTF-16BE", "ISO-8859-1"};
  const auto encoding =
      encodings.contains(codepage) ? codepage : QString("auto");
  player->setList("sub-file-paths", paths);
  player->set("sub-codepage", encoding);
  const auto stamp =
      paths.join('\n') + '\n' +
      QString::number(
          profile.settings.value("subtitles/autoLoad", true).toBool());
  if (!stopped && !subtitleOptionsStamp.isEmpty() &&
      stamp != subtitleOptionsStamp)
    player->command({"rescan-external-files", "keep-selection"});
  if (!stopped && !subtitleCodepage.isEmpty() && subtitleCodepage != encoding)
    reloadSubtitles();
  subtitleOptionsStamp = stamp;
  subtitleCodepage = encoding;
  enforceSubtitlePolicy();
  applySubtitleStyle();
}
void Window::reloadSubtitles() {
  const auto track = selectedSubtitleTrack();
  if (!track.value("external").toBool()) {
    showOsd(uiTr("Перечитать можно выбранные внешние субтитры"));
    return;
  }
  player->command({"sub-reload", track.value("id").toString()});
}
void Window::watchSubtitleFile() {
  if (stopped ||
      !profile.settings.value("subtitles/autoReload", false).toBool()) {
    watchedSubtitle.clear();
    watchedSubtitleHash.clear();
    pendingSubtitleHash.clear();
    return;
  }
  const auto track = selectedSubtitleTrack();
  const auto path = track.value("external-filename").toString();
  if (!track.value("external").toBool() || !QFileInfo(path).isFile())
    return;
  QFile file(path);
  if (file.size() > 16 * 1024 * 1024 || !file.open(QIODevice::ReadOnly))
    return;
  const auto bytes = file.readAll();
  if (bytes.isEmpty())
    return;
  const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
  if (path != watchedSubtitle) {
    watchedSubtitle = path;
    watchedSubtitleHash = hash;
    pendingSubtitleHash.clear();
    return;
  }
  if (hash == watchedSubtitleHash) {
    pendingSubtitleHash.clear();
    return;
  }
  if (hash != pendingSubtitleHash) {
    pendingSubtitleHash = hash;
    return;
  }
  watchedSubtitleHash = hash;
  pendingSubtitleHash.clear();
  reloadSubtitles();
}
void Window::buildSubtitleActions() {
  const auto add = [this](const QString &id, const QString &title,
                          const QString &key, std::function<void()> fn) {
    auto *action = new QAction(title, this);
    action->setObjectName("action_" + id);
    actions[id] = action;
    defaultKeys[id] = key;
    addAction(action);
    connect(action, &QAction::triggered, this, [this, fn] {
      cancelMouseHold();
      heldSeekCommand.clear();
      fn();
    });
  };
  add("subtitleEditor", uiTr("Редактор и синхронизация субтитров…"),
      "Ctrl+Alt+R", [this] { subtitleEditorDialog(); });
  add("subtitleStyleSettings", uiTr("Оформление и рендер субтитров…"),
      "Ctrl+Alt+T", [this] { subtitleStyleDialog(); });
  add("subtitleReload", uiTr("Перечитать субтитры"), "",
      [this] { reloadSubtitles(); });
  add("subtitleCopy", uiTr("Копировать текущую реплику"), "", [this] {
    const auto text = player->get("sub-text").toString();
    if (text.isEmpty())
      showOsd(uiTr("Сейчас нет текстовой реплики"));
    else
      QApplication::clipboard()->setText(text);
  });
  for (int direction : {-1, 1}) {
    add(direction > 0 ? "subtitleCueNext" : "subtitleCuePrevious",
        direction > 0 ? uiTr("К следующей реплике")
                      : uiTr("К предыдущей реплике"),
        direction > 0 ? "Ctrl+Alt+PgDown" : "Ctrl+Alt+PgUp", [this, direction] {
          if (stopped || DeviceSource::isLive(current) || progressive) {
            showOsd(uiTr("Переход по репликам недоступен для этого источника"));
            return;
          }
          player->command({"sub-seek", QString::number(direction)});
        });
    add(direction > 0 ? "subtitleSyncNext" : "subtitleSyncPrevious",
        direction > 0 ? uiTr("Следующая реплика — сейчас")
                      : uiTr("Предыдущая реплика — сейчас"),
        "", [this, direction] {
          if (!stopped)
            player->command({"sub-step", QString::number(direction)});
        });
  }
  add("subtitleExternalEditor", uiTr("Открыть субтитры во внешнем редакторе"),
      "", [this] {
        const auto track = selectedSubtitleTrack();
        const auto path = track.value("external-filename").toString();
        const auto program = profile.settings.value("subtitles/editorProgram")
                                 .toString()
                                 .trimmed();
        if (!track.value("external").toBool() || !QFileInfo(path).isFile() ||
            program.isEmpty() || !QProcess::startDetached(program, {path}))
          showOsd(uiTr("Укажите программу редактора в настройках и выберите "
                       "внешние субтитры"));
      });
}
void Window::subtitleEditorDialog() {
  if (progressive || DeviceSource::isLive(current)) {
    showOsd(uiTr("Синхронизация в редакторе недоступна для этого источника"));
    return;
  }
  const auto track = selectedSubtitleTrack();
  if (stopped || track.isEmpty()) {
    showOsd(uiTr("Сначала выберите текстовую дорожку субтитров"));
    return;
  }
  QString path = track.value("external-filename").toString();
  const auto generation = playbackGeneration;
  bool extracted = false;
  if (!track.value("external").toBool()) {
    if (!QFileInfo(current).isFile() || !track.contains("ff-index")) {
      showOsd(
          uiTr("Для редактора встроенной дорожки нужен локальный медиафайл"));
      return;
    }
    QDir folder(profile.directory + "/Subtitles/Extracted");
    if (!folder.mkpath("."))
      return;
    path = folder.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) +
                           ".srt");
    QProcess ffmpeg;
    QProgressDialog progress(uiTr("Извлечение текстовой дорожки в SRT…"),
                             uiTr("Отмена"), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    connect(&progress, &QProgressDialog::canceled, &ffmpeg, &QProcess::kill);
    connect(&ffmpeg, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            &progress, [&progress] { progress.accept(); });
    connect(&ffmpeg, &QProcess::errorOccurred, &progress,
            [&progress] { progress.reject(); });
    QTimer::singleShot(20000, &ffmpeg, &QProcess::kill);
    ffmpeg.start("ffmpeg", {"-y", "-nostdin", "-v", "error", "-i", current,
                            "-map", "0:" + track.value("ff-index").toString(),
                            "-c:s", "srt", path});
    progress.exec();
    if (ffmpeg.state() != QProcess::NotRunning) {
      ffmpeg.kill();
      ffmpeg.waitForFinished(1000);
    }
    if (progress.wasCanceled() || ffmpeg.exitStatus() != QProcess::NormalExit ||
        ffmpeg.exitCode() || !QFileInfo(path).isFile()) {
      QFile::remove(path);
      showOsd(uiTr("Дорожка не извлечена: нужен FFmpeg и текстовые субтитры, "
                   "не изображение"));
      return;
    }
    extracted = true;
  }
  SubtitleDocument document;
  if (playbackGeneration != generation) {
    if (extracted)
      QFile::remove(path);
    showOsd(uiTr("Источник сменился: откройте редактор для новой дорожки"));
    return;
  }
  QString error;
  if (!document.load(path, extracted ? "UTF-8" : subtitleCodepage, &error)) {
    if (extracted)
      QFile::remove(path);
    showOsd(error);
    return;
  }
  SubtitleEditor dialog(
      document, path, profile.directory,
      [this] { return player->get("time-pos").toDouble(); },
      [this, generation](double seconds) {
        if (playbackGeneration == generation)
          seekTo(seconds + player->get("sub-delay").toDouble());
      },
      this,
      [this](const SubtitleDocument &document, QString *error) {
        return SubtitleStyle::assCopy(document, profile.settings, error);
      });
  connect(&dialog, &SubtitleEditor::applyRequested, this,
          [this, generation](const QString &file) {
            if (playbackGeneration != generation) {
              showOsd(uiTr("Источник сменился: откройте сохранённую копию "
                           "субтитров вручную"));
              return;
            }
            if (loadExternalSubtitles(file))
              player->set("sub-delay", 0);
          });
  dialog.exec();
  if (extracted)
    QFile::remove(path);
}
void Window::attachSubtitlePreferences(QTabWidget *tabs,
                                       QList<std::function<void()>> *apply) {
  auto *page = new QWidget;
  auto *form = new QFormLayout(page);
  auto *scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setWidget(page);
  tabs->addTab(scroll, uiTr("Субтитры — файлы"));
  for (const auto &entry : QList<QPair<QString, QString>>{
           {"preferExternal",
            uiTr("Предпочитать внешние субтитры при открытии")},
           {"blockEmbedded", uiTr("Запретить встроенные субтитры")},
           {"autoReload",
            uiTr("Перечитывать выбранный внешний файл при изменении")}}) {
    auto *box = new QCheckBox(entry.second);
    const auto key = "subtitles/" + entry.first;
    box->setObjectName(key);
    box->setChecked(profile.settings.value(key, false).toBool());
    form->addRow(box);
    apply->append(
        [this, box, key] { profile.settings.setValue(key, box->isChecked()); });
  }
  auto *paths = new QPlainTextEdit(
      profile.settings.value("subtitles/searchPaths").toString());
  paths->setObjectName("subtitles/searchPaths");
  paths->setMaximumHeight(140);
  form->addRow(uiTr("Папки поиска (одна в строке, до 32):"), paths);
  auto *note =
      new QLabel(uiTr("Относительные папки считаются от каталога видео. "
                      "Автопоиск включается на вкладке «Субтитры». "
                      "Явный выбор и запомненная дорожка важнее предпочтения "
                      "внешних; запрет встроенных действует всегда. "
                      "Автоперечитывание ждёт два одинаковых снимка файла, "
                      "чтобы не читать незавершённую запись."));
  note->setWordWrap(true);
  form->addRow(note);
  auto *codepage = new QComboBox;
  codepage->setObjectName("subtitles/codepage");
  codepage->addItems({"auto", "UTF-8", "CP1251", "KOI8-R", "UTF-16LE",
                      "UTF-16BE", "ISO-8859-1"});
  codepage->setCurrentText(
      profile.settings.value("subtitles/codepage", "auto").toString());
  form->addRow(uiTr("Кодировка внешних субтитров:"), codepage);
  auto *encodingNote = new QLabel(
      uiTr("Авто в редакторе: BOM UTF-16, затем UTF-8, иначе Windows-1251. "
           "Для иной старой кодировки выберите её явно. Встроенный редактор "
           "поддерживает SRT, ASS/SSA и простой WebVTT; "
           "из встроенной текстовой дорожки извлекается SRT без авторского "
           "оформления."));
  encodingNote->setWordWrap(true);
  form->addRow(encodingNote);
  auto *program = new QLineEdit(
      profile.settings.value("subtitles/editorProgram").toString());
  program->setObjectName("subtitles/editorProgram");
  program->setPlaceholderText("/usr/bin/xed");
  form->addRow(uiTr("Программа внешнего редактора (без аргументов):"), program);
  apply->append([this, paths, codepage, program] {
    profile.settings.setValue("subtitles/searchPaths", paths->toPlainText());
    profile.settings.setValue("subtitles/codepage", codepage->currentText());
    profile.settings.setValue("subtitles/editorProgram",
                              program->text().trimmed());
  });
  attachSubtitleStylePreferences(tabs, apply);
}
