#include "devicesource.h"
#include "i18n.h"
#include "mediasource.h"
#include "player.h"
#include "playlistpanel.h"
#include "timeline.h"
#include "window.h"
#include <QtWidgets>
#include <algorithm>
namespace {
QString categoryName(const QString &id) {
  return id == "discs"     ? "DVD / Blu-ray / Audio CD"
         : id == "devices" ? uiTr("Устройства и ТВ")
                           : uiTr("Файлы и сеть");
}
QString displayPath(const QString &source) {
  DeviceSource::Request request;
  if (!DeviceSource::decode(source, &request))
    return source;
  if (request.kind == "dvb")
    return request.channel + " · " + request.channels;
  return request.path.isEmpty() ? MediaSource::displayName(source)
                                : request.path;
}
QComboBox *categoryFilter(const QString &name) {
  auto *combo = new QComboBox;
  combo->setObjectName(name);
  combo->addItem(uiTr("Все категории"), "all");
  for (const auto &id : {"files", "discs", "devices"})
    combo->addItem(categoryName(id), id);
  return combo;
}
void copySources(const QStringList &sources) {
  if (sources.isEmpty())
    return;
  auto *mime = new QMimeData;
  QList<QUrl> urls;
  for (const auto &source : sources)
    urls.append(MediaSource::isUrl(source) ? QUrl(source)
                                           : QUrl::fromLocalFile(source));
  mime->setText(sources.join('\n'));
  mime->setUrls(urls);
  QApplication::clipboard()->setMimeData(mime);
}
bool favoriteEditor(QWidget *parent, Favorite *entry, bool creating) {
  QDialog dialog(parent);
  dialog.setObjectName("favoriteEditor");
  dialog.setWindowTitle(creating ? uiTr("Добавить в избранное")
                                 : uiTr("Изменить избранное"));
  dialog.resize(620, 300);
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout;
  auto *title = new QLineEdit(entry->title);
  title->setObjectName("favoriteTitle");
  form->addRow(uiTr("Название:"), title);
  auto *category =
      new QLabel(categoryName(Profile::favoriteCategory(entry->file)));
  form->addRow(uiTr("Категория:"), category);
  auto *source = new QLabel(displayPath(entry->file));
  source->setWordWrap(true);
  source->setTextFormat(Qt::PlainText);
  source->setTextInteractionFlags(Qt::TextSelectableByMouse);
  form->addRow(uiTr("Источник:"), source);
  auto *remember = new QCheckBox(uiTr("Открывать с сохранённой позиции"));
  remember->setObjectName("favoriteRememberPosition");
  remember->setEnabled(Profile::canRememberFavoritePosition(entry->file));
  remember->setChecked(remember->isEnabled() && entry->rememberPosition);
  form->addRow(remember);
  auto *position = new QDoubleSpinBox;
  position->setObjectName("favoritePosition");
  position->setRange(0, 100000000);
  position->setDecimals(3);
  position->setSuffix(uiTr(" с"));
  position->setValue(entry->position);
  position->setEnabled(remember->isChecked());
  form->addRow(uiTr("Позиция:"), position);
  QObject::connect(remember, &QCheckBox::toggled, position,
                   &QWidget::setEnabled);
  auto *portable =
      new QCheckBox(uiTr("Хранить путь относительно папки программы"));
  portable->setObjectName("favoritePortable");
  portable->setEnabled(Profile::canUsePortablePath(entry->file));
  portable->setChecked(portable->isEnabled() && entry->portable);
  form->addRow(portable);
  layout->addLayout(form);
  auto *note = new QLabel(uiTr(
      "Если позиция отключена, избранное открывается с начала. Относительные "
      "пути работают при переносе программы вместе с медиа с сохранением "
      "расположения папок. Для устройств позиция и переносимый путь доступны "
      "только образам и папкам DVD/Blu-ray."));
  note->setWordWrap(true);
  layout->addWidget(note);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  const auto validate = [=] {
    buttons->button(QDialogButtonBox::Ok)
        ->setEnabled(!title->text().trimmed().isEmpty());
  };
  QObject::connect(title, &QLineEdit::textChanged, &dialog, validate);
  validate();
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted || title->text().trimmed().isEmpty())
    return false;
  entry->title = title->text().trimmed();
  entry->position = position->value();
  entry->rememberPosition = remember->isChecked();
  entry->portable = portable->isChecked();
  return true;
}
} // namespace
void Window::refreshLibraryMenus() {
  savePosition();
  historyMenu->clear();
  const auto recent = profile.recentFiles();
  const int limit = std::min(profile.historyMenuLimit(), int(recent.size()));
  for (int index = 0; index < limit; ++index) {
    const auto file = recent[index];
    const double position = profile.position(file);
    auto *a = historyMenu->addAction(
        MediaSource::displayName(file) +
            (position > 0 ? " — " + clockText(position) : ""),
        this, [this, file] { openFiles({file}); });
    a->setToolTip(displayPath(file));
    a->setData(file);
  }
  if (recent.isEmpty() || limit == 0) {
    auto *a = historyMenu->addAction(
        recent.isEmpty() ? uiTr("История пуста")
                         : uiTr("Записи скрыты настройкой размера меню"));
    a->setEnabled(false);
  }
  historyMenu->addSeparator();
  historyMenu->addAction(actions["historyManage"]);
  historyMenu
      ->addAction(uiTr("Очистить историю и позиции"), this,
                  [this] { profile.clearHistory(); })
      ->setObjectName("action_clearHistory");
  favoritesMenu->clear();
  favoritesMenu->addAction(actions["favoriteAdd"]);
  favoritesMenu->addAction(actions["favoriteManage"]);
  if (!profile.favorites().isEmpty()) favoritesMenu->addSeparator();
  QMap<QString, QMenu *> categories;
  for (const auto &entry : profile.favorites()) {
    const auto category = Profile::favoriteCategory(entry.file);
    QMenu *target = favoritesMenu;
    if (category != "files") {
      if (!categories.contains(category)) {
        auto *menu = favoritesMenu->addMenu(categoryName(category));
        menu->setObjectName("favoritesCategory_" + category);
        categories[category] = menu;
      }
      target = categories.value(category);
    }
    auto *a =
        target->addAction(entry.title + (entry.rememberPosition
                                             ? " — " + clockText(entry.position)
                                             : uiTr(" — сначала")),
                          this, [this, entry] { openFavorite(entry); });
    a->setToolTip(displayPath(entry.file));
    a->setData(entry.id);
  }
}
void Window::showHistory() {
  savePosition();
  QDialog dialog(this);
  dialog.setObjectName("historyDialog");
  dialog.setWindowTitle(uiTr("История воспроизведения"));
  dialog.resize(1000, 540);
  auto *layout = new QVBoxLayout(&dialog);
  auto *filters = new QHBoxLayout;
  auto *search = new QLineEdit;
  search->setObjectName("historySearch");
  search->setPlaceholderText(uiTr("Поиск по названию и пути"));
  search->setClearButtonEnabled(true);
  auto *category = categoryFilter("historyCategory");
  filters->addWidget(search, 1);
  filters->addWidget(category);
  layout->addLayout(filters);
  auto *list = new QTreeWidget;
  list->setObjectName("historyList");
  list->setHeaderLabels(
      {uiTr("Название"), uiTr("Путь / адрес"), uiTr("Позиция")});
  list->setRootIsDecorated(false);
  list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  list->setSortingEnabled(true);
  list->sortByColumn(-1, Qt::AscendingOrder);
  list->header()->setSectionResizeMode(0, QHeaderView::Interactive);
  list->setColumnWidth(0, 220);
  list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
  list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  layout->addWidget(list, 1);
  auto *summary = new QLabel;
  summary->setObjectName("historySummary");
  summary->setWordWrap(true);
  layout->addWidget(summary);
  const auto selected = [&] {
    QStringList files;
    for (int row = 0; row < list->topLevelItemCount(); ++row) {
      auto *item = list->topLevelItem(row);
      if (!item->isHidden() && item->isSelected())
        files.append(item->data(0, Qt::UserRole).toString());
    }
    return files;
  };
  const auto filter = [&] {
    int visible = 0;
    const auto needle = search->text().trimmed();
    for (int row = 0; row < list->topLevelItemCount(); ++row) {
      auto *item = list->topLevelItem(row);
      const auto file = item->data(0, Qt::UserRole).toString();
      const bool match = (category->currentData() == "all" ||
                          category->currentData().toString() ==
                              Profile::favoriteCategory(file)) &&
                         (needle.isEmpty() ||
                          item->text(0).contains(needle, Qt::CaseInsensitive) ||
                          file.contains(needle, Qt::CaseInsensitive) ||
                          item->text(1).contains(needle, Qt::CaseInsensitive));
      item->setHidden(!match);
      if (!match)
        item->setSelected(false);
      else
        ++visible;
    }
    summary->setText(
        QString(uiTr("Показано: %1 из %2. Удаление записи также "
                     "удаляет её позицию; файлы на диске сохраняются."))
            .arg(visible)
            .arg(list->topLevelItemCount()));
  };
  const auto fill = [&] {
    const auto selection = selected();
    const bool sorting = list->isSortingEnabled();
    list->setSortingEnabled(false);
    list->clear();
    for (const auto &file : profile.recentFiles()) {
      auto *item = new QTreeWidgetItem(
          list, {MediaSource::displayName(file), displayPath(file),
                 clockText(profile.position(file))});
      item->setData(0, Qt::UserRole, file);
      item->setToolTip(1, file);
      if (Profile::sourceMissing(file))
        item->setForeground(0, QColor(210, 140, 130));
      if (selection.contains(file))
        item->setSelected(true);
    }
    list->setSortingEnabled(sorting);
    filter();
  };
  connect(search, &QLineEdit::textChanged, &dialog, filter);
  connect(category, &QComboBox::currentIndexChanged, &dialog, filter);
  auto *row = new QHBoxLayout;
  layout->addLayout(row);
  const auto button = [&](const QString &id, const QString &text,
                          std::function<void()> fn) {
    auto *b = new QPushButton(text);
    b->setObjectName(id);
    b->setAutoDefault(false);
    row->addWidget(b);
    connect(b, &QPushButton::clicked, &dialog, std::move(fn));
    return b;
  };
  auto *open = button("historyOpen", uiTr("Открыть"), [&] {
    const auto files = selected();
    if (!files.isEmpty()) {
      openFiles(files);
      dialog.accept();
    }
  });
  button("historyOpenStart", uiTr("С начала"), [&] {
    auto *item = list->currentItem();
    if (!item || item->isHidden())
      return;
    const auto source = item->data(0, Qt::UserRole).toString();
    openFavorite(
        {{}, MediaSource::displayName(source), source, 0, false, false});
    dialog.accept();
  });
  button("historyCopy", uiTr("Копировать"), [&] { copySources(selected()); });
  auto *remove = button("historyRemove", uiTr("Удалить записи"), [&] {
    profile.removeRecent(selected());
    fill();
  });
  button("historyRemoveMissing", uiTr("Удалить отсутствующие"), [&] {
    QStringList missing;
    for (const auto &file : profile.recentFiles())
      if (Profile::sourceMissing(file))
        missing.append(file);
    profile.removeRecent(missing);
    fill();
  });
  button("historyClear", uiTr("Очистить всё"), [&] {
    profile.clearHistory();
    fill();
  });
  button("historyClose", uiTr("Закрыть"), [&] { dialog.reject(); });
  connect(list, &QTreeWidget::itemDoubleClicked, open,
          [open] { open->click(); });
  for (const auto &entry : QList<QPair<QKeySequence, std::function<void()>>>{
           {QKeySequence::Copy, [&] { copySources(selected()); }},
           {QKeySequence(Qt::Key_Delete), [remove] { remove->click(); }},
           {QKeySequence(Qt::Key_Return), [open] { open->click(); }}}) {
    auto *shortcut = new QShortcut(entry.first, list);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut, &QShortcut::activated, &dialog, entry.second);
  }
  fill();
  dialog.exec();
  for (auto *child : dialog.findChildren<QObject *>())
    QObject::disconnect(child, nullptr, &dialog, nullptr);
}
void Window::openFavorite(const Favorite &entry) {
  selectPlaybackList(queuePanel->activeList());
  const double start = entry.rememberPosition ? entry.position : 0;
  for (int i = 0; i < playlist->count(); ++i)
    if (playlist->item(i)->data(Qt::UserRole).toString() == entry.file) {
      playIndex(i, start);
      return;
    }
  addFiles({entry.file});
  playIndex(playlist->count() - 1, start);
}
void Window::addFavoriteDialog() {
  if (stopped || current.isEmpty() || progressive)
    return;
  Favorite entry{
      {},
      MediaSource::displayName(current),
      current,
      std::max(0., player->get("time-pos").toDouble()),
      profile.settings.value("favorites/rememberPosition", true).toBool(),
      profile.settings.value("favorites/portablePaths", false).toBool()};
  if (!player->get("seekable").toBool())
    entry.rememberPosition = false;
  if (!favoriteEditor(this, &entry, true))
    return;
  profile.addFavorite(entry.title, entry.file, entry.position,
                      entry.rememberPosition, entry.portable);
  if (Profile::canRememberFavoritePosition(entry.file))
    profile.settings.setValue("favorites/rememberPosition",
                              entry.rememberPosition);
  if (Profile::canUsePortablePath(entry.file))
    profile.settings.setValue("favorites/portablePaths", entry.portable);
}
void Window::manageFavorites() {
  QDialog dialog(this);
  dialog.setObjectName("favoritesDialog");
  dialog.setWindowTitle(uiTr("Избранное"));
  dialog.resize(900, 500);
  auto *layout = new QVBoxLayout(&dialog);
  auto *filters = new QHBoxLayout;
  auto *search = new QLineEdit;
  search->setObjectName("favoritesSearch");
  search->setClearButtonEnabled(true);
  search->setPlaceholderText(uiTr("Поиск по названию и пути"));
  auto *category = categoryFilter("favoritesCategory");
  filters->addWidget(search, 1);
  filters->addWidget(category);
  layout->addLayout(filters);
  auto *list = new QListWidget;
  list->setObjectName("favoritesList");
  list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  list->setDragDropMode(QAbstractItemView::InternalMove);
  list->setDefaultDropAction(Qt::MoveAction);
  layout->addWidget(list, 1);
  auto *detail = new QLabel;
  detail->setObjectName("favoriteDetails");
  detail->setTextFormat(Qt::PlainText);
  detail->setWordWrap(true);
  detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(detail);
  bool filling = false;
  const auto selectedIds = [&] {
    QStringList ids;
    for (int row = 0; row < list->count(); ++row)
      if (list->item(row)->isSelected() && !list->item(row)->isHidden())
        ids.append(list->item(row)->data(Qt::UserRole).toString());
    return ids;
  };
  const auto details = [&] {
    auto *item = list->currentItem();
    if (!item || item->isHidden()) {
      detail->clear();
      return;
    }
    for (const auto &entry : profile.favorites())
      if (entry.id == item->data(Qt::UserRole).toString()) {
        detail->setText(
            categoryName(Profile::favoriteCategory(entry.file)) + " · " +
            displayPath(entry.file) +
            (entry.portable
                 ? uiTr("\nПуть хранится относительно папки программы")
                 : ""));
        break;
      }
  };
  const auto filter = [&] {
    const auto entries = profile.favorites();
    QMap<QString, Favorite> byId;
    for (const auto &entry : entries)
      byId[entry.id] = entry;
    const auto needle = search->text().trimmed();
    for (int row = 0; row < list->count(); ++row) {
      auto *item = list->item(row);
      const auto entry = byId.value(item->data(Qt::UserRole).toString());
      const bool match =
          (category->currentData() == "all" ||
           category->currentData().toString() ==
               Profile::favoriteCategory(entry.file)) &&
          (needle.isEmpty() ||
           entry.title.contains(needle, Qt::CaseInsensitive) ||
           entry.file.contains(needle, Qt::CaseInsensitive) ||
           displayPath(entry.file).contains(needle, Qt::CaseInsensitive));
      item->setHidden(!match);
      if (!match)
        item->setSelected(false);
    }
    // Drag operates on the complete order; filtered reordering uses explicit
    // buttons.
    list->setDragEnabled(needle.isEmpty() && category->currentData() == "all");
    list->setAcceptDrops(list->dragEnabled());
    details();
  };
  const auto fill = [&](const QStringList &keep = QStringList{}) {
    const auto ids = keep.isEmpty() ? selectedIds() : keep;
    const auto previous =
        list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString()
                            : QString();
    QListWidgetItem *cursor = nullptr;
    filling = true;
    list->clear();
    for (const auto &entry : profile.favorites()) {
      auto *item = new QListWidgetItem(
          entry.title + (entry.rememberPosition
                             ? " — " + clockText(entry.position)
                             : uiTr(" — сначала")),
          list);
      item->setData(Qt::UserRole, entry.id);
      item->setToolTip(displayPath(entry.file));
      if (Profile::sourceMissing(entry.file))
        item->setForeground(QColor(210, 140, 130));
      if (ids.contains(entry.id)) {
        item->setSelected(true);
        if (!cursor || entry.id == previous)
          cursor = item;
      }
    }
    if (cursor)
      list->setCurrentItem(cursor, QItemSelectionModel::NoUpdate);
    else if (list->count())
      list->setCurrentRow(0);
    filling = false;
    filter();
  };
  connect(search, &QLineEdit::textChanged, &dialog, filter);
  connect(category, &QComboBox::currentIndexChanged, &dialog, filter);
  connect(list, &QListWidget::currentItemChanged, &dialog, details);
  const auto storeOrder = [&] {
    if (filling)
      return;
    QStringList ids;
    for (int row = 0; row < list->count(); ++row)
      ids.append(list->item(row)->data(Qt::UserRole).toString());
    profile.reorderFavorites(ids);
    details();
  };
  connect(list->model(), &QAbstractItemModel::rowsMoved, &dialog, storeOrder);
  connect(list->model(), &QAbstractItemModel::rowsInserted, &dialog,
          storeOrder);
  connect(list->model(), &QAbstractItemModel::rowsRemoved, &dialog, storeOrder);
  auto *row = new QHBoxLayout;
  layout->addLayout(row);
  const auto button = [&](const QString &id, const QString &text,
                          std::function<void()> fn) {
    auto *b = new QPushButton(text);
    b->setObjectName(id);
    b->setAutoDefault(false);
    row->addWidget(b);
    connect(b, &QPushButton::clicked, &dialog, std::move(fn));
    return b;
  };
  auto *open = button("favoriteOpen", uiTr("Открыть"), [&] {
    auto *item = list->currentItem();
    if (!item || item->isHidden())
      return;
    for (const auto &entry : profile.favorites())
      if (entry.id == item->data(Qt::UserRole).toString()) {
        openFavorite(entry);
        dialog.accept();
        break;
      }
  });
  button("favoriteEdit", uiTr("Изменить…"), [&] {
    auto *item = list->currentItem();
    if (!item || item->isHidden())
      return;
    for (auto entry : profile.favorites())
      if (entry.id == item->data(Qt::UserRole).toString()) {
        if (favoriteEditor(&dialog, &entry, false)) {
          profile.updateFavorite(entry.id, entry.title, entry.position,
                                 entry.rememberPosition, entry.portable);
          fill({entry.id});
        }
        break;
      }
  });
  const auto move = [&](int delta) {
    const auto chosen = selectedIds();
    if (chosen.isEmpty())
      return;
    QStringList visible, all;
    for (int row = 0; row < list->count(); ++row)
      if (!list->item(row)->isHidden())
        visible.append(list->item(row)->data(Qt::UserRole).toString());
    const QSet<QString> shown(visible.begin(), visible.end()),
        selected(chosen.begin(), chosen.end());
    for (int step = 0; step < visible.size(); ++step) {
      const int index = delta < 0 ? step : visible.size() - 1 - step,
                target = index + delta;
      if (target < 0 || target >= visible.size() ||
          !selected.contains(visible[index]) ||
          selected.contains(visible[target]))
        continue;
      visible.swapItemsAt(index, target);
    }
    int next = 0;
    for (const auto &entry : profile.favorites())
      all.append(shown.contains(entry.id) ? visible[next++] : entry.id);
    profile.reorderFavorites(all);
    fill(chosen);
  };
  button("favoriteUp", "↑", [&] { move(-1); });
  button("favoriteDown", "↓", [&] { move(1); });
  button("favoriteCopy", uiTr("Копировать"), [&] {
    const auto ids = selectedIds();
    QStringList sources;
    for (const auto &entry : profile.favorites())
      if (ids.contains(entry.id))
        sources.append(entry.file);
    copySources(sources);
  });
  auto *remove = button("favoriteRemove", uiTr("Удалить"), [&] {
    for (const auto &id : selectedIds())
      profile.removeFavorite(id);
    fill();
  });
  button("favoriteClose", uiTr("Закрыть"), [&] { dialog.reject(); });
  connect(list, &QListWidget::itemDoubleClicked, open,
          [open] { open->click(); });
  auto *copyKey = new QShortcut(QKeySequence::Copy, list);
  copyKey->setContext(Qt::WidgetWithChildrenShortcut);
  connect(copyKey, &QShortcut::activated,
          dialog.findChild<QPushButton *>("favoriteCopy"), &QPushButton::click);
  auto *removeKey = new QShortcut(QKeySequence(Qt::Key_Delete), list);
  removeKey->setContext(Qt::WidgetWithChildrenShortcut);
  connect(removeKey, &QShortcut::activated, remove, &QPushButton::click);
  auto *enterKey = new QShortcut(QKeySequence(Qt::Key_Return), list);
  enterKey->setContext(Qt::WidgetWithChildrenShortcut);
  connect(enterKey, &QShortcut::activated, open, &QPushButton::click);
  fill();
  dialog.exec();
  for (auto *child : dialog.findChildren<QObject *>())
    QObject::disconnect(child, nullptr, &dialog, nullptr);
}
void Window::attachLibraryPreferences(QTabWidget *tabs,
                                      QList<std::function<void()>> *apply) {
  auto *page = new QWidget;
  auto *form = new QFormLayout(page);
  auto *stored = new QSpinBox;
  stored->setObjectName("history/limit");
  stored->setRange(1, 10000);
  stored->setValue(profile.historyLimit());
  auto *menu = new QSpinBox;
  menu->setObjectName("history/menuLimit");
  menu->setRange(0, 100);
  menu->setSpecialValueText(uiTr("Не показывать записи"));
  menu->setValue(profile.historyMenuLimit());
  form->addRow(uiTr("Записей в истории:"), stored);
  form->addRow(uiTr("Записей в меню:"), menu);
  auto *remember =
      new QCheckBox(uiTr("По умолчанию сохранять позицию в новом избранном"));
  remember->setObjectName("favorites/rememberPosition");
  remember->setChecked(
      profile.settings.value("favorites/rememberPosition", true).toBool());
  form->addRow(remember);
  auto *portable = new QCheckBox(
      uiTr("По умолчанию сохранять относительные пути в новом избранном"));
  portable->setObjectName("favorites/portablePaths");
  portable->setChecked(
      profile.settings.value("favorites/portablePaths", false).toBool());
  form->addRow(portable);
  auto *note = new QLabel(uiTr(
      "История — Ctrl+H или меню «Недавние файлы». Уменьшение размера истории "
      "сокращает список; выборочное удаление и очистка также удаляют позиции. "
      "Избранное от этого не меняется. Для каждой закладки можно выбрать "
      "позицию и переносимый путь отдельно."));
  note->setWordWrap(true);
  form->addRow(note);
  tabs->addTab(page, uiTr("История и избранное"));
  apply->append([this, stored, menu, remember, portable] {
    profile.setHistoryLimits(stored->value(), menu->value());
    profile.settings.setValue("favorites/rememberPosition",
                              remember->isChecked());
    profile.settings.setValue("favorites/portablePaths", portable->isChecked());
  });
}
