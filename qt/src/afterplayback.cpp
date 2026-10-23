#include "i18n.h"
#include "mediasource.h"
#include "player.h"
#include "playlistpanel.h"
#include "systemactions.h"
#include "window.h"
#include <QtWidgets>

void Window::buildAfterPlaybackMenu(QMenu *playback) {
  afterMenu = playback->addMenu(uiTr("После воспроизведения"));
  afterMenu->setObjectName("afterPlaybackMenu");
  auto *once = afterMenu->addMenu(uiTr("Один раз — после окончания списка"));
  auto *always =
      afterMenu->addMenu(uiTr("Каждый раз — после окончания списка"));
  const QList<QPair<QString, QString>> common{
      {"none", uiTr("Ничего не делать")}, {"exit", uiTr("Закрыть программу")}};
  auto add = [this](QMenu *menu, QActionGroup *group, const QString &id,
                    const QString &label, bool single) {
    auto *a = menu->addAction(label);
    a->setObjectName((single ? "afterOnce_" : "afterAlways_") + id);
    a->setCheckable(true);
    group->addAction(a);
    connect(a, &QAction::triggered, this, [this, id, single] {
      if (single)
        onceAfterPlayback = id == "none" ? QString() : id;
      else
        profile.settings.setValue("playback/after", id);
      showOsd(single
                  ? uiTr("Однократное действие после воспроизведения изменено")
                  : uiTr("Действие после воспроизведения сохранено"));
      refreshAfterPlaybackMenu();
    });
  };
  auto *onceGroup = new QActionGroup(this),
       *alwaysGroup = new QActionGroup(this);
  for (const auto &entry : common) {
    add(once, onceGroup, entry.first, entry.second, true);
    add(always, alwaysGroup, entry.first, entry.second, false);
  }
  for (const auto &entry :
       QList<QPair<QString, QString>>{{"suspend", uiTr("Сон")},
                                      {"hibernate", uiTr("Гибернация")},
                                      {"shutdown", uiTr("Выключить компьютер")},
                                      {"logout", uiTr("Завершить сеанс")},
                                      {"lock", uiTr("Блокировать компьютер")}})
    add(once, onceGroup, entry.first, entry.second, true);
  for (const auto &entry : QList<QPair<QString, QString>>{
           {"close", uiTr("Закрыть файл")},
           {"minimize", uiTr("Закрыть файл и свернуть")},
           {"nextDirectory", uiTr("Следующий файл в каталоге")},
           {"loopDirectory", uiTr("Следующий файл в каталоге (зациклить)")}}) {
    add(always, alwaysGroup, entry.first, entry.second, false);
    add(once, onceGroup, entry.first, entry.second, true);
  }
  auto *note =
      afterMenu->addAction(uiTr("Действие выполняется до повтора списка"));
  note->setEnabled(false);
  connect(afterMenu, &QMenu::aboutToShow, this,
          &Window::refreshAfterPlaybackMenu);
  refreshAfterPlaybackMenu();
}
void Window::refreshAfterPlaybackMenu() {
  const auto saved =
      profile.settings.value("playback/after", "none").toString();
  for (auto *a : afterMenu->findChildren<QAction *>()) {
    const QString name = a->objectName();
    if (name.startsWith("afterOnce_"))
      a->setChecked(name.mid(10) ==
                    (onceAfterPlayback.isEmpty() ? "none" : onceAfterPlayback));
    if (name.startsWith("afterAlways_"))
      a->setChecked(name.mid(12) == saved);
  }
}
void Window::finishPlayback() {
  if (!completionArmed)
    return;
  completionArmed = false;
  profile.savePosition(current, duration, duration);
  stopped = true;
  const bool end = stdinPlayback || playlistPassEnded();
  ++filePlays;
  if (end)
    ++playlistPlays;
  QString action =
      onceAfterPlayback.isEmpty()
          ? profile.settings.value("playback/after", "none").toString()
          : onceAfterPlayback;
  const QSet<QString> allowed = {"none",     "exit",          "close",
                                 "minimize", "nextDirectory", "loopDirectory",
                                 "suspend",  "hibernate",     "shutdown",
                                 "logout",   "lock"};
  if (!allowed.contains(action))
    action = "none";
  if (end && action == "none" && !onceAfterPlayback.isEmpty()) {
    onceAfterPlayback.clear();
    refreshAfterPlaybackMenu();
  }
  if (!end || action == "none") {
    if (stdinPlayback || !advancePlaylist(true, true)) {
      exitFullscreenAtEnd();
      if (profile.settings.value("playback/rewindAtEnd", false).toBool() &&
          currentItem && !stdinPlayback)
        playIndex(playlist->row(currentItem), 0, false, false, true);
    }
    return;
  }
  exitFullscreenAtEnd();
  onceAfterPlayback.clear();
  refreshAfterPlaybackMenu();
  if (action == "exit") {
    exitApplication();
    return;
  }
  if (action == "close" || action == "minimize") {
    closePlaybackFile();
    if (action == "minimize")
      showMinimized();
    return;
  }
  if (action == "nextDirectory" || action == "loopDirectory") {
    if (!nextInDirectory(action == "loopDirectory"))
      showOsd(uiTr("В каталоге больше нет медиафайлов"));
    return;
  }
  if (!SystemActions::requests(action).isEmpty()) {
    profile.settings.sync();
    if (systemActionHandler)
      systemActionHandler(action);
    else
      SystemActions::execute(action, this, [this](const QString &error) {
        if (!error.isEmpty()) {
          state->setText(uiTr("Действие после воспроизведения: ") + error);
          state->setToolTip(error);
          showOsd(uiTr("Не удалось выполнить действие после воспроизведения"));
        }
      });
  }
}
void Window::closePlaybackFile() {
  cancelMouseHold();
  ++playbackGeneration;
  clearAbLoop();
  pausedByMinimize = false;
  completionArmed = false;
  stdinPlayback = false;
  releaseProgressive();
  if (isFullScreen())
    toggleFullscreen();
  player->command({"stop"});
  currentItem = nullptr;
  currentId.clear();
  current.clear();
  currentIndex = -1;
  pendingResume = -1;
  stopped = true;
  restoreClosedGeometry();
  titleMetadata.clear();
  hoverStatus.clear();
  updateClock();
  syncPlaylist();
}
bool Window::nextInDirectory(bool loop) {
  const QFileInfo currentFile(current);
  if (!currentFile.isFile())
    return false;
  const QStringList files =
      MediaSource::filesInDirectory(currentFile.absolutePath());
  const int index = files.indexOf(currentFile.absoluteFilePath());
  if (index < 0 || files.size() < 2)
    return false;
  int next = index + 1;
  if (next >= files.size()) {
    if (!loop)
      return false;
    next = 0;
  }
  // Retain the queue and reuse an existing entry if it is already present.
  for (int i = 0; i < playlist->count(); ++i)
    if (playlist->item(i)->data(Qt::UserRole).toString() == files[next]) {
      playIndex(i, 0);
      return true;
    }
  queuePanel->append(playlist, {files[next]});
  playIndex(playlist->count() - 1, 0);
  return true;
}
