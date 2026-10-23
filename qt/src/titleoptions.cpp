#include "window.h"
#include "windowtitle.h"
#include "i18n.h"
#include "player.h"
#include "timeline.h"
#include <QtWidgets>
#include <QRegularExpression>
#include <cmath>
#include "classicstatus.h"
#include "progressivesource.h"

QString Window::mediaStatusTitle() const {
  QString title = progressive && !property("foxicmpProgressiveTitle").toString().isEmpty()
      ? property("foxicmpProgressiveTitle").toString() : titleMetadata.trimmed();
  const QString source = stdinPlayback ? QString("stdin:") : progressive ? player->get("path").toString() : current;
  const QString file = WindowTitle::name(source);
  // libmpv synthesizes media-title from the filename when no title tag exists.
  if (!title.isEmpty() && title != file)
    return title;
  const QFileInfo info(file);
  const QString base = info.completeBaseName();
  return base.isEmpty() ? file : base;
}

QString Window::chapterStatus() const {
  if (timeline->chapters.isEmpty())
    return {};
  int index = -1;
  for (int i = 0; i < timeline->chapters.size(); ++i) {
    if (timeline->chapters[i].toMap().value("time").toDouble() <= pos)
      index = i;
  }
  if (index < 0)
    return {};
  const auto chapter = timeline->chapters[index].toMap();
  QString label = chapter.value("title").toString().trimmed();
  label.replace(QRegularExpression("[\\r\\n\\t]+"), " ");
  const QString number = QString::number(index + 1);
  label.remove(QRegularExpression("^0?" + number + "(?:\\.| -)\\s*"));
  if (QRegularExpression("^(?:(?:chapter|глава) )?0?" + number + "$",
                         QRegularExpression::CaseInsensitiveOption).match(label).hasMatch())
    label.clear();
  if (!label.isEmpty())
    return QString("(%1. %2)").arg(index + 1).arg(label);
  return timeline->chapters.size() > 1 ? QString("(%1)").arg(index + 1) : QString();
}

void Window::updatePlayerStatus() {
  if (!state || stopped)
    return;
  QString message;
  if (!hoverStatus.isEmpty())
    message = hoverStatus;
  else if (!lastError.isEmpty())
    message = uiTr("Ошибка: ") + lastError;
  else if ((progressive && progressiveBuffering) ||
           player->get("paused-for-cache").toBool())
    message = progressive && !progressive->status().connected
        ? uiTr("Ожидание загрузчика…") : uiTr("Буферизация…");
  else {
    const double speed = player->get("speed").toDouble();
    message = paused ? QString::fromUtf8("⏸") : QString::fromUtf8("▶");
    if (!paused && std::abs(speed - 1.) > .0001)
      message += QString(" (%1x)").arg(QString::number(speed, 'g', 3));
    const QString title = mediaStatusTitle();
    const QString chapter = chapterStatus();
    if (!title.isEmpty())
      message += "  " + title + (chapter.isEmpty() ? QString() : " " + chapter);
    else if (!chapter.isEmpty())
      message += "  " + chapter;
  }
  state->setToolTip(message);
  state->setText(message);
  QString decoder = player->get("hwdec-current").toString();
  if (decoder == "no" || decoder.isEmpty() || player->get("video-params").toMap().isEmpty())
    decoder.clear();
  gpuIndicator->setProperty("decoder", decoder);
  gpuIndicator->setToolTip(decoder);
  static_cast<ClassicStatusArea *>(statusArea)->arrange();
}

