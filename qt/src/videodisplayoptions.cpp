#include "i18n.h"
#include "classicpreferences.h"
#include "player.h"
#include "videodisplay.h"
#include "window.h"
#include <QtWidgets>

void Window::attachVideoDisplayPreferences(
    QTabWidget *tabs, QList<std::function<void()>> *apply) {
  const auto v = VideoDisplay::values(profile.settings);
  const auto page = [tabs](const QString &title, const QString &name) {
    auto *widget = new QWidget;
    auto *form = new QFormLayout(widget);
    auto *scroll = new QScrollArea;
    scroll->setObjectName(name);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(widget);
    tabs->addTab(scroll, title);
    return form;
  };
  const auto note = [](QFormLayout *form, const QString &text) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    form->addRow(label);
    return label;
  };
  auto syncReset = std::make_shared<QList<std::function<void()>>>();
  const auto choice =
      [this, apply,
       v](QFormLayout *form, const QString &key, const QString &title,
          const QList<QPair<QString, QString>> &items,
          const std::shared_ptr<QList<std::function<void()>>> &reset) {
        auto *box = new QComboBox;
        box->setObjectName("video/" + key);
        for (const auto &item : items)
          box->addItem(item.second, item.first);
        box->setCurrentIndex(box->findData(v.value(key)));
        form->addRow(title, box);
        apply->append([this, key, box] {
          profile.settings.setValue("video/" + key, box->currentData());
        });
        reset->append([box] { box->setCurrentIndex(0); });
        return box;
      };
  auto *icc = page(uiTr("Видео — ICC-профиль"), "videoIccPage");
  auto *enabled =
      new QCheckBox(uiTr("Использовать ICC-профиль дисплея (OpenGL)"));
  enabled->setObjectName("video/iccEnabled");
  enabled->setChecked(v.value("iccEnabled").toBool());
  icc->addRow(enabled);
  auto *source = new QComboBox;
  source->setObjectName("video/iccSource");
  source->addItem(uiTr("Ручной профиль (по умолчанию)"), "manual");
  source->addItem(uiPlatformTr("Автоматически для текущего экрана (colord, X11)", "Автоматически для текущего экрана (Windows ICM)"),
                  "system");
  source->setCurrentIndex(source->findData(v.value("iccSource")));
  icc->addRow(uiTr("Источник профиля:"), source);
  auto *path = new QLineEdit(v.value("iccProfile").toString());
  path->setObjectName("video/iccProfile");
  path->setReadOnly(true);
  icc->addRow(uiTr("Сохранённый профиль:"), path);
  auto *row = new QWidget;
  auto *layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  auto *browse = new QPushButton(uiTr("Выбрать ICC…"));
  browse->setObjectName("videoIccBrowse");
  auto *clear = new QPushButton(uiTr("Убрать выбор"));
  layout->addWidget(browse);
  layout->addWidget(clear);
  icc->addRow(row);
  auto *error = note(icc, QString());
  error->setObjectName("videoIccError");
  connect(clear, &QPushButton::clicked, path, &QLineEdit::clear);
  connect(browse, &QPushButton::clicked, tabs, [this, path, error] {
    const auto file = QFileDialog::getOpenFileName(
        this, uiTr("ICC-профиль дисплея"), profile.directory,
        "ICC (*.icc *.icm);;All files (*)");
    if (file.isEmpty())
      return;
    QString failure;
    VideoDisplay::readIcc(file, &failure);
    error->setText(failure);
    if (failure.isEmpty())
      path->setText(file);
  });
  auto *intent = new QComboBox;
  intent->setObjectName("video/iccIntent");
  intent->addItem(uiTr("Относительный колориметрический (по умолчанию)"), 1);
  intent->addItem(uiTr("Перцепционный"), 0);
  intent->addItem(uiTr("Насыщенность"), 2);
  intent->addItem(uiTr("Абсолютный колориметрический"), 3);
  intent->setCurrentIndex(intent->findData(v.value("iccIntent")));
  icc->addRow(uiTr("Метод преобразования:"), intent);
  apply->append([this, enabled, source, path, intent] {
    profile.settings.setValue("video/iccEnabled", enabled->isChecked());
    profile.settings.setValue("video/iccSource", source->currentData());
    profile.settings.setValue("video/iccProfile", path->text());
    profile.settings.setValue("video/iccIntent", intent->currentData());
  });
  const auto updateControls = [=] {
    const bool manual = source->currentData().toString() == "manual";
    path->setEnabled(manual);
    browse->setEnabled(manual);
    clear->setEnabled(manual);
  };
  connect(source, &QComboBox::currentIndexChanged, tabs, updateControls);
  updateControls();
