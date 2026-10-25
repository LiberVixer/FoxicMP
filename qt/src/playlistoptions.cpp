#include "i18n.h"
#include "mediasource.h"
#include "player.h"
#include "playlistpanel.h"
#include "profile.h"
#include "timeline.h"
#include "window.h"
#include <QProcess>
#include <QtWidgets>

void PlaylistPanel::applyOptions() {
  if (!profile.settings.value("playlist/determineDuration", true).toBool()) {
    probes.clear();
    if (probe) {
      probe->disconnect(this);
      probe->kill();
      probe->deleteLater();
      probe = nullptr;
    }
  } else {
    probes.clear();
    for (auto *list : lists())
      for (int i = 0; i < list->count(); ++i)
        queueProbe(list, list->item(i));
  }
}
void Window::rememberTrack(const char *name, const QString &selection) {
  if (QString::fromLatin1(name) == "sid") {
    for (const auto &entry : player->get("track-list").toList()) {
      const auto track = entry.toMap();
      if (track.value("type").toString() == "sub" &&
          track.value("id").toString() == selection &&
          !subtitleTrackAllowed(track))
        return;
    }
    subtitleUserSelection = true;
  }
  if (QString::fromLatin1(name) == "aid")
    audioUserSelection = true;
  pendingTrackSelections.remove(QString::fromLatin1(name));
  player->set(name, selection);
  if (!currentItem || progressive || stopped ||
      !profile.settings.value("playlist/rememberTracks", true).toBool())
    return;
  auto data = currentItem->data(PlaylistPanel::Metadata).toMap();
  data[QString::fromLatin1(name)] = selection;
  const QString externalKey =
      QString(name) == "aid" ? "selectedAudioFile" : "selectedSubtitleFile";
  data.remove(externalKey);
  for (const auto &v : player->get("track-list").toList()) {
    const auto track = v.toMap();
    if (track.value("type").toString() ==
            (QString(name) == "aid" ? "audio" : "sub") &&
        track.value("id").toString() == selection &&
        track.value("external").toBool())
      data[externalKey] = track.value("external-filename").toString();
  }
  currentItem->setData(PlaylistPanel::Metadata, data);
  syncPlaylist();
}
void Window::restorePlaylistTracks() {
  if (stopped || progressive || !currentItem)
    return;
  const auto tracks = player->get("track-list").toList();
  for (const auto &key : {"aid", "sid"}) {
    if (!pendingTrackSelections.contains(key))
      continue;
    const auto selection = pendingTrackSelections.value(key).toString();
    bool found = selection == "no" || selection == "auto";
    for (const auto &track : tracks) {
      const auto data = track.toMap();
      if (data.value("type").toString() ==
              (QString(key) == "aid" ? "audio" : "sub") &&
          data.value("id").toString() == selection)
        found = true;
    }
    if (found) {
      pendingTrackSelections.remove(key);
      player->set(key, selection);
    }
  }
}
void Window::updatePlaylistChapters() {
  const auto chapters = !progressive && currentItem
                            ? currentItem->data(PlaylistPanel::Metadata)
                                  .toMap()
                                  .value("chapters")
                                  .toList()
                            : QVariantList{};
  timeline->chapters =
      chapters.isEmpty() ? player->get("chapter-list").toList() : chapters;
  timeline->update();
  refreshChapters();
}
bool Window::changeChapter(int direction, double backThreshold) {
  const double now = player->get("time-pos").toDouble();
  double target = -1;
  if (direction > 0) {
    for (const auto &chapter : timeline->chapters) {
      const double time = chapter.toMap().value("time").toDouble();
      if (time > now + 1e-7) {
        target = time;
        break;
      }
    }
  } else {
    for (const auto &chapter : timeline->chapters) {
      const double time = chapter.toMap().value("time").toDouble();
      if (time <= now - backThreshold)
        target = time;
    }
  }
  if (target < 0) return false;
  seekTo(target); return true;
}
void Window::handlePlaylistFailure() {
  completionArmed = false;
  if (progressive || !currentItem)
    return;
  currentItem->setData(PlaylistPanel::Invalid, true);
  currentItem->setForeground(QColor(210, 140, 130));
  currentItem->setToolTip(current + uiTr("\nОшибка: ") + lastError);
  if (!profile.settings.value("playlist/advanceOnError", false).toBool())
    return;
  failedEntries.insert(currentId);
  const auto generation = playbackGeneration;
  const auto failedId = currentId;
  QTimer::singleShot(0, this, [this, generation, failedId] {
    if (generation != playbackGeneration || currentId != failedId ||
        progressive)
      return;
    // One traversal only, regardless of repeat/shuffle. A successful load
    // resets it.
    QList<int> candidates;
    const bool shuffle =
        profile.settings.value("playlist/shuffle", false).toBool();
    const bool wrap =
        profile.settings.value("playlist/repeat", "repeatNone").toString() ==
        "repeatAll";
    for (int step = 1; step <= playlist->count(); ++step) {
      const int row = currentIndex + step;
      if (!shuffle && !wrap && row >= playlist->count())
        break;
      const int index = row % playlist->count();
      const auto id = playlist->item(index)->data(PlaylistPanel::Id).toString();
      if (!playlistItemSkipped(index) && !failedEntries.contains(id) &&
          (!shuffle || !shuffled.contains(id)))
        candidates.append(index);
    }
    if (candidates.isEmpty()) {
      state->setText(
          uiTr("Очередь остановлена: нет следующих доступных записей"));
      return;
    }
    const int row =
        shuffle
            ? candidates[QRandomGenerator::global()->bounded(candidates.size())]
            : candidates.first();
    if (shuffle) {
      const auto id = playlist->item(row)->data(PlaylistPanel::Id).toString();
      shuffled.insert(id);
      while (shuffleTrail.size() > shuffleCursor + 1)
        shuffleTrail.removeLast();
      shuffleTrail.append(id);
      shuffleCursor = shuffleTrail.size() - 1;
    }
    advancingAfterError = true;
    playIndex(row, 0, false, true);
    advancingAfterError = false;
  });
}
bool Window::playlistItemSkipped(int row) const {
  return row >= 0 && row < playlist->count() &&
         profile.settings.value("playlist/skipInvalid", true).toBool() &&
         playlist->item(row)->data(PlaylistPanel::Invalid).toBool();
}
bool Window::playlistPassEnded() const {
  if (!currentItem)
    return false;
  const bool shuffle =
      profile.settings.value("playlist/shuffle", false).toBool();
  for (int row = 0; row < playlist->count(); ++row) {
    if (playlistItemSkipped(row))
      continue;
    if (!shuffle && row > playlist->row(currentItem))
      return false;
    if (shuffle) {
      const auto id = playlist->item(row)->data(PlaylistPanel::Id).toString();
      if (!shuffled.contains(id) ||
          shuffleTrail.mid(shuffleCursor + 1).contains(id))
        return false;
    }
  }
  return true;
}
void Window::setupPlaylistDock() {
  dock->setObjectName("playlistDock");
  dock->setAllowedAreas(Qt::AllDockWidgetAreas);
  auto *placement =
      queuePanel->operationsMenu()->addMenu(uiTr("Размещение панели"));
  auto *group = new QActionGroup(this);
  for (auto entry : QList<QPair<Qt::DockWidgetArea, QString>>{
           {Qt::LeftDockWidgetArea, uiTr("Слева")},
           {Qt::RightDockWidgetArea, uiTr("Справа")},
           {Qt::TopDockWidgetArea, uiTr("Сверху")},
           {Qt::BottomDockWidgetArea, uiTr("Снизу")}}) {
    auto *action =
        placement->addAction(entry.second, this, [this, area = entry.first] {
          dock->setFloating(false);
          addDockWidget(area, dock);
          dock->show();
        });
    action->setObjectName("action_playlistDock_" +
                          QString::number(int(entry.first)));
    action->setCheckable(true);
    group->addAction(action);
    connect(placement, &QMenu::aboutToShow, action,
            [this, action, area = entry.first] {
              action->setChecked(!dock->isFloating() &&
                                 dockWidgetArea(dock) == area);
            });
  }
  auto *floating = placement->addAction(uiTr("Отдельное окно"), this, [this] {
    dock->setFloating(true);
    dock->show();
  });
  floating->setObjectName("action_playlistDock_float");
  floating->setCheckable(true);
  group->addAction(floating);
  connect(placement, &QMenu::aboutToShow, floating,
          [this, floating] { floating->setChecked(dock->isFloating()); });
  if (profile.settings.value("playlist/rememberPanel", true).toBool()) {
    restoreState(profile.settings.value("playlist/dockState").toByteArray(), 1);
    if (dock->isFloating())
      dock->restoreGeometry(
          profile.settings.value("playlist/dockGeometry").toByteArray());
  }
}
void Window::attachPlaylistPreferences(QTabWidget *tabs,
                                       QList<std::function<void()>> *apply) {
  auto *page = new QWidget;
  auto *layout = new QVBoxLayout(page);
  QList<QPair<QString, QCheckBox *>> fields;
  const auto add = [&](const QString &key, const QString &text, bool fallback) {
    auto *check = new QCheckBox(text);
    check->setObjectName(key);
    check->setChecked(profile.settings.value(key, fallback).toBool());
    layout->addWidget(check);
    fields.append({key, check});
  };
  add("playlist/advanceOnError",
      uiTr("Переходить к следующей записи при ошибке воспроизведения"), false);
  add("playlist/skipInvalid",
      uiTr("Пропускать уже ошибочные записи при переходах"), true);
  add("playlist/skipInvalidSources",
      uiTr("Пропускать неверные источники при импорте плейлиста"), false);
  add("playlist/determineDuration",
      uiTr("Определять длительность локальных файлов в фоне"), true);
  add("playlist/hideFullscreen",
      uiTr("Скрывать панель при входе в полный экран"), true);
  add("playlist/rememberTracks",
      uiTr("Запоминать выбранные дорожки для каждой записи"), true);
  add("playlist/rememberPanel",
      uiTr("Сохранять размещение, размер и видимость панели"), true);
  auto *note = new QLabel(
      uiTr("Переход при ошибке не выполняет действия после просмотра и "
           "не зацикливает повреждённый список. Пропуск неверных адресов "
           "не исправляет повреждённую структуру файла. Дорожки "
           "сохраняются в профиле и при экспорте плейлиста."));
  note->setWordWrap(true);
  layout->addWidget(note);
  layout->addStretch();
  tabs->addTab(page, uiTr("Плейлист"));
  apply->append([this, fields] {
    for (const auto &field : fields)
      profile.settings.setValue(field.first, field.second->isChecked());
    queuePanel->applyOptions();
  });
}