void Window::updateWindowTitle() {
  const auto source = stdinPlayback ? QString("stdin:") : progressive ? player->get("path").toString() : current;
  const WindowTitle::Media media{
      source, stdinPlayback ? uiTr("Поток stdin")
          : progressive && !property("foxicmpProgressiveTitle").toString().isEmpty()
              ? property("foxicmpProgressiveTitle").toString() : titleMetadata,
      !lastError.isEmpty() ? uiTr("Ошибка")
      : (progressive && progressiveBuffering) ||
            (player && player->get("paused-for-cache").toBool())
          ? uiTr("Буферизация…")
      : paused ? uiTr("Пауза") : uiTr("Воспроизведение"),
      clockText(pos), duration > 0 ? clockText(duration) : QString("—"), !stopped};
  const auto title = WindowTitle::render(profile.settings, media);
  if (title != windowTitle())
    setWindowTitle(title);
}
void Window::attachTitlePreferences(QTabWidget *tabs,
                                    QList<std::function<void()>> *apply) {
  auto *page = new QWidget;
  page->setObjectName("titlePreferences");
  auto *form = new QFormLayout(page);
  tabs->addTab(page, uiTr("Заголовок окна"));
  auto *mode = new QComboBox;
  mode->setObjectName("ui/titleMode");
  mode->addItem(uiTr("Название из метаданных"), "title");
  mode->addItem(uiTr("Имя файла"), "name");
  mode->addItem(uiTr("Полный путь"), "path");
  mode->addItem(uiTr("Только название плеера"), "app");
  mode->addItem(uiTr("Собственный шаблон"), "custom");
  mode->setCurrentIndex(std::max(0, mode->findData(
      profile.settings.value("ui/titleMode", "title"))));
  form->addRow(uiTr("Содержимое заголовка:"), mode);
  auto *showState = new QCheckBox(uiTr("Показывать состояние воспроизведения"));
  showState->setObjectName("ui/titleState");
  showState->setChecked(profile.settings.value("ui/titleState", false).toBool());
  form->addRow(showState);
  auto *showPosition = new QCheckBox(uiTr("Показывать позицию и длительность"));
  showPosition->setObjectName("ui/titlePosition");
  showPosition->setChecked(profile.settings.value("ui/titlePosition", false).toBool());
  form->addRow(showPosition);
  auto *pattern = new QLineEdit(profile.settings.value("ui/titleTemplate", "{title} - {app}").toString());
  pattern->setObjectName("ui/titleTemplate");
  pattern->setMaxLength(2048);
  form->addRow(uiTr("Шаблон:"), pattern);
  auto *help = new QLabel(uiTr(
      "{name} — имя файла; {path} — полный путь; {title} — название ролика; "
      "{state} — состояние; {position} — позиция; {duration} — длительность; "
      "{app} — FoxicMP Qt. {{ и }} выводят фигурные скобки. "
      "В шаблоне состояние и время задаются этими полями."));
  help->setWordWrap(true);
  help->setTextFormat(Qt::PlainText);
  form->addRow(help);
  auto *preview = new QLabel;
  preview->setObjectName("titlePreview");
  preview->setTextFormat(Qt::PlainText);
  preview->setWordWrap(true);
  preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
  form->addRow(uiTr("Предпросмотр:"), preview);
  auto *note = new QLabel(uiTr(
      "При отсутствии названия используется имя файла. Без открытого файла "
      "показывается название и версия плеера. В сетевом пути скрыты логин, "
      "пароль и параметры URL. Настройки применяются после нажатия OK."));
  note->setWordWrap(true);
  form->addRow(note);
  const auto source = stdinPlayback ? QString("stdin:") : progressive ? player->get("path").toString() : current;
  WindowTitle::Media sample{source, titleMetadata,
      paused ? uiTr("Пауза") : uiTr("Воспроизведение"),
      clockText(pos), duration > 0 ? clockText(duration) : QString("—"), true};
  if (stopped || source.isEmpty())
    sample = {QDir(profile.directory).filePath(uiTr("Пример видео.mkv")),
              uiTr("Пример названия ролика"), uiTr("Воспроизведение"),
              "01:23", "45:00", true};
  const auto refresh = [=] {
    const bool custom = mode->currentData() == "custom";
    pattern->setEnabled(custom);
    showState->setEnabled(!custom);
    showPosition->setEnabled(!custom);
    preview->setText(WindowTitle::format(sample, mode->currentData().toString(),
                       pattern->text(), showState->isChecked(), showPosition->isChecked()));
  };
  connect(mode, &QComboBox::currentIndexChanged, page, refresh);
  connect(pattern, &QLineEdit::textChanged, page, refresh);
  connect(showState, &QCheckBox::toggled, page, refresh);
  connect(showPosition, &QCheckBox::toggled, page, refresh);
  refresh();
  apply->append([=] {
    profile.settings.setValue("ui/titleMode", mode->currentData());
    profile.settings.setValue("ui/titleTemplate", pattern->text());
    profile.settings.setValue("ui/titleState", showState->isChecked());
    profile.settings.setValue("ui/titlePosition", showPosition->isChecked());
  });
}
