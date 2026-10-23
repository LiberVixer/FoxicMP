#include "i18n.h"
#include "subtitlerender.h"
#include "window.h"
#include <QtWidgets>

void Window::attachSubtitleRenderPreferences(
    QTabWidget *tabs, QList<std::function<void()>> *apply) {
  auto collect = std::make_shared<QList<std::function<void()>>>();
  auto reset = std::make_shared<QList<std::function<void()>>>();
  const auto current = SubtitleRender::values(profile.settings);
  const auto defaults = SubtitleRender::defaults();
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
  const auto note = [](QFormLayout *form, const QString &text) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    form->addRow(label);
  };
  const auto check = [this, current, defaults, collect,
                      reset](QFormLayout *form, const QString &id,
                             const QString &text) {
    auto *box = new QCheckBox(text);
    const auto key = "subtitles/render/" + id;
    box->setObjectName(key);
    box->setChecked(current.value(id).toBool());
    form->addRow(box);
    collect->append(
        [this, key, box] { profile.settings.setValue(key, box->isChecked()); });
    reset->append(
        [box, defaults, id] { box->setChecked(defaults.value(id).toBool()); });
  };
  const auto choice = [this, current, defaults, collect,
                       reset](QFormLayout *form, const QString &id,
                              const QString &title,
                              const QList<QPair<QString, QString>> &items) {
    auto *box = new QComboBox;
    const auto key = "subtitles/render/" + id;
    box->setObjectName(key);
    for (const auto &item : items)
      box->addItem(item.second, item.first);
    box->setCurrentIndex(box->findData(current.value(id)));
    form->addRow(title, box);
    collect->append([this, key, box] {
      profile.settings.setValue(key, box->currentData());
    });
    reset->append([box, defaults, id] {
      box->setCurrentIndex(box->findData(defaults.value(id)));
    });
  };
  auto *render = page(uiTr("Субтитры — рендер"));
  note(
      render,
      uiTr(
          "Текст рисует libass через libmpv. Кэш шрифтов и готовых изображений "
          "управляется автоматически; отдельного размера очереди кадров здесь "
          "нет."));
  choice(render, "hinting", uiTr("Хинтинг шрифтов:"),
         {{"none", uiTr("Без хинтинга (по умолчанию)")},
          {"light", uiTr("Лёгкий")},
          {"normal", uiTr("Обычный")},
          {"native", uiTr("Встроенный в шрифт")}});
  choice(render, "shaper", uiTr("Формирование текста:"),
         {{"complex", uiTr("Полное — HarfBuzz (по умолчанию)")},
          {"simple", uiTr("Простое — FriBidi")}});
  check(render, "embeddedFonts",
        uiTr("Использовать шрифты, вложенные в MKV/ASS"));
  check(render, "scaleByWindow",
        uiTr("Менять размер текста при изменении размера окна"));
  check(render, "scaleWithWindow",
        uiTr("Размер обычного текста относительно окна, включая чёрные поля"));
  check(render, "assScaleWithWindow",
        uiTr("Размер ASS относительно окна, включая чёрные поля"));
  check(render, "clearOnSeek",
        uiTr("Очищать кэш текстовых реплик при перемотке (проблемные MKV)"));
  note(
      render,
      uiTr(
          "Хинтинг и масштабирование могут смещать авторские надписи ASS. "
          "Полное формирование текста лучше поддерживает сложные письменности. "
          "Очистка при перемотке нужна для MKV с повторяющимся ReadOrder; "
          "обычно оставляйте её выключенной. Она не отключает кэш шрифтов и "
          "видео."));
  auto *restore = new QPushButton(uiTr("Сбросить рендер и совместимость"));
  restore->setObjectName("subtitleRenderReset");
  render->addRow(restore);
  connect(restore, &QPushButton::clicked, tabs, [reset] {
    for (const auto &fn : *reset)
      fn();
  });
  auto *compat = page(uiTr("Субтитры — совместимость"));
  check(compat, "aspectCompat", uiTr("ASS: пропорции как в VSFilter"));
  check(compat, "blurCompat", uiTr("ASS: размытие как в VSFilter"));
  choice(compat, "colorCompat", uiTr("ASS: цвета VSFilter:"),
         {{"basic", uiTr("Базовая совместимость (по умолчанию)")},
          {"no", uiTr("Без преобразования")},
          {"full", uiTr("Полная совместимость")},
          {"force-601", uiTr("Принудительно BT.601")}});
  check(compat, "fixTiming",
        uiTr("Устранять разрывы и перекрытия текста короче 210 мс"));
  check(compat, "stretchDurations",
        uiTr("Продлевать реплику SRT до следующей"));
  check(compat, "pastVideoEnd",
        uiTr("Обновлять субтитры после конца видео, пока идёт звук"));
  note(compat,
       uiTr("Для устранения коротких разрывов включите замену стиля ASS. "
            "Продление не применяется к авторскому ASS. "
            "Коррекция времени меняет только показ, исходный файл сохраняется. "
            "Следующие параметры относятся к графическим субтитрам DVD/PGS, не "
            "к тексту."));
  check(compat, "bitmapGray", uiTr("Графические субтитры в оттенках серого"));
  check(compat, "stretchDvd",
        uiTr("Растягивать DVD-субтитры к разрешению кадра"));
  check(compat, "stretchImage",
        uiTr("Растягивать графические субтитры на весь экран"));
  check(compat, "imageVideoResolution",
        uiTr("Использовать разрешение видео для графических субтитров"));
  auto *blur = new QDoubleSpinBox;
  blur->setObjectName("subtitles/render/bitmapBlur");
  blur->setRange(0, 3);
  blur->setDecimals(2);
  blur->setSingleStep(.1);
  blur->setValue(current.value("bitmapBlur").toDouble());
  compat->addRow(uiTr("Размытие графических субтитров (0–3):"), blur);
  collect->append([this, blur] {
    profile.settings.setValue("subtitles/render/bitmapBlur", blur->value());
  });
  reset->append([blur] { blur->setValue(0); });
  apply->append([collect] {
    for (const auto &fn : *collect)
      fn();
  });
}
