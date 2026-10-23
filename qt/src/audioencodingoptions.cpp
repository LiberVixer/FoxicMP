#include "audioencoding.h"
#include "i18n.h"
#include "player.h"
#include "window.h"
#include <QtWidgets>

void Window::attachAudioEncodingPreferences(
    QTabWidget *tabs, QList<std::function<void()>> *apply) {
  const auto values = AudioEncoding::values(profile.settings);
  auto *content = new QWidget;
  auto *form = new QFormLayout(content);
  auto *scroll = new QScrollArea;
  scroll->setObjectName("audioEncodingPage");
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(content);
  tabs->addTab(scroll, uiTr("Звук — формат и декодер"));
  const auto choice = [form](const QString &id, const QString &title) {
    auto *box = new QComboBox;
    box->setObjectName("audio/" + id);
    form->addRow(title, box);
    return box;
  };
  auto *format = choice("pcmFormat", uiTr("Формат PCM:"));
  for (const auto &entry :
       QList<QPair<QString, QString>>{{"auto", uiTr("Автоматически")},
                                      {"s16", "Int16"},
                                      {"s24", uiTr("Int24 — 24 бита в Int32")},
                                      {"s32", "Int32"},
                                      {"float", "Float32"}})
    format->addItem(entry.second, entry.first);
  format->setCurrentIndex(format->findData(values.value("pcmFormat")));
  auto *rate = choice("sampleRate", uiTr("Частота PCM:"));
  rate->addItem(uiTr("Автоматически"), 0);
  for (int n : {32000, 44100, 48000, 88200, 96000, 176400, 192000})
    rate->addItem(QString::number(n) + uiTr(" Гц"), n);
  rate->setCurrentIndex(rate->findData(values.value("sampleRate")));
  auto *pcmNote = new QLabel(uiTr(
      "PCM преобразуется после обработки. Int24 использует усечение до 24 бит "
      "в Int32 без dithering; обычная громкость плеера и звуковой сервер могут "
      "изменить младшие биты. Это не выбор физической разрядности ЦАП. "
      "Автоматический режим убирает принудительное преобразование."));
  pcmNote->setWordWrap(true);
  form->addRow(pcmNote);
  auto *threads = new QSpinBox;
  threads->setObjectName("audio/decoderThreads");
  threads->setRange(0, 16);
  threads->setSpecialValueText(uiTr("Авто (до 16)"));
  threads->setValue(values.value("decoderThreads").toInt());
  form->addRow(uiTr("Потоки декодера:"), threads);
  auto *drc = new QDoubleSpinBox;
  drc->setObjectName("audio/ac3Drc");
  drc->setRange(0, 1);
  drc->setDecimals(2);
  drc->setSingleStep(.1);
  drc->setValue(values.value("ac3Drc").toDouble());
  form->addRow(uiTr("DRC AC-3 (0 — выкл., 1 — полностью):"), drc);
  auto *downmix = new QCheckBox(uiTr("Разрешить сведение каналов декодером"));
  downmix->setObjectName("audio/decoderDownmix");
  downmix->setChecked(values.value("decoderDownmix").toBool());
  form->addRow(downmix);
  auto *preferred = choice("preferredDecoder", uiTr("Предпочитать декодер:"));
  preferred->addItem(uiTr("Автоматически, с резервными декодерами"), "");
  const QStringList known{"aac",      "aac_fixed", "ac3",    "ac3_fixed",
                          "eac3",     "dca",       "truehd", "flac",
                          "mp3float", "mp3",       "opus",   "vorbis"};
  QSet<QString> added;
  for (const auto &entry : player->get("decoder-list").toList()) {
    const auto item = entry.toMap();
    const auto driver = item.value("driver").toString();
    if (known.contains(driver) && !added.contains(driver)) {
      preferred->addItem(driver + " — " + item.value("description").toString(),
                         driver);
      added.insert(driver);
    }
  }
  const auto saved = values.value("preferredDecoder").toString();
  if (preferred->findData(saved) < 0)
    preferred->addItem(saved + uiTr(" (сейчас недоступно)"), saved);
  preferred->setCurrentIndex(preferred->findData(saved));
  auto *bitstream =
      new QCheckBox(uiTr("Передавать сжатый звук (bitstream / passthrough)"));
  bitstream->setObjectName("audio/passthrough");
  bitstream->setChecked(values.value("passthrough").toBool());
  form->addRow(bitstream);
  auto *codecs = new QWidget;
  auto *row = new QHBoxLayout(codecs);
  row->setContentsMargins(0, 0, 0, 0);
  QList<QCheckBox *> boxes;
  for (const auto &codec :
       QStringList{"ac3", "dts", "dts-hd", "eac3", "truehd"}) {
    auto *box = new QCheckBox(codec.toUpper());
    box->setObjectName("audio/spdif/" + codec);
    box->setProperty("codec", codec);
    box->setChecked(values.value("spdifCodecs").toStringList().contains(codec));
    row->addWidget(box);
    boxes << box;
  }
  form->addRow(codecs);
  auto *note = new QLabel(uiTr(
      "Bitstream требует совместимого HDMI/S/PDIF-выхода, драйвера и ресивера. "
      "При обработке, ReplayGain, заданной раскладке, двух выходах, скорости "
      "не 1×, DRC или сведении декодером используется PCM. Настройки bitstream "
      "сохраняются и возвращаются после снятия конфликта. Пока bitstream "
      "разрешён, "
      "принудительный формат/частота PCM не применяются; неподдерживаемые "
      "потоки "
      "декодирует libmpv. Программная громкость при bitstream не регулирует "
      "уровень "
      "ресивера. Изменение параметров перезапускает аудиодорожку и может "
      "кратко "
      "прервать звук. DRC действует только при наличии метаданных в AC-3. "
      "Приоритет декодера сохраняет автоматический резерв; потоки "
      "поддерживаются "
      "не каждым кодеком. Микшер отключает сведение в декодере."));
  note->setWordWrap(true);
  form->addRow(note);
  auto *info = new QLabel;
  info->setObjectName("audioEncodingStatus");
  info->setWordWrap(true);
  info->setTextFormat(Qt::PlainText);
  const auto update = [this, info] {
    const auto out = player->get("audio-out-params").toMap();
    info->setText(
        player->QObject::property("foxicmpAudioEncodingStatus").toString() +
        "\n" +
        uiTr("Фактический вывод libmpv: %1; %2 Гц; %3.\nДекодер: %4")
            .arg(out.value("format", "—").toString(),
                 out.value("samplerate", "—").toString(),
                 out.value("channels", "—").toString(),
                 player->get("audio-codec-name").toString()));
  };
  update();
  auto *timer = new QTimer(info);
  timer->setInterval(500);
  connect(timer, &QTimer::timeout, info, update);
  timer->start();
  form->addRow(info);
  auto *reset = new QPushButton(uiTr("Сбросить формат и декодер"));
  reset->setObjectName("audioEncodingReset");
  form->addRow(reset);
  connect(reset, &QPushButton::clicked, content,
          [format, rate, threads, drc, downmix, preferred, bitstream, boxes] {
            format->setCurrentIndex(0);
            rate->setCurrentIndex(0);
            threads->setValue(1);
            drc->setValue(0);
            downmix->setChecked(false);
            preferred->setCurrentIndex(0);
            bitstream->setChecked(false);
            for (auto *box : boxes)
              box->setChecked(box->property("codec") == "ac3" ||
                              box->property("codec") == "dts");
          });
  apply->append(
      [this, format, rate, threads, drc, downmix, preferred, bitstream, boxes] {
        profile.settings.setValue("audio/pcmFormat", format->currentData());
        profile.settings.setValue("audio/sampleRate", rate->currentData());
        profile.settings.setValue("audio/decoderThreads", threads->value());
        profile.settings.setValue("audio/ac3Drc", drc->value());
        profile.settings.setValue("audio/decoderDownmix", downmix->isChecked());
        profile.settings.setValue("audio/preferredDecoder",
                                  preferred->currentData());
        profile.settings.setValue("audio/passthrough", bitstream->isChecked());
        QStringList selected;
        for (auto *box : boxes)
          if (box->isChecked())
            selected << box->property("codec").toString();
        profile.settings.setValue("audio/spdifCodecs", selected);
      });
}