#ifdef Q_OS_WIN
  note(icc, uiTr("ICC действует только в OpenGL. Профиль текущего дисплея Windows проверяется LittleCMS "
      "и копируется в ColorProfiles рядом с программой. Автовыбор следует за экраном окна. "
      "Системные профили и калибровка не меняются."));
#else
  note(icc,
       uiTr("ICC действует только в OpenGL. RGB-профиль дисплея до 16 МиБ "
            "проверяется LittleCMS и копируется в ColorProfiles рядом с "
            "программой. Ручной выбор применяется после OK. Автовыбор "
            "colord в X11 следует за экраном окна и проверяет изменения "
            "раз в 5 секунд. Если профиль недоступен, ICC снимается; "
            "профиль другого экрана не используется. Ручной путь "
            "сохраняется отдельно. В Wayland автовыбор пока недоступен. "
            "LUT-кеш выключен; системные профили и калибровка не меняются."));
#endif
  auto *iccStatus = note(icc, QString());
  const auto iccUpdate = [this, iccStatus] {
    iccStatus->setText(
        player->QObject::property("foxicmpIccStatus").toString() + "\n" +
        uiTr("Фактический вывод: %1.").arg(player->outputName()));
  };
  iccUpdate();
  auto *iccTimer = new QTimer(iccStatus);
  iccTimer->setInterval(500);
  connect(iccTimer, &QTimer::timeout, iccStatus, iccUpdate);
  iccTimer->start();
  auto *resetIcc = new QPushButton(uiTr("Сбросить ICC"));
  resetIcc->setObjectName("videoIccReset");
  icc->addRow(resetIcc);
  connect(resetIcc, &QPushButton::clicked, tabs,
          [enabled, source, path, intent, error] {
            enabled->setChecked(false);
            source->setCurrentIndex(0);
            path->clear();
            intent->setCurrentIndex(0);
            error->clear();
          });

  auto *sync = page(uiTr("Видео — синхронизация"), "videoSyncPage");
  choice(sync, "syncMode", uiTr("Синхронизация:"),
         {{"audio", uiTr("По звуку (по умолчанию)")},
          {"display-resample", uiTr("По дисплею: ресэмплинг звука")},
          {"display-tempo", uiTr("По дисплею: изменение темпа звука")},
          {"display-vdrop", uiTr("По дисплею: пропуск/повтор видеокадров")}},
         syncReset);
  choice(sync, "vsync", uiTr("VSync Qt (после перезапуска):"),
         {{"on", uiTr("Включён, интервал 1 (по умолчанию)")},
          {"off", uiTr("Выключен, интервал 0")}},
         syncReset);
  const auto number = [this, apply, v, syncReset,
                       sync](const QString &key, const QString &title,
                             double maximum, int decimals, double def) {
    auto *spin = new QDoubleSpinBox;
    spin->setObjectName("video/" + key);
    spin->setRange(0, maximum);
    spin->setDecimals(decimals);
    spin->setValue(v.value(key).toDouble());
    sync->addRow(title, spin);
    apply->append([this, key, spin] {
      profile.settings.setValue("video/" + key, spin->value());
    });
    syncReset->append([spin, def] { spin->setValue(def); });
    return spin;
  };
  number("displayFps", uiTr("Частота дисплея, Гц (0 = Qt):"), 1000, 3, 0)
      ->setSpecialValueText(uiTr("Автоматически из Qt"));
  number("maxVideoChange", uiTr("Максимальная коррекция видео, %:"), 5, 3, 1);
  number("maxAudioChange", uiTr("Дополнительная коррекция звука, %:"), 1, 3,
         .125);
  auto *interpolation = new QCheckBox(uiTr("Интерполяция кадров OpenGL"));
  interpolation->setObjectName("video/interpolation");
  interpolation->setChecked(v.value("interpolation").toBool());
  sync->addRow(interpolation);
  apply->append([this, interpolation] {
    profile.settings.setValue("video/interpolation",
                              interpolation->isChecked());
  });
  syncReset->append([interpolation] { interpolation->setChecked(false); });
  choice(sync, "temporalScaler", uiTr("Временной фильтр:"),
         {{"default", uiTr("По умолчанию libmpv")},
          {"oversample", "Oversample"},
          {"linear", "Linear"},
          {"mitchell", "Mitchell"}},
         syncReset);
  note(sync,
       uiTr("По умолчанию сохранена синхронизация по звуку. Режимы дисплея "
            "требуют видимого OpenGL-вывода, VSync 1, известной частоты и "
            "выключенного bitstream. При резервном Software, сворачивании или "
            "конфликте временно используется звук, выбранный режим "
            "сохраняется. Интерполяция действует только в режиме дисплея. "
            "Частота Qt — подсказка, не измерение реального VBlank; драйвер и "
            "композитор могут игнорировать VSync. Настройка не меняет режим "
            "монитора и не включает HDR."));
  auto *status = note(sync, QString());
  status->setObjectName("videoSyncStatus");
  const auto update = [this, status] {
    const auto saved = VideoDisplay::values(profile.settings);
    status->setText(
        player->QObject::property("foxicmpTimingStatus").toString() + "\n" +
        uiTr(
            "Вывод: %1. Запрошенный Qt interval: %2; сохранённый: "
            "%3.\nvideo-sync: %4; display-sync-active: %5.\nПодсказка дисплея: "
            "%6 Гц; оценка libmpv: %7 Гц.\nA/V: %8 с; пропуски кадров: %9.")
            .arg(player->outputName())
            .arg(player->requestedSwapInterval())
            .arg(saved.value("vsync").toString())
            .arg(player->get("video-sync").toString(),
                 player->get("display-sync-active").toString())
            .arg(player->QObject::property("foxicmpDisplayRefreshHint")
                     .toDouble(),
                 0, 'f', 3)
            .arg(player->get("estimated-display-fps").toDouble(), 0, 'f', 3)
            .arg(player->get("avsync").toDouble(), 0, 'f', 4)
            .arg(player->get("frame-drop-count").toLongLong()));
  };
  update();
  auto *timer = new QTimer(status);
  timer->setInterval(500);
  connect(timer, &QTimer::timeout, status, update);
  timer->start();
  auto *resetSync = new QPushButton(uiTr("Сбросить синхронизацию"));
  resetSync->setObjectName("videoSyncReset");
  sync->addRow(resetSync);
  connect(resetSync, &QPushButton::clicked, tabs, [syncReset] {
    for (const auto &fn : *syncReset)
      fn();
  });
}
bool Window::validateVideoDisplayPreferences(QDialog *dialog) {
  auto *path = dialog->findChild<QLineEdit *>("video/iccProfile");
  auto *enabled = dialog->findChild<QCheckBox *>("video/iccEnabled");
  if (!path || !enabled)
    return true;
  if (auto *source = dialog->findChild<QComboBox *>("video/iccSource"))
    if (source->currentData().toString() == "system")
      return true;
  const auto text = path->text();
  if (!enabled->isChecked() &&
      (text.isEmpty() ||
       text == profile.settings.value("video/iccProfile").toString()))
    return true;
  QString error;
  const auto relative =
      VideoDisplay::importIcc(text, profile.directory, &error);
  if (!relative.isEmpty()) {
    path->setText(relative);
    return true;
  }
  dialog->findChild<QLabel *>("videoIccError")->setText(error);
  ClassicPreferences::selectPage(dialog->findChild<QTabWidget *>(),
      dialog->findChild<QWidget *>("videoIccPage"));
  return false;
}
