#include "audioprocessing.h"
#include "i18n.h"
#include "player.h"
#include "window.h"
#include <QtWidgets>

void Window::attachAudioProcessingPreferences(
    QTabWidget *tabs, QList<std::function<void()>> *apply) {
  auto save = std::make_shared<QList<std::function<void()>>>();
  auto reset = std::make_shared<QList<std::function<void()>>>();
  const auto v = AudioProcessing::values(profile.settings);
  const auto defaults = AudioProcessing::defaults();
  const auto page = [tabs](const QString &name) {
    auto *content = new QWidget;
    auto *form = new QFormLayout(content);
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    tabs->addTab(scroll, name);
    return form;
  };
  const auto note = [](QFormLayout *form, const QString &text) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    form->addRow(label);
  };
  const auto number = [this, v, defaults, save,
                       reset](QFormLayout *form, const QString &id,
                              const QString &title, double low, double high,
                              double step, int decimals) {
    auto *box = new QDoubleSpinBox;
    const auto key = "audio/" + id;
    box->setObjectName(key);
    box->setRange(low, high);
    box->setDecimals(decimals);
    box->setSingleStep(step);
    box->setValue(v.value(id).toDouble());
    form->addRow(title, box);
    save->append(
        [this, key, box] { profile.settings.setValue(key, box->value()); });
    reset->append(
        [box, defaults, id] { box->setValue(defaults.value(id).toDouble()); });
    return box;
  };
  const auto check = [this, tabs, v, defaults, save,
                      reset](QFormLayout *form, const QString &id,
                             const QString &title) {
    auto *box = new QCheckBox(title);
    const auto key = "audio/" + id;
    box->setObjectName(key);
    box->setChecked(v.value(id).toBool());
    form->addRow(box);
    // Keep the original Audio page's normalization checkbox synchronized.
    for (auto *other : tabs->findChildren<QCheckBox *>(key)) {
      if (other == box)
        continue;
      QObject::connect(box, &QCheckBox::toggled, other, &QCheckBox::setChecked);
      QObject::connect(other, &QCheckBox::toggled, box, &QCheckBox::setChecked);
    }
    save->append(
        [this, key, box] { profile.settings.setValue(key, box->isChecked()); });
    reset->append(
        [box, defaults, id] { box->setChecked(defaults.value(id).toBool()); });
    return box;
  };
  const auto choice = [this, v, defaults, save,
                       reset](QFormLayout *form, const QString &id,
                              const QString &title,
                              const QList<QPair<QString, QString>> &items) {
    auto *box = new QComboBox;
    const auto key = "audio/" + id;
    box->setObjectName(key);
    for (const auto &item : items)
      box->addItem(item.second, item.first);
    box->setCurrentIndex(box->findData(v.value(id)));
    form->addRow(title, box);
    save->append([this, key, box] {
      profile.settings.setValue(key, box->currentData());
    });
    reset->append([box, defaults, id] {
      box->setCurrentIndex(box->findData(defaults.value(id)));
    });
    return box;
  };
  auto *processing = page(uiTr("Звук — обработка"));
  number(processing, "gainDb", uiTr("Дополнительное усиление, дБ:"), -20, 20,
         .5, 2);
  number(processing, "balance", uiTr("Баланс: −100 влево, +100 вправо:"), -100,
         100, 5, 0);
  choice(processing, "channelMode", uiTr("Каналы стерео:"),
         {{"normal", uiTr("Обычный порядок")},
          {"swap", uiTr("Поменять левый и правый")},
          {"left", uiTr("Левый в оба канала")},
          {"right", uiTr("Правый в оба канала")},
          {"mono", uiTr("Среднее левого и правого")}});
  note(
      processing,
      uiTr(
          "Ненулевой баланс и специальные режимы каналов включают стереовывод. "
          "Нулевой баланс и обычный порядок сохраняют выбранную раскладку. "
          "Обработка применяется к декодированному PCM. Настройки системного "
          "микшера не меняются."));
  check(processing, "limiter", uiTr("Ограничивать пики после обработки"));
  number(processing, "limiterPeak",
         uiTr("Максимальный пик (1 = полный уровень):"), .1, 1, .05, 2);
  choice(processing, "replaygain", "ReplayGain:",
         {{"no", uiTr("Выключен")},
          {"track", uiTr("По тегам дорожки")},
          {"album", uiTr("По тегам альбома")}});
  number(processing, "replaygainPreamp", uiTr("Предусиление ReplayGain, дБ:"),
         -15, 15, .5, 2);
  number(processing, "replaygainFallback",
         uiTr("Усиление без тегов ReplayGain, дБ:"), -15, 15, .5, 2);
  check(processing, "replaygainClip",
        uiTr("ReplayGain: ограничивать усиление по тегу пика"));
  note(processing, uiTr("Порядок: микшер → каналы/баланс → усиление → "
                        "нормализация → ограничитель. "
                        "ReplayGain использует метаданные отдельно. Включённые "
                        "вместе виды усиления суммируют эффект. "
                        "Динамическая нормализация добавляет задержку из-за "
                        "анализа соседних фрагментов."));
  auto *restore = new QPushButton(uiTr("Сбросить обработку звука"));
  restore->setObjectName("audioProcessingReset");
  processing->addRow(restore);
  connect(restore, &QPushButton::clicked, tabs, [reset] {
    for (const auto &fn : *reset)
      fn();
  });
  auto *normalizer = page(uiTr("Звук — нормализация"));
  check(normalizer, "normalize", uiTr("Включить динамическую нормализацию"));
  number(normalizer, "normalizeFrameMs", uiTr("Длина фрагмента, мс:"), 10, 1000,
         10, 0);
  auto *window = number(normalizer, "normalizeWindow",
                        uiTr("Окно сглаживания (нечётное):"), 3, 101, 2, 0);
  connect(window, &QDoubleSpinBox::editingFinished, window, [window] {
    window->setValue(std::clamp(qRound(window->value()) | 1, 3, 101));
  });
  number(normalizer, "normalizePeak", uiTr("Целевой пик (0,1–1):"), .1, 1, .05,
         2);
  number(normalizer, "normalizeMaxGain", uiTr("Предельное усиление, раз:"), 1,
         100, 1, 1);
  number(normalizer, "normalizeRms", uiTr("Целевой RMS (0 — только пики):"), 0,
         1, .01, 3);
  check(normalizer, "normalizeCouple", uiTr("Связывать усиление каналов"));
  check(normalizer, "normalizeDc", uiTr("Убирать постоянную составляющую"));
  note(normalizer,
       uiTr("По умолчанию сохранён прежний алгоритм: 250 мс, окно 31, пик 0,9, "
            "усиление до 10 раз. Чётное окно округляется вверх. "
            "Исходный файл не изменяется. Нормализация и ограничитель "
            "выключены по умолчанию."));
  auto *mix = page(uiTr("Звук — микшер"));
  check(mix, "mixer", uiTr("Использовать мой микшер каналов"));
  choice(mix, "mixLayout", uiTr("Выход микшера:"),
         {{"mono", uiTr("Моно")},
          {"stereo", uiTr("Стерео")},
          {"quad", "Quad (FL FR BL BR)"},
          {"4.0", "4.0 (FL FR FC BC)"},
          {"5.0", "5.0"},
          {"5.1", "5.1"},
          {"5.1(side)", "5.1 (side)"},
          {"7.1", "7.1"}});
  number(mix, "centerLevel", uiTr("Уровень центра при сведении, раз:"), 0, 2,
         .05, 4);
  number(mix, "surroundLevel", uiTr("Уровень тылов при сведении, раз:"), 0, 2,
         .05, 4);
  number(mix, "lfeLevel", uiTr("Уровень LFE при сведении, раз:"), 0, 2, .05, 4);
  check(mix, "mixNormalize",
        uiTr("Нормировать матрицу сведения для защиты от перегрузки"));
  note(
      mix,
      uiTr("Уровни задают вклад каналов, отсутствующих в выходной раскладке. "
           "Например, при 5.1 → стерео центр и LFE смешиваются с левым и "
           "правым. "
           "Это не отдельная громкость колонки, которая сохраняется на выходе. "
           "FFmpeg выбирает стандартную матрицу для выбранных раскладок; "
           "микшер не создаёт объёмную сцену из стерео. Устройство или "
           "системный сервер "
           "может выполнить дополнительное сведение."));
  auto *info = new QLabel;
  info->setObjectName("audioProcessingOutputInfo");
  info->setWordWrap(true);
  const auto update = [this, info] {
    const auto decoded = player->get("audio-params").toMap();
    const auto output = player->get("audio-out-params").toMap();
    info->setText(uiTr("Декодировано: %1; %2 Гц; %3.\nВывод плеера: %4; %5 Гц; "
                       "%6.\nУстройство: %7")
                      .arg(decoded.value("channels", "—").toString(),
                           decoded.value("samplerate", "—").toString(),
                           decoded.value("format", "—").toString(),
                           output.value("channels", "—").toString(),
                           output.value("samplerate", "—").toString(),
                           output.value("format", "—").toString(),
                           player->get("audio-device").toString()));
  };
  update();
  auto *timer = new QTimer(info);
  timer->setInterval(1000);
  connect(timer, &QTimer::timeout, info, update);
  timer->start();
  mix->addRow(info);
  attachAudioSourcePreferences(tabs, apply);
  apply->append([this, save] {
    for (const auto &fn : *save)
      fn();
    // Persist sanitized values, including an odd normalizer window.
    const auto sanitized = AudioProcessing::values(profile.settings);
    for (auto it = sanitized.cbegin(); it != sanitized.cend(); ++it)
      if (it.key() != "channels")
        profile.settings.setValue("audio/" + it.key(), it.value());
  });
  attachAudioFilterPreferences(tabs, apply);
  attachAudioEncodingPreferences(tabs, apply);
}
void Window::audioProcessingDialog() {
  QDialog dialog(this);
  dialog.setObjectName("audioProcessingDialog");
  dialog.setWindowTitle(uiTr("Обработка и микширование звука"));
  dialog.resize(860, 680);
  auto *layout = new QVBoxLayout(&dialog);
  auto *tabs = new QTabWidget;
  layout->addWidget(tabs);
  QList<std::function<void()>> apply;
  attachAudioProcessingPreferences(tabs, &apply);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [this, &dialog] {
    if (validateAudioFilterPreferences(&dialog))
      dialog.accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() == QDialog::Accepted) {
    for (const auto &fn : apply)
      fn();
    applyMediaOptions();
  }
}
