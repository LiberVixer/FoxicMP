#include "subtitleeditor.h"
#include "i18n.h"
#include <QtWidgets>
#include <cmath>
SubtitleEditor::SubtitleEditor(
    const SubtitleDocument &input, const QString &path, const QString &profile,
    std::function<double()> now, std::function<void(double)> jump,
    QWidget *parent,
    std::function<QString(const SubtitleDocument &, QString *)> assExport)
    : QDialog(parent), original(input), document(input), source(path),
      directory(profile), position(std::move(now)), seek(std::move(jump)) {
  setObjectName("subtitleEditor");
  setWindowTitle(uiTr("Субтитры — редактор и синхронизация"));
  resize(1000, 660);
  auto *layout = new QVBoxLayout(this);
  auto *file = new QLabel(uiTr("Источник: %1").arg(source));
  file->setTextFormat(Qt::PlainText);
  file->setWordWrap(true);
  layout->addWidget(file);
  auto *note = new QLabel(
      uiTr("Редактируйте начало, конец и текст. Время: чч:мм:сс,мс. "
           "Двойной щелчок по номеру переносит воспроизведение к реплике. "
           "Оригинал меняется только при явном сохранении в тот же файл."));
  note->setWordWrap(true);
  layout->addWidget(note);
  auto *search = new QLineEdit;
  search->setObjectName("subtitleSearch");
  search->setPlaceholderText(uiTr("Поиск по тексту реплик"));
  layout->addWidget(search);
  table = new QTableWidget;
  table->setObjectName("subtitleCueTable");
  table->setColumnCount(4);
  table->setHorizontalHeaderLabels(
      {uiTr("№"), uiTr("Начало"), uiTr("Конец"), uiTr("Текст")});
  table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  table->setColumnWidth(0, 50);
  table->setColumnWidth(1, 145);
  table->setColumnWidth(2, 145);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  layout->addWidget(table, 1);
  status = new QLabel;
  status->setObjectName("subtitleEditorStatus");
  status->setTextFormat(Qt::PlainText);
  status->setWordWrap(true);
  layout->addWidget(status);
  populate();
  connect(table, &QTableWidget::itemChanged, this, [this] { dirty = true; });
  connect(search, &QLineEdit::textChanged, this, [this](const QString &text) {
    for (int i = 0; i < table->rowCount(); ++i)
      table->setRowHidden(
          i, !table->item(i, 3)->text().contains(text, Qt::CaseInsensitive));
  });
  auto *navigation = new QHBoxLayout;
  layout->addLayout(navigation);
  const auto button = [this](QHBoxLayout *row, const QString &title,
                             const QString &name,
                             std::function<void()> callback) {
    auto *button = new QPushButton(title);
    button->setObjectName(name);
    row->addWidget(button);
    connect(button, &QPushButton::clicked, this, std::move(callback));
    return button;
  };
  const auto jumpSelected = [this] {
    if (!collect() || table->currentRow() < 0)
      return;
    seek(document.cues[table->currentRow()].start / 1000.);
  };
  button(navigation, uiTr("К выбранной реплике"), "subtitleJump", jumpSelected);
  connect(table, &QTableWidget::cellDoubleClicked, this,
          [jumpSelected](int, int column) {
            if (!column)
              jumpSelected();
          });
  button(navigation, uiTr("Текущая реплика"), "subtitleCurrent", [this] {
    if (!collect())
      return;
    const qint64 now = std::llround(position() * 1000);
    int row = -1;
    for (int i = 0; i < document.cues.size(); ++i)
      if (document.cues[i].start <= now &&
          (row < 0 || document.cues[i].start > document.cues[row].start))
        row = i;
    if (row >= 0) {
      table->setCurrentCell(row, 0);
      table->scrollToItem(table->item(row, 0));
    }
  });
  auto *shift = new QDoubleSpinBox;
  shift->setObjectName("subtitleShiftSeconds");
  shift->setRange(-86400, 86400);
  shift->setDecimals(3);
  shift->setSuffix(uiTr(" с"));
  navigation->addWidget(shift);
  button(navigation, uiTr("Сдвинуть все"), "subtitleShift", [this, shift] {
    QString error;
    if (collect() && document.retime(1, shift->value() * 1000, &error)) {
      dirty = true;
      populate();
    } else if (!error.isEmpty())
      message(error);
  });
  button(navigation, uiTr("Выбранная начинается сейчас"), "subtitleBindNow",
         [this] {
           if (!collect() || table->currentRow() < 0)
             return;
           QString error;
           const auto delta =
               position() * 1000 - document.cues[table->currentRow()].start;
           if (!document.retime(1, delta, &error))
             message(error);
           else {
             dirty = true;
             populate();
           }
         });
  auto *sync = new QHBoxLayout;
  layout->addLayout(sync);
  for (int point : {1, 2})
    button(sync, uiTr("Точка %1: выбранная → сейчас").arg(point),
           "subtitleAnchor" + QString::number(point), [this, point] {
             if (!collect() || table->currentRow() < 0)
               return;
             auto &s = point == 1 ? source1 : source2;
             auto &t = point == 1 ? target1 : target2;
             s = document.cues[table->currentRow()].start;
             t = std::llround(position() * 1000);
             updateAnchors();
           });
  button(sync, uiTr("Применить две точки"), "subtitleTwoPoint", [this] {
    if (!collect())
      return;
    QString error;
    if (source1 < 0 || source2 < 0)
      message(uiTr("Сначала задайте обе точки"));
    else if (!document.synchronize(source1, target1, source2, target2, &error))
      message(error);
    else {
      dirty = true;
      populate();
      source1 = source2 = -1;
      updateAnchors();
    }
  });
  anchors = new QLabel;
  anchors->setTextFormat(Qt::PlainText);
  layout->addWidget(anchors);
  updateAnchors();
  auto *styledCopy =
      new QCheckBox(uiTr("Применить как ASS-копию с моим оформлением (стили "
                         "исходника будут заменены)"));
  styledCopy->setObjectName("subtitleAssCopy");
  styledCopy->setVisible(bool(assExport));
  layout->addWidget(styledCopy);
  auto *footer = new QHBoxLayout;
  layout->addLayout(footer);
  button(footer, uiTr("Сбросить правки"), "subtitleResetEdits", [this] {
    document = original;
    dirty = false;
    source1 = source2 = -1;
    populate();
    updateAnchors();
  });
  button(footer, uiTr("Копировать текст"), "subtitleCopySelected", [this] {
    if (table->currentRow() >= 0)
      QApplication::clipboard()->setText(
          table->item(table->currentRow(), 3)->text());
  });
  auto *encoding = new QComboBox;
  encoding->setObjectName("subtitleSaveEncoding");
  encoding->addItems(
      {"UTF-8", "CP1251", "KOI8-R", "UTF-16LE", "UTF-16BE", "ISO-8859-1"});
  footer->addWidget(encoding);
  button(footer, uiTr("Сохранить как…"), "subtitleSaveAs", [this, encoding] {
    const QString filter = document.format == "ass"
                               ? "ASS (*.ass);;SubRip (*.srt);;WebVTT (*.vtt)"
                               : "SubRip (*.srt);;WebVTT (*.vtt)";
    QString selected;
    auto path = QFileDialog::getSaveFileName(
        this, uiTr("Сохранить исправленные субтитры"),
        QFileInfo(source).absolutePath() + "/" +
            QFileInfo(source).completeBaseName() + "-corrected." +
            document.format,
        filter, &selected);
    if (path.isEmpty())
      return;
    if (QFileInfo(path).suffix().isEmpty())
      path += selected.contains("*.ass")   ? ".ass"
              : selected.contains("*.vtt") ? ".vtt"
                                           : ".srt";
    QString error;
    if (!saveTo(path, encoding->currentText(), &error))
      message(error);
    else
      message(uiTr("Сохранено: %1").arg(path));
  });
  button(
      footer, uiTr("Применить копию"), "subtitleApplyCopy",
      [this, styledCopy, assExport] {
        if (!collect())
          return;
        QDir folder(directory + "/Subtitles/Edits");
        if (!folder.mkpath(".")) {
          message(uiTr("Не удалось создать папку исправленных субтитров"));
          return;
        }
        const bool asAss = styledCopy->isChecked() && bool(assExport);
        const auto path =
            folder.filePath((asAss ? QString("foxicmp-styled-") : QString()) +
                            QUuid::createUuid().toString(QUuid::WithoutBraces) +
                            "." + (asAss ? QString("ass") : document.format));
        QString error;
        bool ok;
        if (asAss) {
          const auto text = assExport(document, &error);
          QSaveFile file(path);
          const auto bytes = text.toUtf8();
          ok = !text.isEmpty() && file.open(QIODevice::WriteOnly) &&
               file.write(bytes) == bytes.size() && file.commit();
          if (!ok && error.isEmpty())
            error = file.errorString();
        } else
          ok = document.save(path, document.format, "UTF-8", &error);
        if (!ok) {
          message(error);
          return;
        }
        dirty = false;
        emit applyRequested(path);
        message(uiTr("Копия сохранена рядом с программой и отправлена плееру. "
                     "Задержка сброшена."));
      });
  button(footer, uiTr("Закрыть"), "subtitleEditorClose", [this] { close(); });
}
void SubtitleEditor::message(const QString &text) { status->setText(text); }
void SubtitleEditor::populate() {
  QSignalBlocker block(table);
  const int selected = table->currentRow();
  table->setRowCount(document.cues.size());
  for (int i = 0; i < document.cues.size(); ++i) {
    const auto &cue = document.cues[i];
    const QStringList values{QString::number(i + 1),
                             SubtitleDocument::timeText(cue.start),
                             SubtitleDocument::timeText(cue.end), cue.text};
    for (int c = 0; c < 4; ++c) {
      auto *item = new QTableWidgetItem(values[c]);
      if (!c)
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
      table->setItem(i, c, item);
    }
  }
  if (!document.cues.isEmpty())
    table->setCurrentCell(
        std::clamp(selected, 0, int(document.cues.size()) - 1), 0);
  message(uiTr("Реплик: %1").arg(document.cues.size()));
}
bool SubtitleEditor::collect() {
  auto copy = document;
  for (int i = 0; i < copy.cues.size(); ++i) {
    copy.cues[i].start = SubtitleDocument::parseTime(table->item(i, 1)->text());
    copy.cues[i].end = SubtitleDocument::parseTime(table->item(i, 2)->text());
    copy.cues[i].text = table->item(i, 3)->text();
  }
  QString error;
  if (!copy.valid(&error)) {
    message(error);
    return false;
  }
  document = std::move(copy);
  return true;
}
void SubtitleEditor::updateAnchors() {
  anchors->setText(
      uiTr("Точки: %1 → %2; %3 → %4")
          .arg(source1 < 0 ? "—" : SubtitleDocument::timeText(source1),
               source1 < 0 ? "—" : SubtitleDocument::timeText(target1),
               source2 < 0 ? "—" : SubtitleDocument::timeText(source2),
               source2 < 0 ? "—" : SubtitleDocument::timeText(target2)));
}
bool SubtitleEditor::saveTo(const QString &path, const QString &encoding,
                            QString *error) {
  if (!collect()) {
    if (error)
      *error = status->text();
    return false;
  }
  auto format = QFileInfo(path).suffix().toLower();
  if (format == "ssa")
    format = "ass";
  if (!document.save(path, format, encoding, error))
    return false;
  dirty = false;
  return true;
}
void SubtitleEditor::closeEvent(QCloseEvent *event) {
  if (dirty && QMessageBox::question(
                   this, uiTr("Несохранённые субтитры"),
                   uiTr("Закрыть редактор и отбросить несохранённые правки?"),
                   QMessageBox::Discard | QMessageBox::Cancel,
                   QMessageBox::Cancel) != QMessageBox::Discard) {
    event->ignore();
    return;
  }
  event->accept();
  QDialog::done(QDialog::Rejected);
}
void SubtitleEditor::reject() { close(); }
