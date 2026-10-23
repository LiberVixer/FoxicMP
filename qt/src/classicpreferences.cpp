#include "classicpreferences.h"
#include "i18n.h"
#include <QtWidgets>

namespace {
class LoadingSpinner final : public QWidget {
  int angle = 0;
public:
  explicit LoadingSpinner(QWidget *parent) : QWidget(parent) {
    setObjectName("settingsLoadingSpinner"); setFixedSize(40, 40);
    setAccessibleName(uiTr("Загрузка настроек…"));
    auto *timer = new QTimer(this); timer->setInterval(30);
    connect(timer, &QTimer::timeout, this, [this] { angle = (angle + 30) % 360; update(); });
    timer->start();
  }
protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    auto color = palette().color(QPalette::Text); color.setAlpha(210);
    painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(QRectF(6, 6, 28, 28), -angle * 16, 260 * 16);
  }
};
class SearchKeys : public QObject {
  QLineEdit *search;
  QListWidget *results;
  std::function<void()> open;
public:
  SearchKeys(QLineEdit *s, QListWidget *r, std::function<void()> callback)
      : QObject(s), search(s), results(r), open(std::move(callback)) {
    s->installEventFilter(this); r->installEventFilter(this);
  }
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() != QEvent::KeyPress) return false;
    const auto key = static_cast<QKeyEvent *>(event)->key();
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && (watched == search || results->isVisible())) {
      if (results->isVisible()) open();
      return true;
    }
    if (key == Qt::Key_Escape && !search->text().isEmpty()) { search->clear(); search->setFocus(); return true; }
    if (watched == search && key == Qt::Key_Down && results->isVisible()) { results->setFocus(); return true; }
    return false;
  }
};
void reveal(QWidget *widget) {
  for (auto *parent = widget; parent; parent = parent->parentWidget())
    if (auto *button = qobject_cast<QAbstractButton *>(parent->property("settingsExpandButton").value<QObject *>()))
      button->setChecked(true);
}
QString plain(QString text) {
  text.remove('&');
  return text.simplified();
}
struct Page {
  QString category, title;
  QScrollArea *scroll;
  QVBoxLayout *layout;
  QFormLayout *form;
};
}

void ClassicPreferences::beginLoading(QDialog *dialog) {
  dialog->setProperty("settingsLoading", true);
  dialog->setProperty("settingsReady", false);
  auto *page = new QWidget(dialog); page->setObjectName("settingsLoadingPage");
  auto *layout = new QVBoxLayout(page); layout->setSpacing(14);
  layout->addStretch(); layout->addWidget(new LoadingSpinner(page), 0, Qt::AlignHCenter);
  auto *text = new QLabel(uiTr("Загрузка настроек…")); text->setAlignment(Qt::AlignCenter);
  layout->addWidget(text); layout->addStretch();
  auto *cancel = new QPushButton(uiTr("Отмена")); cancel->setObjectName("settingsCancelLoading");
  layout->addWidget(cancel, 0, Qt::AlignRight);
  QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
  dialog->layout()->addWidget(page); dialog->setCursor(Qt::BusyCursor);
  dialog->setModal(true); dialog->show();
}

bool ClassicPreferences::loadingStep(QDialog *dialog) {
  if (!dialog || !dialog->property("settingsLoading").toBool()) return true;
  // Widgets stay on the GUI thread. Yield between construction stages so the
  // loading animation, playback events and Cancel remain responsive.
  QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  return dialog->isVisible();
}

void ClassicPreferences::finishLoading(QDialog *dialog) {
  auto *page = dialog->findChild<QWidget *>("settingsLoadingPage");
  if (page) { dialog->layout()->removeWidget(page); page->hide(); page->deleteLater(); }
  for (const auto &name : {"settingsSearch", "classicPreferencesSplitter", "classicPreferencesPages"})
    if (auto *widget = dialog->findChild<QWidget *>(name)) widget->show();
  if (auto *buttons = dialog->findChild<QDialogButtonBox *>()) buttons->show();
  dialog->unsetCursor();
  dialog->layout()->activate();
  if (auto *search = dialog->findChild<QLineEdit *>("settingsSearch")) search->setFocus();
  dialog->setProperty("settingsLoading", false); dialog->setProperty("settingsReady", true);
}

