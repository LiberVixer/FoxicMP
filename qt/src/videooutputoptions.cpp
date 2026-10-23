#include "i18n.h"
#include "player.h"
#include "videooutput.h"
#include "window.h"
#include <QtWidgets>

void Window::attachVideoOutputPreferences(QTabWidget *tabs,
                                          QList<std::function<void()>> *apply) {
  const auto v = VideoOutput::values(profile.settings);
  auto reset = std::make_shared<QList<std::function<void()>>>();
  const auto page = [tabs](const QString &title, const QString &id) {
    auto *widget = new QWidget;
    auto *form = new QFormLayout(widget);
    auto *scroll = new QScrollArea;
    scroll->setObjectName(id);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(widget);
    tabs->addTab(scroll, title);
    return form;
  };
  const auto choice = [this, v, apply,
                       reset](QFormLayout *form, const QString &key,
                              const QString &title,
                              const QList<QPair<QString, QString>> &items) {
    auto *box = new QComboBox;
    box->setObjectName("video/" + key);
    for (const auto &item : items)
      box->addItem(item.second, item.first);
    box->setCurrentIndex(box->findData(v.value(key)));
    form->addRow(title, box);
    apply->append([this, box, key] {
      profile.settings.setValue("video/" + key, box->currentData());
    });
    reset->append([box] { box->setCurrentIndex(0); });
    return box;
  };
  const auto note = [](QFormLayout *form, const QString &text) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    form->addRow(label);
  };
  auto *scaling = page(uiTr("Видео — масштабирование"), "videoScalingPage");
  choice(scaling, "softwareScaler", uiTr("Программный вывод:"),
         {{"default", uiTr("По умолчанию libmpv")},
          {"point", uiTr("Ближайший пиксель")},
          {"bilinear", "Bilinear"},
          {"bicubic", "Bicubic"},
          {"lanczos", "Lanczos"},
          {"spline", "Spline"},
          {"area", "Area"}});
  const QList<QPair<QString, QString>> kernels{
      {"default", uiTr("По умолчанию libmpv")},
      {"nearest", uiTr("Ближайший пиксель")},
      {"bilinear", "Bilinear"},
      {"bicubic", "Bicubic"},
      {"lanczos", "Lanczos"},
      {"spline36", "Spline36"},
      {"ewa_lanczossharp", "EWA LanczosSharp"}};
  choice(scaling, "upscaler", uiTr("OpenGL: увеличение:"), kernels);
  choice(scaling, "downscaler", uiTr("OpenGL: уменьшение:"), kernels);
  choice(scaling, "chromaScaler", uiTr("OpenGL: цветовые компоненты:"),
         kernels);
  note(scaling,
       uiTr("Программный фильтр использует libswscale; при явном выборе "
            "zimg отключается. «По умолчанию» восстанавливает исходные "
            "настройки libmpv. "
            "Три OpenGL-фильтра действуют только в GPU-выводе. При программном "
            "резервном "
            "выводе они сохраняются, но не заменяют программный фильтр. "
            "Параметры "
            "меняют масштабирование кадра, а не геометрию Pan&Scan."));
  auto *decoding = page(uiTr("Видео — декодирование"), "videoDecodingPage");
  const int colorResetStart = reset->size();
  auto *color = page(uiTr("Видео — цвет и точность"), "videoColorPage");
  choice(color, "softwarePrecision", uiTr("Software: точность кадра:"),
         {{"8", uiTr("8 бит на компонент (по умолчанию)")},
          {"16", uiTr("16 бит на компонент (RGBX64)")}});
  note(
      color,
      uiTr("16 бит сохраняет больше уровней при преобразовании кадра и требует "
           "вдвое больше памяти. Экран и композитор могут сократить точность. "
           "Это не включает 10-битный режим монитора. Параметры оттенка и гаммы "
           "действуют в обоих выводах и в превью."));
  choice(color, "outputLevels", uiTr("OpenGL: диапазон RGB:"),
         {{"default", uiTr("По умолчанию libmpv")},
          {"full", uiTr("Полный (0–255)")},
          {"limited", uiTr("Ограниченный (16–235)")}});
  choice(color, "fboFormat", uiTr("OpenGL: промежуточные текстуры:"),
         {{"default", uiTr("По умолчанию libmpv")},
          {"rgba8", "RGBA 8"},
          {"rgb10_a2", "RGB 10 / A 2"},
          {"rgba16", "RGBA 16"},
          {"rgba16f", "RGBA Float16"},
          {"rgba32f", "RGBA Float32"}});
  choice(color, "ditherDepth", uiTr("OpenGL: dithering до разрядности:"),
         {{"default", uiTr("По умолчанию libmpv")},
          {"no", uiTr("Выключен")},
          {"8", "8"},
          {"10", "10"},
          {"12", "12"},
          {"16", "16"}});
  choice(color, "targetPrim", uiTr("OpenGL: цветовой охват дисплея:"),
         {{"default", uiTr("По умолчанию libmpv")},
          {"bt.709", "BT.709 / sRGB"},
          {"bt.2020", "BT.2020"},
          {"dci-p3", "DCI-P3"},
          {"adobe", "Adobe RGB"}});
  choice(color, "targetTrc", uiTr("OpenGL: передаточная функция:"),
         {{"default", uiTr("По умолчанию libmpv")},
          {"srgb", "sRGB"},
          {"bt.1886", "BT.1886"},
          {"gamma2.2", "Gamma 2.2"},
          {"gamma2.4", "Gamma 2.4"}});
  choice(color, "toneMapping", uiTr("OpenGL: преобразование HDR в SDR:"),
         {{"default", uiTr("По умолчанию libmpv")},
          {"clip", "Clip"},
          {"mobius", "Mobius"},
          {"reinhard", "Reinhard"},
          {"hable", "Hable"},
          {"bt.2390", "BT.2390"}});
  note(color,
       uiTr("Параметры OpenGL не действуют в Software. Выбирайте диапазон и "
            "охват согласно дисплею; неверные значения меняют цвета и уровни "
            "чёрного. Формат FBO задаёт промежуточную обработку: конечная "
            "поверхность Qt остаётся 8-битной. HDR passthrough и "
            "автоматический ICC-профиль здесь не включаются. Принятие "
            "параметра libmpv не подтверждает поддержку GPU."));
  auto *colorStatus = new QLabel;
  colorStatus->setObjectName("videoColorStatus");
  colorStatus->setWordWrap(true);
  const auto updateColor = [this, colorStatus] {
    colorStatus->setText(
        uiTr("Фактический вывод: %1. Software: %2 бит/компонент по настройке. "
             "Конечная поверхность OpenGL: RGBA8.")
            .arg(player->outputName(), VideoOutput::values(profile.settings)
                                           .value("softwarePrecision")
                                           .toString()));
  };
  updateColor();
  auto *colorTimer = new QTimer(colorStatus);
  colorTimer->setInterval(500);
  connect(colorTimer, &QTimer::timeout, colorStatus, updateColor);
  colorTimer->start();
  color->addRow(colorStatus);
  auto *colorReset = new QPushButton(uiTr("Сбросить цвет и точность"));
  colorReset->setObjectName("videoColorReset");
  color->addRow(colorReset);
  const auto colorResets = reset->mid(colorResetStart);
  reset->erase(reset->begin() + colorResetStart, reset->end());
  connect(colorReset, &QPushButton::clicked, tabs, [colorResets] {
    for (const auto &fn : colorResets)
      fn();
  });

  auto *hardware =
      new QCheckBox(uiTr("Аппаратное декодирование (при доступности)"));
  hardware->setObjectName("playback/hardwareDecoding");
  hardware->setChecked(
      profile.settings.value("playback/hardwareDecoding", true).toBool());
  for (auto *other :
       tabs->window()->findChildren<QCheckBox *>("playback/hardwareDecoding")) {
    if (other == hardware)
      continue;
    connect(hardware, &QCheckBox::toggled, other, &QCheckBox::setChecked);
    connect(other, &QCheckBox::toggled, hardware, &QCheckBox::setChecked);
  }
  decoding->addRow(hardware);
  apply->append([this, hardware] {
    profile.settings.setValue("playback/hardwareDecoding",
                              hardware->isChecked());
  });
  reset->append([hardware] { hardware->setChecked(true); });
  choice(decoding, "hardwareBackend", uiTr("Метод (с копированием кадра):"),
         {{"auto-copy", uiTr("Автоматически — auto-copy")},
          {"vaapi-copy", "VA-API copy"},
          {"nvdec-copy", "NVIDIA NVDEC copy"},
          {"vdpau-copy", "VDPAU copy"}});
  auto *native = new QCheckBox(uiTr("Разрешённые кодеки из списка libmpv"));
  native->setObjectName("video/hardwareCodecsDefault");
  native->setChecked(v.value("hardwareCodecsDefault").toBool());
  decoding->addRow(native);
  auto *codecs = new QWidget;
  auto *row = new QGridLayout(codecs);
  QList<QCheckBox *> boxes;
  const QStringList names{"h264", "hevc", "vp8",        "vp9",
                          "av1",  "vc1",  "mpeg2video", "prores"};
  for (int i = 0; i < names.size(); ++i) {
    auto *box = new QCheckBox(names[i].toUpper());
    box->setObjectName("video/hwcodec/" + names[i]);
    box->setProperty("codec", names[i]);
    box->setChecked(
        v.value("hardwareCodecs").toStringList().contains(names[i]));
    box->setEnabled(!native->isChecked());
    connect(native, &QCheckBox::toggled, box,
            [box](bool checked) { box->setEnabled(!checked); });
    row->addWidget(box, i / 4, i % 4);
    boxes << box;
  }
  decoding->addRow(codecs);
  apply->append([this, native, boxes] {
    profile.settings.setValue("video/hardwareCodecsDefault",
                              native->isChecked());
    QStringList selected;
    for (auto *box : boxes)
      if (box->isChecked())
        selected << box->property("codec").toString();
    profile.settings.setValue("video/hardwareCodecs", selected);
  });
  reset->append([native, boxes] {
    native->setChecked(true);
    for (auto *box : boxes)
      box->setChecked(VideoOutput::defaults()
                          .value("hardwareCodecs")
                          .toStringList()
                          .contains(box->property("codec").toString()));
  });
  const auto number = [this, v, apply, reset](
                          QFormLayout *form, const QString &key,
                          const QString &title, int low, int high, int def) {
    auto *spin = new QSpinBox;
    spin->setObjectName("video/" + key);
    spin->setRange(low, high);
    spin->setValue(v.value(key).toInt());
    form->addRow(title, spin);
    apply->append([this, key, spin] {
      profile.settings.setValue("video/" + key, spin->value());
    });
    reset->append([spin, def] { spin->setValue(def); });
    return spin;
  };
  auto *threads = number(decoding, "decoderThreads",
                         uiTr("Потоки программного декодера:"), 0, 16, 0);
  threads->setSpecialValueText(uiTr("Автоматически"));
  number(decoding, "hardwareFallbackFrames",
         uiTr("Переход на CPU после ошибок кадров:"), 1, 16, 3);
  auto *deinterlace = new QCheckBox(uiTr("Включить деинтерлейсинг libmpv"));
  deinterlace->setObjectName("video/deinterlace");
  deinterlace->setChecked(v.value("deinterlace").toBool());
  decoding->addRow(deinterlace);
  apply->append([this, deinterlace] {
    profile.settings.setValue("video/deinterlace", deinterlace->isChecked());
  });
  reset->append([deinterlace] { deinterlace->setChecked(false); });
  note(decoding,
       uiTr("Пустой свой список кодеков выключает аппаратное декодирование. "
            "Недоступный метод или неподдерживаемый поток возвращает "
            "декодирование на CPU. "
            "Copy-back совместим с программными фильтрами; это не прямой вывод "
            "GPU. "
            "Смена декодера может кратко прервать видео. Число потоков зависит "
            "от кодека. "
            "Деинтерлейсинг выключен по умолчанию: включайте для "
            "чересстрочного видео, "
            "на прогрессивном он может ухудшить изображение. Алгоритм выбирает "
            "libmpv."));
  auto *status = new QLabel;
  status->setObjectName("videoOutputStatus");
  status->setWordWrap(true);
  status->setTextFormat(Qt::PlainText);
  const auto update = [this, status] {
    status->setText(
        player->QObject::property("foxicmpVideoOutputStatus").toString() +
        "\n" +
        uiTr("Вывод: %1. Аппаратный декодер: %2.\nДекодер: %3. Формат: %4.")
            .arg(player->outputName(), player->get("hwdec-current").toString(),
                 player->get("video-codec").toString(),
                 player->get("video-params")
                     .toMap()
                     .value("pixelformat")
                     .toString()));
  };
  update();
  auto *timer = new QTimer(status);
  timer->setInterval(500);
  connect(timer, &QTimer::timeout, status, update);
  timer->start();
  decoding->addRow(status);
  auto *button =
      new QPushButton(uiTr("Сбросить масштабирование и декодирование"));
  button->setObjectName("videoOutputReset");
  decoding->addRow(button);
  connect(button, &QPushButton::clicked, tabs, [reset] {
    for (const auto &fn : *reset)
      fn();
  });
}

void Window::videoOutputDialog() {
  QDialog dialog(this);
  dialog.setObjectName("videoOutputDialog");
  dialog.setWindowTitle(uiTr("Видеовывод и декодирование"));
  dialog.resize(860, 680);
  auto *layout = new QVBoxLayout(&dialog);
  auto *tabs = new QTabWidget;
  layout->addWidget(tabs);
  QList<std::function<void()>> apply;
  attachVideoOutputPreferences(tabs, &apply);
  attachVideoDisplayPreferences(tabs, &apply);
  attachVideoShaderPreferences(tabs, &apply);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    if (validateVideoShaderPreferences(&dialog) &&
        validateVideoDisplayPreferences(&dialog))
      dialog.accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() == QDialog::Accepted) {
    for (const auto &fn : apply)
      fn();
    applyMediaOptions();
  }
}
