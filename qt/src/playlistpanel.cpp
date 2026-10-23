#include "playlistpanel.h"
#include "appearance.h"
#include "i18n.h"
#include "mediasource.h"
#include "playlistfile.h"
#include "profile.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QtWidgets>
#include <algorithm>
#include <cmath>
#include <random>

namespace {
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString clockText(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0)
    return "—";
  const qint64 s = qint64(seconds);
  return QString("%1:%2:%3")
      .arg(s / 3600, 2, 10, QChar('0'))
      .arg(s / 60 % 60, 2, 10, QChar('0'))
      .arg(s % 60, 2, 10, QChar('0'));
}
class DurationDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    QStyleOptionViewItem item(option);
    initStyleOption(&item, index);
    const auto duration =
        clockText(index.data(PlaylistPanel::Duration).toDouble());
    const int width = item.fontMetrics.horizontalAdvance("00:00:00") + 16;
    item.text = item.fontMetrics.elidedText(
        item.text, Qt::ElideRight, std::max(0, item.rect.width() - width - 8));
    auto *style = item.widget ? item.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);
    painter->save();
    painter->setFont(item.font);
    painter->setPen(item.palette.color(
        item.state.testFlag(QStyle::State_Selected) ? QPalette::HighlightedText
                                                    : QPalette::Text));
    painter->drawText(item.rect.adjusted(item.rect.width() - width, 0, -6, 0),
                      Qt::AlignRight | Qt::AlignVCenter, duration);
    painter->restore();
  }
};
} // namespace