void ClassicPreferences::selectPage(QTabWidget *tabs, QWidget *widget) {
  if (!widget) return;
  if (auto *search = tabs->window()->findChild<QLineEdit *>("settingsSearch")) search->clear();
  reveal(widget);
  for (auto *parent = widget; parent; parent = parent->parentWidget()) {
    const int index = tabs->indexOf(parent);
    if (index < 0) continue;
    { const QSignalBlocker blocker(tabs); tabs->setTabVisible(index, true); }
    tabs->setCurrentIndex(index);
    if (auto *scroll = qobject_cast<QScrollArea *>(parent))
      QTimer::singleShot(0, scroll, [scroll, widget] { scroll->ensureWidgetVisible(widget, 8, 20); });
    return;
  }
}

void ClassicPreferences::organize(QDialog *dialog, QTabWidget *tabs, const QString &initialPath) {
  // Keep the original draft widgets alive. Moving their rows preserves all
  // validation, reset, enabled-state and Apply/Cancel connections.
  auto *inactive = new QWidget(dialog);
  inactive->setObjectName("settingsInactiveSources"); inactive->hide();
  QMap<QString, QWidget *> sources;
  while (tabs->count()) {
    const auto title = tabs->tabText(0);
    QWidget *widget = tabs->widget(0); tabs->removeTab(0);
    if (auto *scroll = qobject_cast<QScrollArea *>(widget)) {
      auto *body = scroll->takeWidget();
      if (!scroll->objectName().isEmpty()) body->setObjectName(scroll->objectName());
      scroll->setObjectName({}); // Do not shadow the body until deleteLater runs.
      scroll->deleteLater(); widget = body;
    }
    widget->setParent(inactive); widget->hide(); sources[title] = widget;
    if (!loadingStep(dialog)) return;
  }
  QMap<QString, QString> categories{
      {"general", uiTr("Общие")}, {"interface", uiTr("Интерфейс и окно")},
      {"control", uiTr("Управление")}, {"playback", uiTr("Воспроизведение")},
      {"video", uiTr("Видео")}, {"audio", uiTr("Аудио")},
      {"subtitles", uiTr("Субтитры")}, {"network", uiTr("Онлайн и сеть")},
      {"capture", uiTr("Снимки и миниатюры")}, {"service", uiTr("Обслуживание")}};
  QMap<QString, Page> pages;
  QStringList order;
  const auto page = [&](const QString &category, const QString &id, const QString &title) {
    auto *scroll = new QScrollArea;
    scroll->setObjectName(id); scroll->setProperty("classicPageId", id);
    scroll->setProperty("settingsCategory", category);
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget;
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(8, 6, 8, 8); layout->setSpacing(8);
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(form);
    scroll->setWidget(body); tabs->addTab(scroll, title);
    pages.insert(id, {category, title, scroll, layout, form}); order.append(id);
  };
  page("general", "IDD_PPAGEPLAYER", uiTr("Запуск и трей"));
  page("general", "settingsHistory", uiTr("История и избранное"));
  page("general", "IDD_PPAGEFORMATS", uiTr("Интеграция"));
  page("interface", "IDD_PPAGEINTERFACE", uiTr("Оформление"));
  page("interface", "IDD_PPAGEWINDOWSIZE", uiTr("Окно"));
  page("interface", "IDD_PPAGEFULLSCREEN", uiTr("Полный экран"));
  page("interface", "settingsPanels", uiTr("Панели"));
  page("interface", "settingsTimeline", uiTr("Таймлайн"));
  page("interface", "IDD_PPAGEOSD", "OSD");
  page("control", "IDD_PPAGEACCELTBL", uiTr("Клавиатура"));
  page("control", "IDD_PPAGEMOUSE", uiTr("Мышь"));
  page("control", "settingsCommandSteps", uiTr("Шаги команд"));
  page("control", "settingsMediaKeys", uiTr("Медиаклавиши"));
  page("playback", "IDD_PPAGEPLAYBACK", uiTr("Основное"));
  page("playback", "settingsPlaylist", uiTr("Плейлисты"));
  page("video", "IDD_PPAGEVIDEO", uiTr("Вывод"));
  page("video", "IDD_PPAGEINTERNALFILTERS", uiTr("Декодирование"));
  page("video", "settingsVideoFrame", uiTr("Кадр"));
  page("video", "IDD_PPAGECOLOR", uiTr("Цвет"));
  page("video", "IDD_PPAGESYNC", uiTr("Синхронизация"));
  page("video", "videoShadersPage", uiTr("Шейдеры"));
  page("audio", "IDD_PPAGEAUDIO", uiTr("Выход"));
  page("audio", "settingsAudioTracks", uiTr("Дорожки"));
  page("audio", "settingsAudioMixer", uiTr("Микшер"));
  page("audio", "IDD_PPAGESOUNDPROCESSING", uiTr("Нормализация"));
  page("audio", "audioFiltersPage", uiTr("Фильтры"));
  page("audio", "audioEncodingPage", uiTr("Формат"));
  page("subtitles", "IDD_PPAGESUBTITLES", uiTr("Файлы и языки"));
  page("subtitles", "IDD_PPAGESUBSTYLE", uiTr("Оформление"));
  page("subtitles", "settingsSubtitlePlacement", uiTr("Размещение"));
  page("subtitles", "IDD_PPAGESUBRENDERING", uiTr("Отрисовка"));
  page("subtitles", "settingsSubtitleCompatibility", uiTr("ASS и совместимость"));
  page("network", "IDD_PPAGEYOUTUBE", uiTr("Веб-видео"));
  page("network", "settingsStreamServices", uiTr("Потоковые сервисы"));
  page("network", "IDD_PPAGEWEBSERVER", uiTr("Веб-пульт"));
  page("capture", "settingsCaptures", uiTr("Снимки"));
  page("capture", "settingsThumbnails", uiTr("Листы миниатюр"));
  page("service", "settingsProfiles", uiTr("Профили и копии"));
  page("service", "settingsUpdates", uiTr("Обновления"));
  page("service", "IDD_PPAGEMISC", uiTr("Диагностика"));

  const auto field = [dialog](const QString &key) { return dialog->findChild<QWidget *>(key); };
  const auto move = [&](QWidget *widget, const QString &id, const QString &caption = QString()) {
    if (!widget) return;
    auto *target = pages[id].form;
    QWidget *rowWidget = widget;
    const auto forms = dialog->findChildren<QFormLayout *>();
    for (; rowWidget && rowWidget != dialog; rowWidget = rowWidget->parentWidget()) {
      for (auto *form : forms) {
        int row = -1; QFormLayout::ItemRole role;
        form->getWidgetPosition(rowWidget, &row, &role);
        if (row < 0) continue;
        const auto taken = form->takeRow(row);
        auto *label = taken.labelItem ? taken.labelItem->widget() : nullptr;
        auto *value = taken.fieldItem ? taken.fieldItem->widget() : nullptr;
        auto *layout = taken.fieldItem ? taken.fieldItem->layout() : nullptr;
        if (!caption.isEmpty()) {
          if (auto *text = qobject_cast<QLabel *>(label)) text->setText(caption);
          else if (!label) label = new QLabel(caption);
          widget->setProperty("settingsLabel", caption);
        }
        delete taken.labelItem;
        if (value) {
          delete taken.fieldItem;
          if (label) target->addRow(label, value); else target->addRow(value);
          value->show();
        } else if (layout) {
          if (label) target->addRow(label, layout); else target->addRow(layout);
        }
        if (label) label->show();
        return;
      }
    }
    // Fields from a vertical layout (checkboxes, notes, action buttons).
    for (auto *layout : dialog->findChildren<QLayout *>()) layout->removeWidget(widget);
    target->addRow(widget); widget->show();
  };
  const auto moveKey = [&](const QString &key, const QString &id, const QString &caption = QString()) {
    move(field(key), id, caption);
  };
  const auto addSource = [&](const QString &title, const QString &id, const QString &section = QString(), bool collapsed = false) {
    auto *widget = sources.take(title); if (!widget) return;
    auto &destination = pages[id];
    if (section.isEmpty()) { destination.layout->addWidget(widget); widget->show(); return; }
    if (!collapsed) {
      auto *box = new QGroupBox(section); auto *layout = new QVBoxLayout(box);
      layout->setContentsMargins(8, 12, 8, 8); layout->addWidget(widget);
      destination.layout->addWidget(box); widget->show(); return;
    }
    auto *toggle = new QToolButton; toggle->setText(section);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setCheckable(true); toggle->setArrowType(Qt::RightArrow);
    destination.layout->addWidget(toggle, 0, Qt::AlignLeft);
    auto *box = new QWidget; auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0); layout->addWidget(widget);
    box->setProperty("settingsExpandButton", QVariant::fromValue<QObject *>(toggle));
    destination.layout->addWidget(box); widget->show(); box->hide();
    QObject::connect(toggle, &QToolButton::toggled, box, [box, toggle](bool open) {
      box->setVisible(open); toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
  };

  // Split mixed legacy pages before assigning complete pages to their owners.
  moveKey("history/enabled", "settingsHistory");
  moveKey("playback/rememberPosition", "settingsHistory");
  moveKey("video/output", "IDD_PPAGEVIDEO");
  moveKey("audio/languages", "settingsAudioTracks");
  moveKey("subtitles/languages", "IDD_PPAGESUBTITLES");
  moveKey("profileLocation", "settingsProfiles");
  moveKey("settingsProfileManagement", "settingsProfiles");
  moveKey("classicMaintenanceDetails", "IDD_PPAGEMISC");
  moveKey("maintenance/priority", "IDD_PPAGEMISC");
  moveKey("maintenance/updatePreviews", "settingsUpdates");
  moveKey("settingsCheckUpdates", "settingsUpdates");
  for (const auto &key : {"capture/format", "capture/quality", "ui/screenshotSubtitles"}) moveKey(key, "settingsCaptures");
  for (const auto &key : {"capture/columns", "capture/rows", "capture/thumbWidth"}) moveKey(key, "settingsThumbnails");
  for (const auto &key : {"ui/onTop", "ui/frameless"}) moveKey(key, "IDD_PPAGEWINDOWSIZE");
  moveKey("ui/osd", "IDD_PPAGEOSD");
  for (const auto &key : {"ui/remainingTime", "ui/showZeroHours", "ui/showMenu", "ui/showControls", "ui/showStatus", "ui/showTimeline",
                         "playlist/rememberPanel", "playlist/hideFullscreen", "appearance/playlistFont", "appearance/playlistFontSize", "appearance/playlistFontPercent"})
    moveKey(key, "settingsPanels");
  moveKey("classicInterfaceSeek", "settingsTimeline");
  moveKey("preview/timeoutMs", "settingsTimeline"); moveKey("settingsPreviewHelp", "settingsTimeline");
  for (const auto &key : {"playback/seekSmallMs", "playback/seekMediumMs", "playback/seekLargeMs", "playback/speedStep"})
    moveKey(key, "settingsCommandSteps");
  for (const auto &key : {"mouse/holdSpeed", "mouse/holdSpeedRate", "mouse/holdSpeedDelayMs"}) moveKey(key, "IDD_PPAGEMOUSE");
  for (const auto &key : {"input/mpris", "settingsMediaKeysHelp", "settingsMediaKeysStatus"}) moveKey(key, "settingsMediaKeys");
  for (const auto &key : {"online/aceStreamEndpoint", "online/torrServerEndpoint"}) moveKey(key, "settingsStreamServices");
  for (const auto &key : {"audio/device", "audio/channels", "audio/dualOutput", "audio/secondDevice", "audioOutputStatus", "settingsAudioRouteHelp"})
    moveKey(key, "IDD_PPAGEAUDIO");
  for (const auto &key : {"audio/gainDb", "audio/balance", "audio/channelMode"}) moveKey(key, "settingsAudioMixer");
  // The two old normalization checkboxes shared one setting. Keep the hidden
  // legacy binding synchronized rather than letting its save overwrite a draft.
  const auto normalize = dialog->findChildren<QCheckBox *>("audio/normalize");
  if (normalize.size() == 2) {
    QObject::connect(normalize[0], &QCheckBox::toggled, normalize[1], &QCheckBox::setChecked);
    QObject::connect(normalize[1], &QCheckBox::toggled, normalize[0], &QCheckBox::setChecked);
  }
  addSource(uiTr("Запуск и трей"), "IDD_PPAGEPLAYER");
  addSource(uiTr("История и избранное"), "settingsHistory");
  addSource(uiTr("Форматы"), "IDD_PPAGEFORMATS");
  addSource(uiTr("Оформление"), "IDD_PPAGEINTERFACE");
  addSource(uiTr("Логотип"), "IDD_PPAGEINTERFACE", uiTr("Логотип"), true);
  addSource(uiTr("Поведение окна"), "IDD_PPAGEWINDOWSIZE");
  addSource(uiTr("Заголовок окна"), "IDD_PPAGEWINDOWSIZE", uiTr("Заголовок окна"), true);
  addSource(uiTr("Полный экран"), "IDD_PPAGEFULLSCREEN");
  addSource("OSD", "IDD_PPAGEOSD");
  addSource(uiTr("Клавиши"), "IDD_PPAGEACCELTBL");
  addSource(uiTr("Мышь"), "IDD_PPAGEMOUSE");
  addSource(uiTr("Управление"), "settingsCommandSteps");
  addSource(uiTr("Воспроизведение"), "IDD_PPAGEPLAYBACK");
  addSource(uiTr("Плейлист"), "settingsPlaylist");
  addSource(uiTr("Видео — масштабирование"), "IDD_PPAGEVIDEO");
  addSource(uiTr("Видео — декодирование"), "IDD_PPAGEINTERNALFILTERS");
  addSource(uiTr("Pan&Scan"), "settingsVideoFrame");
  addSource(uiTr("Кадрирование"), "settingsVideoFrame", uiTr("Кадрирование"), true);
  addSource(uiTr("Стерео 3D"), "settingsVideoFrame", uiTr("Стерео 3D"), true);
  addSource(uiTr("Пресеты Pan&Scan"), "settingsVideoFrame", uiTr("Пресеты Pan&Scan"), true);
  addSource(uiTr("Изображение"), "IDD_PPAGECOLOR");
  addSource(uiTr("Видео — цвет и точность"), "IDD_PPAGECOLOR", uiTr("Точность цвета и HDR"), true);
  addSource(uiTr("Видео — ICC-профиль"), "IDD_PPAGECOLOR", uiTr("ICC-профиль дисплея"), true);
  addSource(uiTr("Видео — синхронизация"), "IDD_PPAGESYNC");
  addSource(uiTr("Видео — шейдеры"), "videoShadersPage");
  addSource(uiTr("Звук — файлы и выходы"), "settingsAudioTracks");
  addSource(uiTr("Звук — микшер"), "settingsAudioMixer");
  addSource(uiTr("Звук — нормализация"), "IDD_PPAGESOUNDPROCESSING");
  addSource(uiTr("Звук — обработка"), "IDD_PPAGESOUNDPROCESSING", uiTr("ReplayGain и ограничитель"), true);
  addSource(uiTr("Звук — фильтры"), "audioFiltersPage");
  addSource(uiTr("Звук — формат и декодер"), "audioEncodingPage");
  addSource(uiTr("Субтитры — файлы"), "IDD_PPAGESUBTITLES");
  moveKey("subtitles/autoLoad", "IDD_PPAGESUBTITLES"); moveKey("subtitles/visible", "IDD_PPAGESUBTITLES");
  addSource(uiTr("Субтитры"), "IDD_PPAGESUBSTYLE");
  addSource(uiTr("Субтитры — цвета и тень"), "IDD_PPAGESUBSTYLE", uiTr("Тень и прозрачность"), true);
  moveKey("subtitles/position", "settingsSubtitlePlacement");
  addSource(uiTr("Субтитры — размещение"), "settingsSubtitlePlacement");
  addSource(uiTr("Субтитры — геометрия ASS"), "settingsSubtitlePlacement", uiTr("Геометрия ASS"), true);
  addSource(uiTr("Субтитры — рендер"), "IDD_PPAGESUBRENDERING");
  moveKey("subtitles/overrideAss", "settingsSubtitleCompatibility");
  addSource(uiTr("Субтитры — совместимость"), "settingsSubtitleCompatibility");
  addSource(uiTr("Онлайн"), "IDD_PPAGEYOUTUBE");
  addSource(uiTr("Web-интерфейс"), "IDD_PPAGEWEBSERVER");
  // These source pages now contain only inactive or already moved bindings.
  for (const auto &title : {uiTr("Общие"), uiTr("Звук"), uiTr("Окно и кадры"), uiTr("Разное"), uiTr("Снимки и миниатюры"), uiTr("Обслуживание")}) sources.remove(title);
  dialog->setProperty("settingsUnassignedSources", sources.keys());
  for (const auto &title : sources.keys()) addSource(title, "IDD_PPAGEMISC", title, true);
  for (const auto &key : {"classicKeyFilter", "associationFilter"})
    if (auto *filter = dialog->findChild<QLineEdit *>(key)) filter->setProperty("settingsNavigation", true);
  for (auto &p : pages) p.layout->addStretch();

  auto *outer = qobject_cast<QVBoxLayout *>(dialog->layout()); outer->removeWidget(tabs);
  auto *search = new QLineEdit; search->setObjectName("settingsSearch");
  search->setProperty("settingsNavigation", true);
  search->setPlaceholderText(uiTr("Поиск по настройкам…")); search->setClearButtonEnabled(true);
  outer->insertWidget(0, search);
  auto *split = new QSplitter; split->setObjectName("classicPreferencesSplitter");
  auto *tree = new QTreeWidget; tree->setObjectName("classicPreferencesTree");
  tree->setHeaderHidden(true); tree->setRootIsDecorated(true); tree->setMinimumWidth(190);
  auto *right = new QWidget; auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0); rightLayout->setSpacing(6);
  auto *heading = new QLabel; heading->setObjectName("classicPageHeading"); heading->setMargin(6);
  auto font = heading->font(); font.setBold(true); heading->setFont(font);
  auto *results = new QListWidget; results->setObjectName("settingsSearchResults"); results->hide();
  results->setWordWrap(true);
  rightLayout->addWidget(heading); rightLayout->addWidget(results, 1); rightLayout->addWidget(tabs, 1);
  tabs->setObjectName("classicPreferencesPages"); tabs->tabBar()->show();
  tabs->setDocumentMode(true); tabs->setUsesScrollButtons(true);
  tabs->tabBar()->setExpanding(false);
  split->addWidget(tree); split->addWidget(right); split->setStretchFactor(1, 1); split->setSizes({230, 640});
  outer->insertWidget(1, split, 1);
  if (dialog->property("settingsLoading").toBool()) { search->hide(); split->hide(); }
  QMap<QString, QTreeWidgetItem *> nodes;
  QMap<int, QTreeWidgetItem *> leaves;
  QMap<QString, int> first;
  for (int i = 0; i < order.size(); ++i) {
    const auto &p = pages[order[i]];
    if (!nodes.contains(p.category)) {
      auto *node = new QTreeWidgetItem(tree); node->setText(0, categories[p.category]);
      node->setData(0, Qt::UserRole, p.category); node->setData(0, Qt::UserRole + 1, categories[p.category]);
      nodes[p.category] = node; first[p.category] = i;
    }
    auto *leaf = new QTreeWidgetItem(nodes[p.category]); leaf->setText(0, p.title);
    leaf->setData(0, Qt::UserRole, p.category); leaf->setData(0, Qt::UserRole + 2, i);
    leaves[i] = leaf;
  }
  auto remembered = std::make_shared<QMap<QString, int>>(first);
  auto active = std::make_shared<bool>(false);
  const auto activate = [=](int index) {
    if (*active || index < 0 || index >= tabs->count()) return;
    *active = true;
    const auto category = tabs->widget(index)->property("settingsCategory").toString();
    { const QSignalBlocker blockTabs(tabs), blockTree(tree);
      for (int i = 0; i < tabs->count(); ++i)
        tabs->setTabVisible(i, tabs->widget(i)->property("settingsCategory") == category);
      tabs->setCurrentIndex(index); nodes.value(category)->setExpanded(true);
      tree->setCurrentItem(leaves.value(index)); tree->scrollToItem(leaves.value(index));
    }
    (*remembered)[category] = index;
    heading->setText(categories.value(category) + " — " + tabs->tabText(index));
    *active = false;
  };
  QObject::connect(tabs, &QTabWidget::currentChanged, dialog, activate);
  QObject::connect(tree, &QTreeWidget::currentItemChanged, dialog, [=](QTreeWidgetItem *item) {
    if (item) {
      const auto index = item->data(0, Qt::UserRole + 2);
      search->clear(); activate(index.isValid() ? index.toInt() : remembered->value(item->data(0, Qt::UserRole).toString()));
    }
  });
  QMap<QString, QString> aliases{{"IDD_PPAGELOGO", "IDD_PPAGEINTERFACE"}, {"videoColorPage", "IDD_PPAGECOLOR"},
      {"videoIccPage", "IDD_PPAGECOLOR"}, {"videoScalingPage", "IDD_PPAGEVIDEO"},
      {"videoDecodingPage", "IDD_PPAGEINTERNALFILTERS"}, {"videoSyncPage", "IDD_PPAGESYNC"}};
  const auto selected = aliases.value(initialPath, initialPath);
  activate(std::max(0, int(order.indexOf(selected))));
  const auto legacyPath = initialPath == "IDD_PPAGELOGO" ? QString("classicLogoPreview") : initialPath;
  if (auto *legacy = dialog->findChild<QWidget *>(legacyPath); !legacyPath.isEmpty() && legacy)
    reveal(legacy);

  struct Entry { QString text, terms; QWidget *target; };
  QList<Entry> entries;
  const QMap<QString, QString> synonyms{
      {"ui/rememberGeometry", uiTr("сохранить положение окна размер запоминать")},
      {"playback/rememberPosition", uiTr("сохранить позицию фильма продолжить просмотр")},
      {"appearance/themeBrightness", uiTr("яркость оформления темы")},
      {"video/brightness", uiTr("яркость видео изображения")},
      {"video/hardwareBackend", uiTr("кодеки аппаратное декодирование")},
      {"video/iccProfile", uiTr("цветовой профиль ICC")},
      {"ui/chapterMarkers", uiTr("линии разделения глав")}};
  for (const auto &id : order) {
    if (!loadingStep(dialog)) return;
    const auto &p = pages[id];
    const QString path = categories[p.category] + " → " + p.title;
    entries.append({path, path, p.scroll});
    for (auto *widget : p.scroll->findChildren<QWidget *>()) {
      const auto key = widget->objectName();
      if (widget->isHidden() || widget->property("settingsNavigation").toBool() || key.startsWith("qt_") ||
          (!qobject_cast<QAbstractButton *>(widget) && !qobject_cast<QAbstractSpinBox *>(widget) &&
           !qobject_cast<QComboBox *>(widget) && !qobject_cast<QLineEdit *>(widget) &&
           !qobject_cast<QPlainTextEdit *>(widget) && !qobject_cast<QKeySequenceEdit *>(widget) &&
           !qobject_cast<QSlider *>(widget))) continue;
      if (qobject_cast<QLineEdit *>(widget) && (qobject_cast<QAbstractSpinBox *>(widget->parentWidget()) || qobject_cast<QComboBox *>(widget->parentWidget()))) continue;
      QString label = widget->property("settingsLabel").toString();
      if (auto *button = qobject_cast<QAbstractButton *>(widget)) label = button->text();
      for (auto *form : p.scroll->findChildren<QFormLayout *>())
        if (auto *text = qobject_cast<QLabel *>(form->labelForField(widget))) label = text->text();
      if (label.isEmpty()) label = widget->accessibleName();
      if (label.isEmpty() && widget->parentWidget())
        for (auto *text : widget->parentWidget()->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly)) label += " " + text->text();
      label = plain(label);
      if (label.isEmpty() && key.isEmpty()) continue;
      const QString text = path + " — " + (label.isEmpty() ? key : label.left(160));
      entries.append({text, text + " " + key + " " + synonyms.value(key), widget});
    }
  }
  QObject::connect(search, &QLineEdit::textChanged, dialog, [=](const QString &query) {
    const auto words = query.simplified().split(' ', Qt::SkipEmptyParts);
    const bool searching = !words.isEmpty();
    results->setVisible(searching); tabs->setVisible(!searching);
    if (!searching) { activate(tabs->currentIndex()); return; }
    results->clear(); int count = 0;
    for (const auto &entry : entries) {
      bool matches = true;
      for (const auto &word : words) matches &= entry.terms.contains(word, Qt::CaseInsensitive);
      if (!matches) continue;
      ++count; if (count > 200) continue;
      auto *item = new QListWidgetItem(entry.text, results);
      item->setData(Qt::UserRole, QVariant::fromValue<QObject *>(entry.target));
    }
    heading->setText(uiTr("Найдено: %1").arg(count));
    if (results->count()) results->setCurrentRow(0);
    else { auto *item = new QListWidgetItem(uiTr("Настройки не найдены"), results); item->setFlags(Qt::NoItemFlags); }
  });
  const auto openResult = [=](QListWidgetItem *item) {
    auto *target = qobject_cast<QWidget *>(item->data(Qt::UserRole).value<QObject *>());
    if (!target) return;
    search->clear(); ClassicPreferences::selectPage(tabs, target); target->setFocus(Qt::OtherFocusReason);
  };
  QObject::connect(results, &QListWidget::itemActivated, dialog, openResult);
  QObject::connect(results, &QListWidget::itemClicked, dialog, openResult);
  QObject::connect(search, &QLineEdit::returnPressed, dialog, [=] {
    if (auto *item = results->currentItem(); item && results->isVisible()) openResult(item);
  });
  new SearchKeys(search, results, [=] { if (auto *item = results->currentItem()) openResult(item); });
}
