#include "framepreview.h"
#include "i18n.h"
#include "mediaoptions.h"
#include "player.h"
#include "window.h"
#include <QtWidgets>
#include <cmath>
namespace {
QJsonArray readPresets(QSettings &settings) {
  const auto bytes = settings.value("transform/presetsV1").toByteArray();
  if (bytes.size() > 256 * 1024)
    return {};
  const auto array = QJsonDocument::fromJson(bytes).array();
  QJsonArray result;
  for (const auto &value : array) {
    const auto entry = value.toObject();
    const auto name = entry.value("name").toString().trimmed().left(80);
    if (name.isEmpty() || !entry.value("state").isObject())
      continue;
    result.append(QJsonObject{
        {"name", name},
        {"state", QJsonObject::fromVariantMap(
                      VideoTransform::fromMap(
                          entry.value("state").toObject().toVariantMap())
                          .toMap())}});
    if (result.size() >= 32)
      break;
  }
  return result;
}
} // namespace
QString Window::previewVideoFilters() const {
  QStringList filters;
  const auto transform = videoTransform.sourceFilters(transformStereoHint);
  if (!transform.isEmpty())
    filters << transform;
  const auto color = MediaOptions::colorFilter(profile.settings);
  if (!color.isEmpty())
    filters << color;
  return filters.join(',');
}
bool Window::applyVideoTransform() {
  if (applyingTransform)
    return true;
  applyingTransform = true;
  const bool ok = videoTransform.apply(
      player, MediaOptions::colorFilter(profile.settings), transformStereoHint);
  if (ok)
    lastAppliedTransform = videoTransform;
  else {
    videoTransform = lastAppliedTransform;
    lastAppliedTransform.apply(player,
                               MediaOptions::colorFilter(profile.settings),
                               transformStereoHint);
  }
  applyingTransform = false;
  if (preview)
    preview->setFilters(previewVideoFilters());
  return ok;
}
void Window::applyMediaOptions() {
  MediaOptions::apply(player, profile.settings);
  applyAudioSourceOptions();
  applySubtitleOptions();
  if (!applyVideoTransform())
    showOsd(uiTr("Преобразование недоступно для этого видео"));
}
void Window::transformChanged() {
  videoTransform = VideoTransform::fromMap(videoTransform.toMap());
  if (!applyVideoTransform()) {
    showOsd(uiTr("Преобразование недоступно для этого видео"));
    return;
  }
  videoTransform.save(profile.settings);
  if (actions.contains("mirrorHorizontal"))
    actions["mirrorHorizontal"]->setChecked(videoTransform.mirror);
  if (actions.contains("mirrorVertical"))
    actions["mirrorVertical"]->setChecked(videoTransform.flip);
  if (actions.contains("stereoSwap"))
    actions["stereoSwap"]->setChecked(videoTransform.swapEyes);
}
void Window::captureTransformGeometry() {
  videoTransform.x = player->get("video-pan-x").toDouble();
  videoTransform.y = player->get("video-pan-y").toDouble();
  videoTransform.zoom = player->get("video-zoom").toDouble();
  videoTransform.fill = player->get("panscan").toDouble();
  lastAppliedTransform = videoTransform;
  videoTransform.save(profile.settings);
}
void Window::buildTransformActions() {
  const auto add = [&](const QString &id, const QString &label,
                       const QString &key, std::function<void()> fn) {
    auto *action = new QAction(label, this);
    action->setObjectName("action_" + id);
    actions[id] = action;
    defaultKeys[id] = key;
    addAction(action);
    connect(action, &QAction::triggered, this, [this, fn] {
      cancelMouseHold();
      if (clickTimer)
        clickTimer->stop();
      showFullscreenControls();
      fn();
    });
    return action;
  };
  add("transformDialog", uiTr("Pan&Scan и преобразования…"), "Ctrl+Alt+P",
      [this] { transformDialog(); });
  add("transformReset", uiTr("Сбросить все преобразования"), "", [this] {
    videoTransform = {};
    transformChanged();
  });
  add("panCenter", uiTr("Центрировать изображение"), "", [this] {
    videoTransform.x = videoTransform.y = 0;
    transformChanged();
  });
  for (const auto &entry : QList<QPair<QString, QString>>{
           {"widthUp", uiTr("Увеличить ширину")},
           {"widthDown", uiTr("Уменьшить ширину")},
           {"heightUp", uiTr("Увеличить высоту")},
           {"heightDown", uiTr("Уменьшить высоту")},
           {"scaleUp", uiTr("Увеличить ширину и высоту")},
           {"scaleDown", uiTr("Уменьшить ширину и высоту")}}) {
    add(entry.first, entry.second, "", [this, id = entry.first] {
      const double factor = id.endsWith("Up") ? 1.02 : 1 / 1.02;
      if (!id.startsWith("height"))
        videoTransform.width =
            std::clamp(videoTransform.width * factor, .2, 5.);
      if (!id.startsWith("width"))
        videoTransform.height =
            std::clamp(videoTransform.height * factor, .2, 5.);
      transformChanged();
    });
  }
  for (const auto &entry : QList<QPair<QString, QString>>{
           {"panUpLeft", uiTr("Сдвинуть вверх и влево")},
           {"panUpRight", uiTr("Сдвинуть вверх и вправо")},
           {"panDownLeft", uiTr("Сдвинуть вниз и влево")},
           {"panDownRight", uiTr("Сдвинуть вниз и вправо")}}) {
    add(entry.first, entry.second, "", [this, id = entry.first] {
      videoTransform.x = std::clamp(
          videoTransform.x + (id.endsWith("Left") ? -.02 : .02), -3., 3.);
      videoTransform.y = std::clamp(
          videoTransform.y + (id.startsWith("panUp") ? -.02 : .02), -3., 3.);
      transformChanged();
    });
  }
  add("rotateClockwise", uiTr("Повернуть по часовой стрелке"), "", [this] {
    videoTransform.rotation = (videoTransform.rotation + 90) % 360;
    transformChanged();
  });
  add("rotateCounterclockwise", uiTr("Повернуть против часовой стрелки"), "",
      [this] {
        videoTransform.rotation = (videoTransform.rotation + 270) % 360;
        transformChanged();
      });
  for (const auto &entry : QList<QPair<QString, QString>>{
           {"mirrorHorizontal", uiTr("Отразить по горизонтали")},
           {"mirrorVertical", uiTr("Отразить по вертикали")},
           {"stereoSwap", uiTr("Поменять левый и правый глаз")}}) {
    auto *action = add(entry.first, entry.second, "", [this, id = entry.first] {
      if (id == "mirrorHorizontal")
        videoTransform.mirror = actions[id]->isChecked();
      else if (id == "mirrorVertical")
        videoTransform.flip = actions[id]->isChecked();
      else
        videoTransform.swapEyes = actions[id]->isChecked();
      transformChanged();
    });
    action->setCheckable(true);
    action->setChecked(entry.first == "mirrorHorizontal" ? videoTransform.mirror
                       : entry.first == "mirrorVertical"
                           ? videoTransform.flip
                           : videoTransform.swapEyes);
  }
  for (const auto &entry : QList<QPair<QString, QString>>{
           {"pass", uiTr("Без преобразования формата")},
           {"ml", uiTr("Моно — левый глаз")},
           {"mr", uiTr("Моно — правый глаз")},
           {"irl", uiTr("Чередование строк")},
           {"irl2", uiTr("Чередование строк — размер 2×")},
           {"abl", uiTr("Over/under")},
           {"ab2l", uiTr("Half over/under")},
           {"sbsl", uiTr("Side-by-side")},
           {"sbs2l", uiTr("Half side-by-side")},
           {"arcd", uiTr("Анаглиф красный/голубой — Dubois")},
           {"arcc", uiTr("Анаглиф красный/голубой — цветной")},
           {"icl", uiTr("Чередование столбцов")},
           {"chl", uiTr("Шахматный порядок")}}) {
    auto *action = add("stereoOutput_" + entry.first, entry.second, "",
                       [this, value = entry.first] {
                         videoTransform.stereoOutput = value;
                         transformChanged();
                       });
    action->setCheckable(true);
  }
  add("transformSavePreset", uiTr("Сохранить текущие преобразования…"), "",
      [this] { saveTransformPreset(); });
  for (const auto &entry : QList<QPair<QString, QString>>{
           {"fit", uiTr("Исходный кадр")},
           {"fill", uiTr("Заполнить окно")},
           {"wide", uiTr("Растянуть 4:3 до 16:9")},
           {"crop169", uiTr("Обрезать до 16:9")},
           {"crop235", uiTr("Обрезать до 2.35:1")}}) {
    add("transformPreset_" + entry.first, entry.second, "",
        [this, id = entry.first] {
          if (VideoTransform::preset(profile.settings, id, &videoTransform))
            transformChanged();
        });
  }
}
void Window::refreshTransformPresets(QMenu *menu) {
  menu->clear();
  for (const auto &id : {"fit", "fill", "wide", "crop169", "crop235"})
    menu->addAction(actions["transformPreset_" + QString(id)]);
  menu->addSeparator();
  const auto savedPresets = readPresets(profile.settings);
  for (const auto &value : savedPresets) {
    const auto entry = value.toObject();
    const auto state =
        VideoTransform::fromMap(entry.value("state").toObject().toVariantMap());
    auto *action =
        menu->addAction(entry.value("name").toString(), this, [this, state] {
          videoTransform = state;
          transformChanged();
        });
    action->setData(entry.value("state").toObject().toVariantMap());
  }
  if (!savedPresets.isEmpty()) menu->addSeparator();
  menu->addAction(actions["transformSavePreset"]);
  menu->addAction(actions["transformDialog"]);
}
void Window::buildTransformMenu(QMenu *parent) {
  auto *menu = parent->addMenu(uiTr("Pan&Scan и преобразования"));
  menu->setObjectName("transformMenu");
  menu->addAction(actions["transformDialog"]);
  for (const auto &id : {"scaleUp", "scaleDown", "widthUp", "widthDown",
                         "heightUp", "heightDown"})
    menu->addAction(actions[id]);
  menu->addSeparator();
  for (const auto &id :
       {"panLeft", "panRight", "panUp", "panDown", "panUpLeft", "panUpRight",
        "panDownLeft", "panDownRight", "panCenter"})
    menu->addAction(actions[id]);
  menu->addSeparator();
  for (const auto &id :
       {"rotateClockwise", "rotateCounterclockwise", "mirrorHorizontal",
        "mirrorVertical", "stereoSwap", "transformReset"})
    menu->addAction(actions[id]);
  auto *stereo = menu->addMenu(uiTr("Вывод стерео 3D"));
  auto *group = new QActionGroup(stereo);
  for (const auto &id : {"pass", "ml", "mr", "irl", "irl2", "abl", "ab2l",
                         "sbsl", "sbs2l", "arcd", "arcc", "icl", "chl"}) {
    auto *action = actions["stereoOutput_" + QString(id)];
    stereo->addAction(action);
    // Actions are shared by both menus; one group owns exclusivity.
    if (!action->actionGroup())
      group->addAction(action);
  }
  stereo->addSeparator();
  stereo->addAction(actions["stereoSwap"]);
  connect(stereo, &QMenu::aboutToShow, this, [this] {
    for (auto it = actions.begin(); it != actions.end(); ++it)
      if (it.key().startsWith("stereoOutput_"))
        it.value()->setChecked(it.key().mid(13) == videoTransform.stereoOutput);
  });
  auto *presets = menu->addMenu(uiTr("Пресеты преобразований"));
  presets->setObjectName("transformPresetsMenu");
  connect(presets, &QMenu::aboutToShow, this,
          [this, presets] { refreshTransformPresets(presets); });
  refreshTransformPresets(presets);
}
void Window::saveTransformPreset() {
  auto presets = readPresets(profile.settings);
  if (presets.size() >= 32) {
    showOsd(uiTr("Достигнут предел: 32 пресета"));
    return;
  }
  bool ok = false;
  auto name = QInputDialog::getText(this, uiTr("Сохранить преобразования"),
                                    uiTr("Название пресета:"),
                                    QLineEdit::Normal, {}, &ok)
                  .trimmed()
                  .left(80);
  if (!ok || name.isEmpty())
    return;
  presets.append(QJsonObject{
      {"name", name},
      {"state", QJsonObject::fromVariantMap(videoTransform.toMap())}});
  profile.settings.setValue(
      "transform/presetsV1",
      QJsonDocument(presets).toJson(QJsonDocument::Compact));
}
void Window::attachTransformPreferences(QTabWidget *tabs,
                                        QList<std::function<void()>> *apply) {
  auto draft = std::make_shared<VideoTransform>(videoTransform);
  auto presets = std::make_shared<QJsonArray>(readPresets(profile.settings));
  auto page = [tabs](const QString &name) {
    auto *widget = new QWidget;
    auto *form = new QFormLayout(widget);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(widget);
    scroll->setFrameShape(QFrame::NoFrame);
    tabs->addTab(scroll, name);
    return form;
  };
  QMap<QString, QDoubleSpinBox *> fields;
  const auto number = [&](QFormLayout *form, const QString &label,
                          const QString &key, double value, double min,
                          double max, double step, const QString &suffix) {
    auto *box = new QDoubleSpinBox;
    box->setDecimals(2);
    box->setRange(min, max);
    box->setSingleStep(step);
    box->setSuffix(suffix);
    box->setValue(value);
    box->setObjectName("transform/" + key);
    form->addRow(label, box);
    fields[key] = box;
    return box;
  };
  auto *pan = page(uiTr("Pan&Scan"));
  number(pan, uiTr("Ширина, %:"), "width", draft->width * 100, 20, 500, 2, "%");
  number(pan, uiTr("Высота, %:"), "height", draft->height * 100, 20, 500, 2,
         "%");
  number(pan, uiTr("Смещение X, % кадра:"), "x", draft->x * 100, -300, 300, 2,
         "%");
  number(pan, uiTr("Смещение Y, % кадра:"), "y", draft->y * 100, -300, 300, 2,
         "%");
  number(pan, uiTr("Общий масштаб (log₂):"), "zoom", draft->zoom, -3, 3, .1,
         {});
  number(pan, uiTr("Заполнение окна, %:"), "fill", draft->fill * 100, 0, 100,
         10, "%");
  auto *rotation = new QComboBox;
  rotation->setObjectName("video/rotation");
  for (int degrees : {0, 90, 180, 270})
    rotation->addItem(QString::number(degrees) + "°", degrees);
  rotation->setCurrentIndex(std::max(0, rotation->findData(draft->rotation)));
  pan->addRow(uiTr("Поворот по часовой стрелке"), rotation);
  auto check = [&](QFormLayout *form, const QString &label, const QString &key,
                   bool value) {
    auto *box = new QCheckBox(label);
    box->setObjectName("transform/" + key);
    box->setChecked(value);
    form->addRow(box);
    return box;
  };
  auto *mirror =
      check(pan, uiTr("Отразить по горизонтали"), "mirror", draft->mirror);
  auto *flip = check(pan, uiTr("Отразить по вертикали"), "flip", draft->flip);
  auto *remember =
      check(pan, uiTr("Сохранять преобразования между запусками"), "remember",
            profile.settings.value("transform/remember", false).toBool());
  auto *note = new QLabel(
      uiTr("Без сохранения преобразования действуют до закрытия плеера. "
           "Пресеты хранятся отдельно. Центрирование не сбрасывает масштаб."));
  note->setWordWrap(true);
  pan->addRow(note);
  auto *crop = page(uiTr("Кадрирование"));
  for (const auto &entry :
       QList<QPair<QString, QString>>{{"left", uiTr("Слева, %:")},
                                      {"right", uiTr("Справа, %:")},
                                      {"top", uiTr("Сверху, %:")},
                                      {"bottom", uiTr("Снизу, %:")}})
    number(crop, entry.second, entry.first,
           draft->toMap().value(entry.first).toDouble(), 0, 45, 1, "%");
  auto *aspect = new QComboBox;
  aspect->setObjectName("transform/cropAspect");
  aspect->addItem(uiTr("Не обрезать по пропорциям"), "no");
  for (const auto &ratio : {"4:3", "16:9", "1.85:1", "2.35:1", "2.39:1"})
    aspect->addItem(ratio, ratio);
  aspect->setCurrentIndex(std::max(0, aspect->findData(draft->cropAspect)));
  crop->addRow(uiTr("Центральное кадрирование:"), aspect);
  auto *cropNote =
      new QLabel(uiTr("Сначала отрезаются края, затем применяется центральное "
                      "кадрирование. Размеры округляются до чётных пикселей. "
                      "Поворот и отражение применяются после обрезки."));
  cropNote->setWordWrap(true);
  crop->addRow(cropNote);
  auto *stereo = page(uiTr("Стерео 3D"));
  const auto combo = [&](QFormLayout *form, const QString &label,
                         const QString &key,
                         const QList<QPair<QString, QString>> &entries,
                         const QString &selected) {
    auto *box = new QComboBox;
    box->setObjectName("transform/" + key);
    for (const auto &entry : entries)
      box->addItem(entry.second, entry.first);
    box->setCurrentIndex(std::max(0, box->findData(selected)));
    form->addRow(label, box);
    return box;
  };
  auto *input = combo(stereo, uiTr("Формат исходного 3D:"), "stereoInput",
                      {{"auto", uiTr("Авто — только по метаданным")},
                       {"mono", uiTr("Обычное 2D")},
                       {"sbsl", uiTr("Side-by-side, левый слева")},
                       {"sbsr", uiTr("Side-by-side, правый слева")},
                       {"sbs2l", uiTr("Half side-by-side, левый слева")},
                       {"sbs2r", uiTr("Half side-by-side, правый слева")},
                       {"abl", uiTr("Over/under, левый сверху")},
                       {"abr", uiTr("Over/under, правый сверху")},
                       {"ab2l", uiTr("Half over/under, левый сверху")},
                       {"ab2r", uiTr("Half over/under, правый сверху")},
                       {"irl", uiTr("Строки, левый первым")},
                       {"irr", uiTr("Строки, правый первым")},
                       {"icl", uiTr("Столбцы, левый первым")},
                       {"icr", uiTr("Столбцы, правый первым")}},
                      draft->stereoInput);
  auto *output = combo(stereo, uiTr("Вывод 3D:"), "stereoOutput",
                       {{"pass", uiTr("Без преобразования формата")},
                        {"ml", uiTr("Моно — левый глаз")},
                        {"mr", uiTr("Моно — правый глаз")},
                        {"irl", uiTr("Чередование строк")},
                        {"irl2", uiTr("Чередование строк — размер 2×")},
                        {"abl", uiTr("Over/under")},
                        {"ab2l", uiTr("Half over/under")},
                        {"sbsl", uiTr("Side-by-side")},
                        {"sbs2l", uiTr("Half side-by-side")},
                        {"arcd", uiTr("Анаглиф красный/голубой — Dubois")},
                        {"arcc", uiTr("Анаглиф красный/голубой — цветной")},
                        {"icl", uiTr("Чередование столбцов")},
                        {"chl", uiTr("Шахматный порядок")}},
                       draft->stereoOutput);
  auto *detected = new QLabel(
      uiTr("Метаданные 3D: %1")
          .arg(VideoTransform{}.resolvedStereo(transformStereoHint).isEmpty()
                   ? uiTr("не распознаны")
                   : transformStereoHint));
  detected->setWordWrap(true);
  stereo->addRow(detected);
  auto *swap = check(stereo, uiTr("Поменять левый и правый глаз"), "swapEyes",
                     draft->swapEyes);
  auto *stereoNote = new QLabel(uiTr(
      "Авто не угадывает 3D по имени файла. Если метаданных нет, выберите "
      "формат явно. Обычный 2D не превращается в настоящее 3D. MVC/Blu-ray 3D "
      "и управление затворными очками здесь не реализованы."));
  stereoNote->setWordWrap(true);
  stereo->addRow(stereoNote);
  auto *presetForm = page(uiTr("Пресеты Pan&Scan"));
  auto *list = new QListWidget;
  list->setObjectName("transformPresetList");
  list->setMinimumHeight(150);
  list->setDragDropMode(QAbstractItemView::InternalMove);
  presetForm->addRow(list);
  for (const auto &value : *presets) {
    const auto entry = value.toObject();
    auto *item = new QListWidgetItem(entry.value("name").toString(), list);
    item->setData(Qt::UserRole, entry.value("state").toObject().toVariantMap());
    item->setFlags(item->flags() | Qt::ItemIsEditable);
  }
  auto *name = new QLineEdit;
  name->setObjectName("transformPresetName");
  name->setMaxLength(80);
  presetForm->addRow(uiTr("Название пресета:"), name);
  auto *buttons = new QWidget;
  auto *row = new QHBoxLayout(buttons);
  row->setContentsMargins(0, 0, 0, 0);
  auto *store = new QPushButton(uiTr("Добавить из настроек"));
  store->setObjectName("transformPresetAdd");
  auto *load = new QPushButton(uiTr("Загрузить в настройки"));
  load->setObjectName("transformPresetLoad");
  auto *replace = new QPushButton(uiTr("Обновить выбранный"));
  replace->setObjectName("transformPresetReplace");
  auto *remove = new QPushButton(uiTr("Удалить пресет"));
  remove->setObjectName("transformPresetRemove");
  row->addWidget(store);
  row->addWidget(load);
  row->addWidget(replace);
  row->addWidget(remove);
  presetForm->addRow(buttons);
  const auto collect = [fields, rotation, mirror, flip, aspect, input, output,
                        swap] {
    QVariantMap map;
    for (auto it = fields.begin(); it != fields.end(); ++it)
      map[it.key()] = it.value()->value();
    for (const auto &key : {"width", "height", "x", "y", "fill"})
      map[key] = map.value(key).toDouble() / 100;
    map["rotation"] = rotation->currentData();
    map["mirror"] = mirror->isChecked();
    map["flip"] = flip->isChecked();
    map["cropAspect"] = aspect->currentData();
    map["stereoInput"] = input->currentData();
    map["stereoOutput"] = output->currentData();
    map["swapEyes"] = swap->isChecked();
    return VideoTransform::fromMap(map);
  };
  const auto populate = [fields, rotation, mirror, flip, aspect, input, output,
                         swap](const VideoTransform &state) {
    auto map = state.toMap();
    for (const auto &key : {"width", "height", "x", "y", "fill"})
      map[key] = map.value(key).toDouble() * 100;
    for (auto it = fields.begin(); it != fields.end(); ++it)
      it.value()->setValue(map.value(it.key()).toDouble());
    rotation->setCurrentIndex(rotation->findData(state.rotation));
    mirror->setChecked(state.mirror);
    flip->setChecked(state.flip);
    aspect->setCurrentIndex(aspect->findData(state.cropAspect));
    input->setCurrentIndex(input->findData(state.stereoInput));
    output->setCurrentIndex(output->findData(state.stereoOutput));
    swap->setChecked(state.swapEyes);
  };
  connect(store, &QPushButton::clicked, list, [list, name, collect] {
    const auto title = name->text().trimmed();
    if (title.isEmpty() || list->count() >= 32)
      return;
    auto *item = new QListWidgetItem(title, list);
    item->setData(Qt::UserRole, collect().toMap());
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    list->setCurrentItem(item);
  });
  connect(replace, &QPushButton::clicked, list, [list, collect] {
    if (auto *item = list->currentItem())
      item->setData(Qt::UserRole, collect().toMap());
  });
  connect(load, &QPushButton::clicked, list, [list, populate] {
    if (auto *item = list->currentItem())
      populate(VideoTransform::fromMap(item->data(Qt::UserRole).toMap()));
  });
  connect(remove, &QPushButton::clicked, list,
          [list] { delete list->takeItem(list->currentRow()); });
  auto *reset = new QPushButton(uiTr("Сбросить все преобразования"));
  reset->setObjectName("transformResetButton");
  pan->addRow(reset);
  connect(reset, &QPushButton::clicked, tabs,
          [populate] { populate(VideoTransform{}); });
  auto *center = new QPushButton(uiTr("Центрировать изображение"));
  pan->addRow(center);
  connect(center, &QPushButton::clicked, tabs, [fields] {
    fields["x"]->setValue(0);
    fields["y"]->setValue(0);
  });
  apply->append([this, collect, remember, list] {
    videoTransform = collect();
    profile.settings.setValue("transform/remember", remember->isChecked());
    // New transform controls supersede the old always-persistent rotation.
    profile.settings.setValue("video/rotation", 0);
    if (!remember->isChecked())
      profile.settings.remove("transform/stateV1");
    QJsonArray array;
    for (int i = 0; i < list->count(); ++i) {
      auto *item = list->item(i);
      const auto name = item->text().trimmed().left(80);
      if (!name.isEmpty())
        array.append(QJsonObject{
            {"name", name},
            {"state",
             QJsonObject::fromVariantMap(
                 VideoTransform::fromMap(item->data(Qt::UserRole).toMap())
                     .toMap())}});
    }
    profile.settings.setValue(
        "transform/presetsV1",
        QJsonDocument(array).toJson(QJsonDocument::Compact));
    transformChanged();
  });
}
void Window::transformDialog() {
  QDialog dialog(this);
  dialog.setObjectName("transformDialog");
  dialog.setWindowTitle(uiTr("Pan&Scan и преобразования"));
  dialog.resize(760, 590);
  auto *layout = new QVBoxLayout(&dialog);
  auto *tabs = new QTabWidget;
  layout->addWidget(tabs);
  QList<std::function<void()>> apply;
  attachTransformPreferences(tabs, &apply);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() == QDialog::Accepted)
    for (const auto &save : apply)
      save();
}