PlaylistPanel::PlaylistPanel(Profile &p, QWidget *parent)
    : QWidget(parent), profile(p) {
  setObjectName("playlistPanel");
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  auto *row = new QHBoxLayout;
  search = new QLineEdit;
  search->setObjectName("playlistSearch");
  search->setPlaceholderText(uiTr("Поиск по названию и пути"));
  search->setClearButtonEnabled(true);
  row->addWidget(search, 1);
  menu = new QMenu(uiTr("Операции с плейлистом"), this);
  menu->setObjectName("playlistOperations");
  auto *button = new QToolButton;
  button->setText("⋯");
  button->setToolTip(menu->title());
  button->setMenu(menu);
  button->setPopupMode(QToolButton::InstantPopup);
  row->addWidget(button);
  layout->addLayout(row);
  tabs = new QTabWidget;
  tabs->setObjectName("playlistTabs");
  tabs->setMovable(true);
  tabs->setTabsClosable(true);
  layout->addWidget(tabs, 1);
  summary = new QLabel;
  summary->setObjectName("playlistSummary");
  summary->setWordWrap(true);
  layout->addWidget(summary);
  const auto add = [this](QMenu *target, const QString &id,
                          const QString &label, std::function<void()> fn) {
    auto *a = target->addAction(label, this, std::move(fn));
    a->setObjectName("action_" + id);
    return a;
  };
  add(menu, "playlistPlaySelected", uiTr("Воспроизвести выделенный"),
      [this] { emit playRequested(activeList(), activeList()->currentRow()); });
  add(menu, "playlistNewTab", uiTr("Новая вкладка…"), [this] {
    bool ok;
    const auto name =
        QInputDialog::getText(this, uiTr("Новый плейлист"), uiTr("Название"),
                              QLineEdit::Normal, uiTr("Плейлист"), &ok);
    if (ok && !name.trimmed().isEmpty())
      createList(name.trimmed());
  });
  add(menu, "playlistRenameTab", uiTr("Переименовать вкладку…"), [this] {
    bool ok;
    const auto name = QInputDialog::getText(
        this, uiTr("Плейлист"), uiTr("Название"), QLineEdit::Normal,
        tabs->tabText(tabs->currentIndex()), &ok);
    if (ok && !name.trimmed().isEmpty()) {
      tabs->setTabText(tabs->currentIndex(), name.trimmed());
      emit changed(activeList());
    }
  });
  add(menu, "playlistCloseTab", uiTr("Удалить вкладку…"),
      [this] { closeList(tabs->currentIndex()); });
  add(menu, "playlistBrowse", uiTr("Просмотр папок…"),
      [this] { browseFolder(); });
  menu->addSeparator();
  auto *sorting = menu->addMenu(uiTr("Сортировка и порядок"));
  for (const auto &entry : QList<QPair<QString, QString>>{
           {"name", uiTr("По названию")},
           {"path", uiTr("По пути")},
           {"created", uiTr("По дате создания")},
           {"modified", uiTr("По дате изменения")},
           {"size", uiTr("По размеру")},
           {"duration", uiTr("По длительности")},
           {"reverse", uiTr("Обратный порядок")},
           {"random", uiTr("Перемешать строки")},
           {"original", uiTr("Восстановить исходный порядок")}})
    add(sorting, "playlistSort_" + entry.first, entry.second,
        [this, key = entry.first] { sort(key); });
  menu->addSeparator();
  add(menu, "playlistCopy", uiTr("Копировать (Ctrl+C в списке)"),
      [this] { copy(false); });
  add(menu, "playlistCut", uiTr("Вырезать из списка (Ctrl+X)"),
      [this] { copy(true); });
  add(menu, "playlistPaste", uiTr("Добавить из буфера (Ctrl+V)"),
      [this] { paste(); });
  add(menu, "playlistSelectAll", uiTr("Выделить видимые записи (Ctrl+A)"),
      [this] {
        for (int i = 0; i < activeList()->count(); ++i)
          activeList()->item(i)->setSelected(
              !activeList()->item(i)->isHidden());
      });
  add(menu, "playlistRemoveMissing", uiTr("Удалить отсутствующие из списка"),
      [this] { removeMissing(); });
  menu->addSeparator();
  add(menu, "playlistReveal", uiTr("Открыть папку файла"), [this] {
    auto *item = activeList()->currentItem();
    if (item && !MediaSource::isUrl(item->data(Source).toString()))
      QDesktopServices::openUrl(QUrl::fromLocalFile(
          QFileInfo(item->data(Source).toString()).absolutePath()));
  });
  add(menu, "playlistInfo", uiTr("MediaInfo / сведения о файле…"),
      [this] { information(); });
  add(menu, "playlistTrash", uiTr("Переместить файлы в корзину…"),
      [this] { trashSelected(); });
  connect(tabs, &QTabWidget::tabCloseRequested, this,
          &PlaylistPanel::closeList);
  connect(tabs, &QTabWidget::currentChanged, this, [this] {
    filter();
    if (!restoring && !arranging)
      emit changed(activeList());
  });
  connect(tabs->tabBar(), &QTabBar::tabMoved, this, [this] {
    if (!restoring)
      emit changed(activeList());
  });
  connect(search, &QLineEdit::textChanged, this, &PlaylistPanel::filter);
  const auto saved =
      QJsonDocument::fromJson(
          profile.settings.value("playlist/tabsV1").toByteArray())
          .object();
  savedPlayingId = saved.value("playingTab").toString();
  for (const auto &value : saved.value("tabs").toArray()) {
    const auto tab = value.toObject();
    auto *list = createList(tab.value("name").toString(uiTr("Плейлист")));
    list->setProperty("tabId", tab.value("id").toString(uuid()));
    qint64 next = 0;
    for (const auto &entry : tab.value("entries").toArray()) {
      const auto data = entry.toObject();
      const auto source =
          MediaSource::normalize(data.value("source").toString());
      if (source.isEmpty())
        continue;
      auto *item = new QListWidgetItem(
          data.value("title").toString(MediaSource::displayName(source)), list);
      item->setData(Source, source);
      item->setData(Id, data.value("id").toString(uuid()));
      item->setData(Metadata, data.value("metadata").toObject().toVariantMap());
      const qint64 order = qint64(data.value("order").toDouble(next));
      item->setData(OriginalOrder, order);
      next = std::max(next, order + 1);
      item->setToolTip(source);
      item->setData(Duration, -1);
      const QFileInfo info(source);
      if (!MediaSource::isUrl(source) &&
          info.size() == qint64(data.value("size").toDouble(-1)) &&
          info.lastModified().toMSecsSinceEpoch() ==
              qint64(data.value("modified").toDouble(-1)))
        item->setData(Duration, data.value("duration").toDouble(-1));
      queueProbe(list, item);
    }
    list->setProperty("nextOrder", next);
  }
  if (!tabs->count()) {
    auto *list = createList(uiTr("Основной"));
    append(list, profile.settings.value("files/playlist").toStringList());
  }
  for (auto *list : lists())
    if (list->property("tabId").toString() ==
        saved.value("activeTab").toString())
      tabs->setCurrentWidget(list);
  restoring = false;
  filter();
}
PlaylistPanel::~PlaylistPanel() {
  if (probe) {
    probe->disconnect(this);
    probe->kill();
    probe->waitForFinished(1000);
  }
}
QListWidget *PlaylistPanel::activeList() const {
  return qobject_cast<QListWidget *>(tabs->currentWidget());
}
QList<QListWidget *> PlaylistPanel::lists() const {
  QList<QListWidget *> result;
  for (int i = 0; i < tabs->count(); ++i)
    result.append(qobject_cast<QListWidget *>(tabs->widget(i)));
  return result;
}
QListWidget *PlaylistPanel::savedPlayingList() const {
  for (auto *list : lists())
    if (list->property("tabId").toString() == savedPlayingId)
      return list;
  return activeList();
}
QListWidget *PlaylistPanel::createList(const QString &name) {
  auto *list = new QListWidget;
  list->setObjectName("playlist");
  list->setProperty("tabId", uuid());
  list->setProperty("nextOrder", qint64(0));
  list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  list->setDragDropMode(QAbstractItemView::InternalMove);
  list->setDefaultDropAction(Qt::MoveAction);
  list->setItemDelegate(new DurationDelegate(list));
  list->installEventFilter(this);
  list->viewport()->installEventFilter(this);
  list->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(list, &QWidget::customContextMenuRequested, this,
          [this, list](const QPoint &p) {
            menu->popup(list->viewport()->mapToGlobal(p));
          });
  connect(list, &QListWidget::itemDoubleClicked, this,
          [this, list](QListWidgetItem *item) {
            emit playRequested(list, list->row(item));
          });
  const auto update = [this, list] {
    if (restoring || arranging)
      return;
    updateSummary();
    emit changed(list, true);
  };
  connect(list->model(), &QAbstractItemModel::rowsMoved, this, update);
  connect(list->model(), &QAbstractItemModel::rowsInserted, this, update);
  connect(list->model(), &QAbstractItemModel::rowsRemoved, this, update);
  // Titles supplied by an online playlist are persisted as well.
  connect(list->model(), &QAbstractItemModel::dataChanged, this,
          [this, list](const QModelIndex &, const QModelIndex &,
                       const QList<int> &roles) {
            if (!restoring && !arranging &&
                (roles.isEmpty() || roles.contains(Qt::DisplayRole))) {
              filter();
              emit changed(list);
            }
          });
  tabs->addTab(list, name);
  tabs->setCurrentWidget(list);
  if (!restoring)
    emit changed(list, true);
  list->setFont(Appearance::playlistFont(profile.settings, QApplication::font()));
  return list;
}
void PlaylistPanel::append(QListWidget *list, const QStringList &sources,
                           bool expandPlaylists) {
  if (!list)
    return;
  QList<PlaylistEntry> entries;
  for (const auto &source : sources) {
    if (expandPlaylists && PlaylistFile::isPlaylist(source)) {
      QList<PlaylistEntry> imported;
      QString error;
      if (!PlaylistFile::readEntries(
              source, &imported, &error,
              profile.settings.value("playlist/skipInvalidSources", false)
                  .toBool())) {
        emit importFailed(error);
        return;
      }
      entries.append(imported);
    } else
      entries.append({source, {}, -1, {}});
  }
  arranging = true;
  for (const auto &entry : entries) {
    const QString source = MediaSource::normalize(entry.source);
    if (source.isEmpty())
      continue;
    auto *item = new QListWidgetItem(
        entry.title.isEmpty() ? MediaSource::displayName(source) : entry.title,
        list);
    item->setData(Source, source);
    item->setData(Id, uuid());
    item->setData(Duration, entry.duration);
    item->setData(Metadata, entry.metadata);
    const auto order = list->property("nextOrder").toLongLong();
    item->setData(OriginalOrder, order);
    list->setProperty("nextOrder", order + 1);
    item->setToolTip(source);
    queueProbe(list, item);
  }
  arranging = false;
  filter();
  if (!restoring)
    emit changed(list, true);
}
void PlaylistPanel::save(QListWidget *playing, const QString &currentId) {
  if (restoring || !activeList())
    return;
  QJsonArray data;
  for (int i = 0; i < tabs->count(); ++i) {
    auto *list = qobject_cast<QListWidget *>(tabs->widget(i));
    QJsonArray entries;
    for (int row = 0; row < list->count(); ++row) {
      auto *item = list->item(row);
      entries.append(
          QJsonObject{{"source", item->data(Source).toString()},
                      {"title", item->text()},
                      {"id", item->data(Id).toString()},
                      {"order", double(item->data(OriginalOrder).toLongLong())},
                      {"duration", item->data(Duration).toDouble()},
                      {"size", double(item->data(FileSize).toLongLong())},
                      {"modified", double(item->data(Modified).toLongLong())}});
      auto entry = entries.last().toObject();
      entry["metadata"] =
          QJsonObject::fromVariantMap(item->data(Metadata).toMap());
      entries.replace(entries.size() - 1, entry);
    }
    data.append(QJsonObject{{"id", list->property("tabId").toString()},
                            {"name", tabs->tabText(i)},
                            {"entries", entries}});
  }
  profile.settings.setValue(
      "playlist/tabsV1",
      QJsonDocument(
          QJsonObject{{"tabs", data},
                      {"activeTab", activeList()->property("tabId").toString()},
                      {"playingTab", playing
                                         ? playing->property("tabId").toString()
                                         : QString()},
                      {"currentId", currentId}})
          .toJson(QJsonDocument::Compact));
}
void PlaylistPanel::filter() {
  auto *list = activeList();
  if (!list)
    return;
  const auto needle = search->text().trimmed();
  for (int i = 0; i < list->count(); ++i) {
    auto *item = list->item(i);
    const bool hidden =
        !needle.isEmpty() &&
        !item->text().contains(needle, Qt::CaseInsensitive) &&
        !item->data(Source).toString().contains(needle, Qt::CaseInsensitive);
    item->setHidden(hidden);
    if (hidden)
      item->setSelected(false);
  }
  updateSummary();
}
void PlaylistPanel::updateSummary() {
  if (!activeList())
    return;
  int visible = 0, unknown = 0;
  double total = 0;
  for (int i = 0; i < activeList()->count(); ++i) {
    auto *item = activeList()->item(i);
    if (item->isHidden())
      continue;
    ++visible;
    const double seconds = item->data(Duration).toDouble();
    if (seconds < 0)
      ++unknown;
    else
      total += seconds;
  }
  summary->setText(
      QString(uiTr("Записей: %1 из %2 · %3%4"))
          .arg(visible)
          .arg(activeList()->count())
          .arg(clockText(total),
               unknown ? QString(uiTr(" · без длительности: %1")).arg(unknown)
                       : QString()));
  summary->setToolTip(uiTr(
      "Поиск скрывает строки. Порядок воспроизведения включает весь список."));
}
void PlaylistPanel::removeItems(QListWidget *list,
                                const QList<QListWidgetItem *> &items) {
  if (items.isEmpty())
    return;
  emit removing(list, items);
  arranging = true;
  for (auto *item : items)
    delete list->takeItem(list->row(item));
  arranging = false;
  updateSummary();
  emit changed(list, true);
}
bool PlaylistPanel::removeById(QListWidget *list, const QString &id) {
  if (!list || !lists().contains(list)) return false;
  for (int row = 0; row < list->count(); ++row) {
    auto *item = list->item(row);
    if (item->data(Id).toString() == id) {
      removeItems(list, {item});
      return true;
    }
  }
  return false;
}
void PlaylistPanel::removeSelected() {
  removeItems(activeList(), activeList()->selectedItems());
}
void PlaylistPanel::clearActive() {
  QList<QListWidgetItem *> items;
  for (int i = 0; i < activeList()->count(); ++i)
    items.append(activeList()->item(i));
  removeItems(activeList(), items);
}
void PlaylistPanel::closeList(int index) {
  if (index < 0)
    return;
  if (QMessageBox::question(this, uiTr("Удаление вкладки"),
                            uiTr("Удалить вкладку «") + tabs->tabText(index) +
                                uiTr("»? Файлы останутся на диске."),
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No) != QMessageBox::Yes)
    return;
  auto *list = qobject_cast<QListWidget *>(tabs->widget(index));
  emit removingList(list);
  arranging = true;
  if (tabs->count() == 1)
    createList(uiTr("Основной"));
  tabs->removeTab(tabs->indexOf(list));
  list->deleteLater();
  arranging = false;
  filter();
  emit changed(activeList());
}
void PlaylistPanel::moveSelected(int delta) {
  auto *list = activeList();
  QList<int> rows;
  for (auto *item : list->selectedItems())
    rows.append(list->row(item));
  std::sort(rows.begin(), rows.end());
  if (delta > 0)
    std::reverse(rows.begin(), rows.end());
  arranging = true;
  for (int row : rows) {
    const int target = row + delta;
    if (target < 0 || target >= list->count() ||
        list->item(target)->isSelected())
      continue;
    auto *item = list->takeItem(row);
    list->insertItem(target, item);
    item->setSelected(true);
  }
  arranging = false;
  emit changed(list);
}
void PlaylistPanel::sort(const QString &key) {
  auto *list = activeList();
  QList<QListWidgetItem *> items;
  for (int i = 0; i < list->count(); ++i)
    items.append(list->item(i));
  if (key == "reverse")
    std::reverse(items.begin(), items.end());
  else if (key == "random")
    std::shuffle(items.begin(), items.end(), *QRandomGenerator::global());
  else {
    QCollator collator;
    if (collator.locale().language() == QLocale::C)
      collator.setLocale(QLocale(QLocale::English));
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    QHash<QListWidgetItem *, QVariant> values;
    for (auto *item : items) {
      const auto path = item->data(Source).toString();
      const bool local = !MediaSource::isUrl(path);
      const QFileInfo info(local ? path : QString());
      if (key == "name")
        values[item] = item->text();
      else if (key == "path")
        values[item] = path;
      else if (key == "size")
        values[item] = local && info.isFile() ? info.size() : qint64(-1);
      else if (key == "created")
        values[item] = info.birthTime().isValid()
                           ? info.birthTime().toMSecsSinceEpoch()
                           : qint64(-1);
      else if (key == "modified")
        values[item] = info.lastModified().isValid()
                           ? info.lastModified().toMSecsSinceEpoch()
                           : qint64(-1);
      else if (key == "duration")
        values[item] = item->data(Duration);
      else
        values[item] = item->data(OriginalOrder);
    }
    std::stable_sort(items.begin(), items.end(), [&](auto *a, auto *b) {
      if (key == "name" || key == "path")
        return collator.compare(values[a].toString(), values[b].toString()) < 0;
      const double av = values[a].toDouble(), bv = values[b].toDouble();
      // Unavailable dates, size and duration sort after known values.
      return av < 0 ? false : bv < 0 || av < bv;
    });
  }
  const auto selected = list->selectedItems();
  auto *cursor = list->currentItem();
  arranging = true;
  while (list->count())
    list->takeItem(0);
  for (auto *item : items)
    list->addItem(item);
  list->setCurrentItem(cursor, QItemSelectionModel::NoUpdate);
  for (auto *item : selected)
    item->setSelected(true);
  arranging = false;
  filter();
  emit changed(list);
}
void PlaylistPanel::copy(bool cut) {
  auto selected = activeList()->selectedItems();
  if (selected.isEmpty())
    return;
  std::sort(selected.begin(), selected.end(), [this](auto *a, auto *b) {
    return activeList()->row(a) < activeList()->row(b);
  });
  auto *mime = new QMimeData;
  QStringList text;
  QList<QUrl> urls;
  for (auto *item : selected) {
    const auto source = item->data(Source).toString();
    text.append(source);
    urls.append(MediaSource::isUrl(source) ? QUrl(source)
                                           : QUrl::fromLocalFile(source));
  }
  mime->setText(text.join('\n'));
  mime->setUrls(urls);
  QApplication::clipboard()->setMimeData(mime);
  if (cut)
    removeItems(activeList(), selected);
}
void PlaylistPanel::paste() {
  const auto *mime = QApplication::clipboard()->mimeData();
  QStringList sources;
  if (mime->hasUrls())
    for (const auto &url : mime->urls())
      sources.append(url.isLocalFile() ? url.toLocalFile()
                                       : url.toString(QUrl::FullyEncoded));
  else
    for (const auto &line : mime->text().split('\n'))
      if (!line.trimmed().isEmpty() && !line.startsWith('#'))
        sources.append(line.trimmed());
  QStringList expanded;
  for (const auto &source : sources)
    if (QFileInfo(source).isDir())
      expanded.append(MediaSource::filesInDirectory(source));
    else
      expanded.append(source);
  append(activeList(), expanded);
}
void PlaylistPanel::removeMissing() {
  QList<QListWidgetItem *> missing;
  for (int i = 0; i < activeList()->count(); ++i) {
    auto *item = activeList()->item(i);
    const auto source = item->data(Source).toString();
    if (!MediaSource::isUrl(source) && !QFileInfo::exists(source))
      missing.append(item);
  }
  removeItems(activeList(), missing);
}
void PlaylistPanel::trashSelected() {
  QSet<QString> paths;
  for (auto *item : activeList()->selectedItems()) {
    const auto source = item->data(Source).toString();
    if (!MediaSource::isUrl(source) && QFileInfo(source).isFile())
      paths.insert(source);
  }
  if (paths.isEmpty())
    return;
  QStringList sorted = paths.values();
  sorted.sort();
  QMessageBox confirm(
      QMessageBox::Warning, uiTr("Удаление файлов с диска"),
      QString(uiTr("Переместить в корзину файлов: %1?\nОни будут "
                   "удалены из всех вкладок."))
          .arg(paths.size()),
      QMessageBox::Yes | QMessageBox::No, this);
  confirm.setObjectName("playlistTrashConfirm");
  confirm.setDetailedText(sorted.join('\n'));
  confirm.setDefaultButton(QMessageBox::No);
  if (confirm.exec() != QMessageBox::Yes)
    return;
  QSet<QString> removed;
  QStringList failed;
  for (const auto &path : sorted)
    if (QFile::moveToTrash(path))
      removed.insert(path);
    else
      failed.append(path);
  for (auto *list : lists()) {
    QList<QListWidgetItem *> items;
    for (int i = 0; i < list->count(); ++i)
      if (removed.contains(list->item(i)->data(Source).toString()))
        items.append(list->item(i));
    removeItems(list, items);
  }
  if (!failed.isEmpty())
    QMessageBox::warning(this, uiTr("Корзина"),
                         uiTr("Не удалось переместить:\n") + failed.join('\n'));
}
void PlaylistPanel::browseFolder() {
  QDialog dialog(this);
  dialog.setObjectName("playlistFolderBrowser");
  dialog.setWindowTitle(uiTr("Просмотр папок — добавление в текущую вкладку"));
  dialog.resize(720, 480);
  auto *layout = new QVBoxLayout(&dialog);
  auto *path = new QLineEdit(
      profile.settings.value("files/lastDirectory", QDir::homePath())
          .toString());
  path->setObjectName("playlistFolderPath");
  layout->addWidget(path);
  auto *model = new QFileSystemModel(&dialog);
  model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
  auto *tree = new QTreeView;
  tree->setObjectName("playlistFolderTree");
  tree->setModel(model);
  tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  tree->setSortingEnabled(true);
  tree->sortByColumn(0, Qt::AscendingOrder);
  layout->addWidget(tree);
  const auto navigate = [&] {
    const QFileInfo info(path->text());
    if (info.isDir()) {
      path->setText(info.absoluteFilePath());
      tree->setRootIndex(model->setRootPath(info.absoluteFilePath()));
      profile.settings.setValue("files/lastDirectory", info.absoluteFilePath());
    }
  };
  connect(path, &QLineEdit::returnPressed, &dialog, navigate);
  connect(tree, &QTreeView::doubleClicked, &dialog,
          [&](const QModelIndex &index) {
            if (model->isDir(index)) {
              path->setText(model->filePath(index));
              navigate();
            } else
              append(activeList(), {model->filePath(index)});
          });
  auto *buttons = new QHBoxLayout;
  layout->addLayout(buttons);
  const auto button = [&](const QString &label, std::function<void()> fn) {
    auto *b = new QPushButton(label);
    b->setAutoDefault(false);
    buttons->addWidget(b);
    connect(b, &QPushButton::clicked, &dialog, std::move(fn));
  };
  button(uiTr("На уровень выше"), [&] {
    QDir dir(path->text());
    if (dir.cdUp()) {
      path->setText(dir.path());
      navigate();
    }
  });
  button(uiTr("Добавить выделенное"), [&] {
    QStringList sources;
    for (const auto &index : tree->selectionModel()->selectedRows())
      if (model->isDir(index))
        sources.append(MediaSource::filesInDirectory(model->filePath(index)));
      else
        sources.append(model->filePath(index));
    append(activeList(), sources);
  });
  button(uiTr("Добавить всю папку"), [&] {
    append(activeList(), MediaSource::filesInDirectory(path->text()));
  });
  button(uiTr("Закрыть"), [&] { dialog.accept(); });
  navigate();
  dialog.exec();
}
void PlaylistPanel::information() {
  auto *item = activeList()->currentItem();
  if (!item)
    return;
  const QString source = item->data(Source).toString();
  if (MediaSource::isUrl(source)) {
    QMessageBox::information(this, uiTr("Сведения об источнике"),
                             source + uiTr("\n\nСетевые источники и устройства "
                                           "автоматически не опрашиваются."));
    return;
  }
  const QString mediaInfo = QStandardPaths::findExecutable("mediainfo");
  const QString executable = mediaInfo.isEmpty()
                                 ? QStandardPaths::findExecutable("ffprobe")
                                 : mediaInfo;
  QDialog dialog(this);
  dialog.setObjectName("playlistInfoDialog");
  dialog.setWindowTitle(
      mediaInfo.isEmpty()
          ? uiTr("Сведения о файле — FFprobe (MediaInfo не установлен)")
          : "MediaInfo");
  dialog.resize(780, 540);
  auto *layout = new QVBoxLayout(&dialog);
  auto *report = new QPlainTextEdit;
  report->setObjectName("playlistInfoReport");
  report->setReadOnly(true);
  report->setPlainText(executable.isEmpty()
                           ? uiTr("Для отчёта установите mediainfo или ffmpeg.")
                           : uiTr("Чтение: ") + source);
  layout->addWidget(report);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  auto *copyButton = buttons->addButton(uiTr("Копировать отчёт"),
                                        QDialogButtonBox::ActionRole);
  connect(copyButton, &QPushButton::clicked, &dialog, [report] {
    QApplication::clipboard()->setText(report->toPlainText());
  });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  QByteArray output;
  QTimer timeout;
  timeout.setSingleShot(true);
  connect(&timeout, &QTimer::timeout, &process, &QProcess::kill);
  connect(&process, &QProcess::readyReadStandardOutput, &dialog, [&] {
    output += process.readAllStandardOutput();
    if (output.size() > 1024 * 1024)
      process.kill();
  });
  connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          &dialog, [&] {
            output += process.readAllStandardOutput();
            report->setPlainText(
                output.isEmpty() ? uiTr("Не удалось получить сведения: ") +
                                       process.errorString()
                                 : QString::fromUtf8(output.left(1024 * 1024)));
          });
  connect(&process, &QProcess::errorOccurred, &dialog,
          [&] { report->setPlainText(process.errorString()); });
  if (!executable.isEmpty()) {
    process.start(executable,
                  mediaInfo.isEmpty()
                      ? QStringList{"-v", "error", "-protocol_whitelist",
                                    "file,crypto,data", "-show_format",
                                    "-show_streams", "-of", "json", "-i",
                                    source}
                      : QStringList{source});
    timeout.start(10000);
  }
  dialog.exec();
  process.disconnect(&dialog);
  if (process.state() != QProcess::NotRunning) {
    process.kill();
    process.waitForFinished(1000);
  }
}
void PlaylistPanel::queueProbe(QListWidget *list, QListWidgetItem *item) {
  if (!profile.settings.value("playlist/determineDuration", true).toBool())
    return;
  const auto source = item->data(Source).toString();
  if (MediaSource::isUrl(source))
    return;
  const QFileInfo info(source);
  item->setData(FileSize, info.size());
  item->setData(Modified, info.lastModified().toMSecsSinceEpoch());
  if (!info.isFile() || item->data(Duration).toDouble() >= 0)
    return;
  probes.append({list, item->data(Id).toString()});
  QTimer::singleShot(0, this, &PlaylistPanel::probeNext);
}
void PlaylistPanel::probeNext() {
  if (!profile.settings.value("playlist/determineDuration", true).toBool()) {
    probes.clear();
    return;
  }
  if (probe || probes.isEmpty())
    return;
  const auto task = probes.takeFirst();
  QListWidgetItem *item = nullptr;
  if (task.first)
    for (int i = 0; i < task.first->count(); ++i)
      if (task.first->item(i)->data(Id).toString() == task.second)
        item = task.first->item(i);
  if (!item || item->data(Duration).toDouble() >= 0) {
    QTimer::singleShot(0, this, &PlaylistPanel::probeNext);
    return;
  }
  const QString executable = QStandardPaths::findExecutable("ffprobe");
  if (executable.isEmpty()) {
    probes.clear();
    return;
  }
  const auto source = item->data(Source).toString();
  const qint64 size = item->data(FileSize).toLongLong(),
               modified = item->data(Modified).toLongLong();
  probe = new QProcess(this);
  auto *process = probe;
  auto output = std::make_shared<QByteArray>();
  connect(process, &QProcess::readyReadStandardOutput, this, [process, output] {
    *output += process->readAllStandardOutput();
    if (output->size() > 65536)
      process->kill();
  });
  const auto finish = [this, process, output, task, source, size, modified] {
    if (probe != process)
      return;
    *output += process->readAllStandardOutput();
    bool ok;
    const double seconds = output->trimmed().toDouble(&ok);
    if (ok && std::isfinite(seconds) && seconds >= 0 &&
        process->exitCode() == 0 && QFileInfo(source).size() == size &&
        QFileInfo(source).lastModified().toMSecsSinceEpoch() == modified) {
      // Update duplicates in every tab, but never dereference a removed item.
      for (auto *list : lists())
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->data(Source).toString() == source)
            list->item(i)->setData(Duration, seconds);
      updateSummary();
      emit changed(task.first);
    }
    probe = nullptr;
    process->deleteLater();
    QTimer::singleShot(0, this, &PlaylistPanel::probeNext);
  };
  connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, finish);
  connect(process, &QProcess::errorOccurred, this,
          [finish](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
              finish();
          });
  QTimer::singleShot(5000, process, [process] {
    if (process->state() != QProcess::NotRunning)
      process->kill();
  });
  process->start(executable,
                 {"-v", "error", "-protocol_whitelist", "file,crypto,data",
                  "-show_entries", "format=duration", "-of",
                  "default=noprint_wrappers=1:nokey=1", "-i", source});
}
bool PlaylistPanel::eventFilter(QObject *object, QEvent *event) {
  auto *list = qobject_cast<QListWidget *>(object);
  if (!list)
    list = qobject_cast<QListWidget *>(object->parent());
  if (!list || !lists().contains(list))
    return QWidget::eventFilter(object, event);
  if (event->type() == QEvent::ShortcutOverride ||
      event->type() == QEvent::KeyPress) {
    auto *key = static_cast<QKeyEvent *>(event);
    const bool clipboard = key->matches(QKeySequence::Copy) ||
                           key->matches(QKeySequence::Cut) ||
                           key->matches(QKeySequence::Paste) ||
                           key->matches(QKeySequence::SelectAll);
    const bool navigation =
        key->modifiers() == Qt::NoModifier &&
        (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down ||
         key->key() == Qt::Key_Left || key->key() == Qt::Key_Right ||
         key->key() == Qt::Key_Home || key->key() == Qt::Key_End ||
         key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
         key->key() == Qt::Key_Delete);
    if (clipboard || navigation) {
      if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
      }
      if (key->matches(QKeySequence::Copy))
        copy(false);
      else if (key->matches(QKeySequence::Cut))
        copy(true);
      else if (key->matches(QKeySequence::Paste))
        paste();
      else if (key->matches(QKeySequence::SelectAll))
        menu->findChild<QAction *>("action_playlistSelectAll")->trigger();
      else if (key->key() == Qt::Key_Delete)
        removeSelected();
      else if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
        emit playRequested(list, list->currentRow());
      else
        return false;
      return true;
    }
  }
  if (object == list->viewport() &&
      (event->type() == QEvent::DragEnter ||
       event->type() == QEvent::DragMove || event->type() == QEvent::Drop)) {
    auto *drop = static_cast<QDropEvent *>(event);
    if (drop->mimeData()->hasUrls()) {
      if (event->type() == QEvent::Drop) {
        QStringList sources;
        for (const auto &url : drop->mimeData()->urls()) {
          const auto source = url.isLocalFile()
                                  ? url.toLocalFile()
                                  : url.toString(QUrl::FullyEncoded);
          if (QFileInfo(source).isDir())
            sources.append(MediaSource::filesInDirectory(source));
          else
            sources.append(source);
        }
        append(list, sources);
      }
      drop->acceptProposedAction();
      return true;
    }
  }
  return QWidget::eventFilter(object, event);
}
