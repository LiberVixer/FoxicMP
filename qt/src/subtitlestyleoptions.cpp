#include "devicesource.h"
#include "i18n.h"
#include "player.h"
#include "subtitledocument.h"
#include "subtitlestyle.h"
#include "window.h"
#include <QtWidgets>
#include <cmath>

void Window::applySubtitleStyle() {
  const auto track = selectedSubtitleTrack();
  const auto path = track.value("external-filename").toString();
  const bool copy = track.value("external").toBool() &&
                    SubtitleStyle::isCopy(path, profile.directory);
  const auto stamp =
      QJsonDocument::fromVariant(SubtitleStyle::values(profile.settings))
          .toJson(QJsonDocument::Compact);
  if (copy && (subtitleStylePath != path || subtitleStyleStamp != stamp)) {
    // Store the stamp before reloading: track-list changes re-enter this
    // method.
    subtitleStylePath = path;
    subtitleStyleStamp = stamp;
    SubtitleDocument document;
    QString error;
    if (document.load(path, "UTF-8", &error)) {
      const auto text =
          SubtitleStyle::assCopy(document, profile.settings, &error).toUtf8();
      QSaveFile file(path);
      if (!text.isEmpty() && file.open(QIODevice::WriteOnly) &&
          file.write(text) == text.size() && file.commit())
        player->command({"sub-reload", track.value("id").toString()});
      else if (error.isEmpty())
        error = file.errorString();
    }
    if (!error.isEmpty())
      showOsd(error);
  } else if (!copy) {
    subtitleStylePath.clear();
    subtitleStyleStamp.clear();
  }
  SubtitleStyle::apply(player, profile.settings, copy);
  if (player->QObject::property("foxicmpSubtitleRefresh").toBool()) {
    player->setProperty("foxicmpSubtitleRefresh", false);
    if (!stopped && !progressive && !DeviceSource::isLive(current)) {
      const auto now = player->get("time-pos");
      const auto generation = playbackGeneration;
      if (now.isValid())
        QTimer::singleShot(100, this, [this, generation] {
          if (playbackGeneration != generation || stopped || progressive ||
              DeviceSource::isLive(current))
            return;
          const auto seconds = player->get("time-pos").toDouble();
          if (seekAvailable(seconds, seconds))
            seekTo(seconds);
        });
    }
  }
}
void Window::attachSubtitleStylePreferences(
    QTabWidget *tabs, QList<std::function<void()>> *apply) {
  auto collect = std::make_shared<QList<std::function<void()>>>();
  auto reset = std::make_shared<QList<std::function<void()>>>();
  const auto page = [tabs](const QString &name) {
    auto *widget = new QWidget;
    auto *form = new QFormLayout(widget);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(widget);
    tabs->addTab(scroll, name);
    return form;
  };
  const auto number = [this, collect,
                       reset](QFormLayout *form, const QString &label,
                              const QString &id, double fallback, double low,
                              double high, double step) {
    auto *box = new QDoubleSpinBox;
    const auto key = "subtitles/" + id;
    box->setObjectName(key);
    box->setDecimals(2);
    box->setRange(low, high);
    box->setSingleStep(step);
    const auto raw = profile.settings.value(key, fallback).toDouble();
    box->setValue(std::isfinite(raw) ? raw : fallback);
    form->addRow(label, box);
    collect->append(
        [this, key, box] { profile.settings.setValue(key, box->value()); });
    reset->append([box, fallback] { box->setValue(fallback); });
    return box;
  };
  const auto choice = [this, collect,
                       reset](QFormLayout *form, const QString &label,
                              const QString &id,
                              const QList<QPair<QString, QString>> &items,
                              const QString &fallback) {
    auto *box = new QComboBox;
    const auto key = "subtitles/" + id;
    box->setObjectName(key);
    for (const auto &item : items)
      box->addItem(item.second, item.first);
    box->setCurrentIndex(
        std::max(0, box->findData(profile.settings.value(key, fallback))));
    form->addRow(label, box);
    collect->append([this, key, box] {
      profile.settings.setValue(key, box->currentData());
    });
    reset->append(
        [box, fallback] { box->setCurrentIndex(box->findData(fallback)); });
  };
  const auto check = [this, collect, reset](QFormLayout *form,
                                            const QString &title,
                                            const QString &id, bool fallback) {
    auto *box = new QCheckBox(title);
    const auto key = "subtitles/" + id;
    box->setObjectName(key);
    box->setChecked(profile.settings.value(key, fallback).toBool());
    form->addRow(box);
    collect->append(
        [this, key, box] { profile.settings.setValue(key, box->isChecked()); });
    reset->append([box, fallback] { box->setChecked(fallback); });
    return box;
  };
  const auto color = [this, tabs, collect,
                      reset](QFormLayout *form, const QString &title,
                             const QString &id, const QString &fallback) {
    auto *button = new QPushButton;
    const auto key = "subtitles/" + id;
    button->setObjectName(key);
    const auto set = [button](QColor c) {
      button->setProperty("color", c);
      button->setText(c.name(QColor::HexArgb));
    };
    QColor c(profile.settings.value(key, fallback).toString());
    set(c.isValid() ? c : QColor(fallback));
    form->addRow(title, button);
    connect(button, &QPushButton::clicked, tabs, [button, set, tabs] {
      const auto c = QColorDialog::getColor(
          button->property("color").value<QColor>(), tabs,
          uiTr("Цвет субтитров"), QColorDialog::ShowAlphaChannel);
      if (c.isValid())
        set(c);
    });
    collect->append([this, key, button] {
      profile.settings.setValue(
          key, button->property("color").value<QColor>().name(QColor::HexArgb));
    });
    reset->append([set, fallback] { set(QColor(fallback)); });
  };
  auto *layout = page(uiTr("Субтитры — размещение"));
  check(layout, uiTr("Включить дополнительное оформление"), "styleEnabled",
        false);
  auto *note = new QLabel(
      uiTr("SRT: выравнивание, отступы, интервалы, тень и прозрачность "
           "работают напрямую. "
           "Масштабы X/Y, угол, точная позиция X и вторичный цвет требуют ASS. "
           "В редакторе включите «Применить как ASS-копию». "
           "Авторский ASS меняется только при включённом «Применять мой стиль "
           "также к ASS/SSA». Его встроенные теги могут иметь приоритет."));
  note->setWordWrap(true);
  layout->addRow(note);
  choice(layout, uiTr("Выравнивание по горизонтали:"), "alignX",
         {{"left", uiTr("Слева")},
          {"center", uiTr("По центру")},
          {"right", uiTr("Справа")}},
         "center");
  choice(layout, uiTr("Выравнивание по вертикали:"), "alignY",
         {{"top", uiTr("Сверху")},
          {"center", uiTr("По центру")},
          {"bottom", uiTr("Снизу")}},
         "bottom");
  choice(layout, uiTr("Выравнивание строк:"), "justify",
         {{"auto", uiTr("Автоматически")},
          {"left", uiTr("Слева")},
          {"center", uiTr("По центру")},
          {"right", uiTr("Справа")}},
         "auto");
  number(layout, uiTr("Боковые отступы, усл. пиксели:"), "marginX", 25, 0, 500,
         1);
  number(layout, uiTr("Вертикальный отступ, усл. пиксели:"), "marginY", 22, 0,
         500, 1);
  number(layout, uiTr("Межбуквенный интервал:"), "spacing", 0, -10, 30, .5);
  number(layout, uiTr("Межстрочный интервал:"), "lineSpacing", 0, -20, 80, 1);
  check(layout, uiTr("Разрешить размещение на чёрных полях"), "useMargins",
        true);
  auto *geometry = page(uiTr("Субтитры — геометрия ASS"));
  number(geometry, uiTr("Ширина текста, %:"), "scaleX", 100, 20, 300, 5);
  number(geometry, uiTr("Высота текста, %:"), "scaleY", 100, 20, 300, 5);
  number(geometry, uiTr("Угол поворота, °:"), "angle", 0, -180, 180, 1);
  number(geometry, uiTr("Позиция X в ASS-копии, %:"), "horizontalPosition", 50,
         0, 100, 1);
  auto *geometryNote = new QLabel(
      uiTr("50% — без смещения; 0/100% сдвигают якорь на половину кадра "
           "влево/вправо. "
           "ASS-копия использует холст 1280×720, сохраняет текст и время, "
           "заменяя исходное оформление. "
           "Параметры копии обновляются после OK и при её повторном открытии. "
           "В авторском ASS единицы размеров зависят от его холста; "
           "точная позиция X применяется только к созданной копии. Графические "
           "субтитры этим стилем не меняются."));
  geometryNote->setWordWrap(true);
  geometry->addRow(geometryNote);
  check(geometry, uiTr("Полужирный текст"), "bold", false);
  check(geometry, uiTr("Курсив"), "italic", false);
  auto *colors = page(uiTr("Субтитры — цвета и тень"));
  number(colors, uiTr("Непрозрачность текста, %:"), "textOpacity", 100, 0, 100,
         5);
  number(colors, uiTr("Непрозрачность обводки, %:"), "outlineOpacity", 100, 0,
         100, 5);
  color(colors, uiTr("Вторичный цвет ASS (караоке):"), "secondaryColor",
        "#ffffff00");
  number(colors, uiTr("Непрозрачность вторичного цвета, %:"),
         "secondaryOpacity", 100, 0, 100, 5);
  color(colors, uiTr("Цвет тени:"), "shadowColor", "#ff000000");
  number(colors, uiTr("Непрозрачность тени, %:"), "shadowOpacity", 100, 0, 100,
         5);
  number(colors, uiTr("Смещение тени:"), "shadowOffset", 0, 0, 20, .5);
  color(colors, uiTr("Цвет фона (альфа 0 — без фона):"), "backColor",
        "#00000000");
  number(colors, uiTr("Непрозрачность фона, %:"), "backOpacity", 100, 0, 100,
         5);
  number(colors, uiTr("Размытие краёв:"), "blur", 0, 0, 20, .25);
  auto *colorNote = new QLabel(uiTr(
      "Проценты непрозрачности умножаются на альфа-канал выбранного цвета. "
      "Цвет текста и обводки задаются в основных параметрах оформления. Фон "
      "заменяет тень. "
      "Вторичный цвет виден у ASS с тегами караоке, обычный текст использует "
      "основной цвет."));
  colorNote->setWordWrap(true);
  colors->addRow(colorNote);
  auto *restore = new QPushButton(uiTr("Сбросить дополнительное оформление"));
  restore->setObjectName("subtitleStyleReset");
  layout->addRow(restore);
  connect(restore, &QPushButton::clicked, tabs, [reset] {
    for (const auto &fn : *reset)
      fn();
  });
  attachSubtitleRenderPreferences(tabs, apply);
  apply->append([collect] {
    for (const auto &fn : *collect)
      fn();
  });
}
void Window::subtitleStyleDialog() {
  QDialog dialog(this);
  dialog.setObjectName("subtitleStyleDialog");
  dialog.setWindowTitle(uiTr("Оформление и рендер субтитров"));
  dialog.resize(840, 650);
  auto *layout = new QVBoxLayout(&dialog);
  auto *tabs = new QTabWidget;
  layout->addWidget(tabs);
  QList<std::function<void()>> apply;
  attachSubtitleStylePreferences(tabs, &apply);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() == QDialog::Accepted) {
    for (const auto &fn : apply)
      fn();
    applyMediaOptions();
  }
}
