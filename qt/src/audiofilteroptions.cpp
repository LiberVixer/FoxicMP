#include "audioprocessing.h"
#include "i18n.h"
#include "classicpreferences.h"
#include "player.h"
#include "window.h"
#include <QtWidgets>

namespace {
QJsonArray presets(const QByteArray &bytes) {
  QJsonArray result;
  QSet<QString> names;
  const auto list = QJsonDocument::fromJson(bytes).array();
  for (const auto &entry : list) {
    const auto item = entry.toObject();
    const auto name = item.value("name").toString().trimmed();
    const auto graph = item.value("graph").toString().trimmed();
    if (!name.isEmpty() && name.size() <= 80 && !names.contains(name) &&
        graph.size() <= 8192 && !graph.contains(QChar(0)) &&
        result.size() < 64) {
      result.append(QJsonObject{{"name", name}, {"graph", graph}});
      names.insert(name);
    }
  }
  return result;
}
} // namespace

void Window::attachAudioFilterPreferences(QTabWidget *tabs,
                                          QList<std::function<void()>> *apply) {
  auto *page = new QWidget;
  page->setObjectName("audioFiltersPage");
  auto *layout = new QVBoxLayout(page);
  tabs->addTab(page, uiTr("Звук — фильтры"));
  auto *enabled = new QCheckBox(uiTr("Включить мою цепочку аудиофильтров"));
  enabled->setObjectName("audio/customEnabled");
  enabled->setChecked(
      profile.settings.value("audio/customEnabled", false).toBool());
  layout->addWidget(enabled);
  auto *selector = new QComboBox;
  selector->setObjectName("audioFilterPresets");
  const QList<QPair<QString, QString>> builtins{
      {uiTr("Без обработки"), ""},
      {uiTr("Речь: срез низких и высоких частот"),
       "highpass=f=100,lowpass=f=8000"},
      {uiTr("Мягкое усиление баса"), "bass=g=3:f=100:w=0.5"},
      {uiTr("Мягкое усиление высоких частот"), "treble=g=3:f=3000:w=0.5"},
      {uiTr("Тише на 6 дБ"), "volume=-6dB"}};
  auto saved = std::make_shared<QJsonArray>(
      presets(profile.settings.value("audio/filterPresetsV1").toByteArray()));
  const auto refresh = [selector, saved, builtins] {
    selector->clear();
    for (const auto &entry : builtins)
      selector->addItem(entry.first, entry.second);
    for (const auto &entry : *saved) {
      const auto item = entry.toObject();
      selector->addItem(item.value("name").toString(),
                        item.value("graph").toString());
    }
  };
  refresh();
  layout->addWidget(selector);
  auto *row = new QHBoxLayout;
  layout->addLayout(row);
  const auto button = [row](const QString &name, const QString &id) {
    auto *b = new QPushButton(name);
    b->setObjectName(id);
    row->addWidget(b);
    return b;
  };
  auto *load = button(uiTr("Загрузить пресет"), "audioFilterLoad");
  auto *save = button(uiTr("Сохранить как…"), "audioFilterSave");
  auto *remove = button(uiTr("Удалить мой пресет"), "audioFilterDelete");
  auto *editor = new QPlainTextEdit(
      profile.settings.value("audio/customGraph").toString());
  editor->setObjectName("audio/customGraph");
  editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  editor->setPlaceholderText("highpass=f=100,lowpass=f=8000");
  layout->addWidget(editor, 1);
  auto *note = new QLabel(uiTr(
      "Цепочка FFmpeg с одним аудиовходом и выходом, фильтры через запятую. "
      "Порядок: микшер → каналы/баланс → усиление → нормализация → моя цепочка "
      "→ ограничитель. Пустая цепочка ничего не добавляет. Выключение "
      "сохраняет текст. "
      "Проверка использует FFmpeg и стерео 48 кГц; доступность фильтров и "
      "работа "
      "с другой раскладкой зависят от сборки libmpv/FFmpeg. Фильтры могут "
      "менять "
      "длительность и задержку звука. Изменения и пресеты сохраняются после "
      "OK."));
  note->setWordWrap(true);
  layout->addWidget(note);
  auto *status = new QLabel;
  status->setObjectName("audioFilterStatus");
  status->setWordWrap(true);
  status->setTextFormat(Qt::PlainText);
  status->setText(
      player->QObject::property("foxicmpCustomGraphError").toString());
  layout->addWidget(status);
  auto *bottom = new QHBoxLayout;
  layout->addLayout(bottom);
  auto *check = new QPushButton(uiTr("Проверить цепочку"));
  check->setObjectName("audioFilterCheck");
  bottom->addWidget(check);
  auto *import = new QPushButton(uiTr("Импорт пресетов…"));
  import->setObjectName("audioFilterImport");
  bottom->addWidget(import);
  auto *exportButton = new QPushButton(uiTr("Экспорт пресетов…"));
  exportButton->setObjectName("audioFilterExport");
  bottom->addWidget(exportButton);
  auto *reset = new QPushButton(uiTr("Сбросить мою цепочку"));
  reset->setObjectName("audioFilterReset");
  layout->addWidget(reset);
  connect(reset, &QPushButton::clicked, page, [enabled, editor] {
    enabled->setChecked(false);
    editor->clear();
  });
  connect(editor, &QPlainTextEdit::textChanged, status, &QLabel::clear);
  connect(check, &QPushButton::clicked, page, [editor, status] {
    const auto error =
        AudioProcessing::validateGraph(editor->toPlainText().trimmed());
    status->setText(
        error.isEmpty()
            ? uiTr("Цепочка прошла проверку FFmpeg (стерео 48 кГц).")
            : error);
  });
  connect(load, &QPushButton::clicked, page, [selector, editor] {
    editor->setPlainText(selector->currentData().toString());
  });
  connect(
      save, &QPushButton::clicked, page,
      [page, editor, saved, refresh, selector, status, builtins] {
        bool ok;
        const auto name =
            QInputDialog::getText(page, uiTr("Сохранить аудиопресет"),
                                  uiTr("Имя (до 80 символов):"),
                                  QLineEdit::Normal, {}, &ok)
                .trimmed();
        if (!ok)
          return;
        const auto graph = editor->toPlainText().trimmed();
        if (name.isEmpty() || name.size() > 80 || graph.size() > 8192 ||
            graph.contains(QChar(0))) {
          status->setText(uiTr("Нужно имя до 80 и цепочка до 8192 символов."));
          return;
        }
        int index = -1;
        for (int i = 0; i < saved->size(); ++i)
          if (saved->at(i).toObject().value("name").toString() == name)
            index = i;
        if (index >= 0) {
          if (QMessageBox::question(page, uiTr("Заменить пресет?"), name) !=
              QMessageBox::Yes)
            return;
          saved->replace(index, QJsonObject{{"name", name}, {"graph", graph}});
        } else {
          if (saved->size() >= 64) {
            status->setText(uiTr("Можно сохранить до 64 своих пресетов."));
            return;
          }
          index = saved->size();
          saved->append(QJsonObject{{"name", name}, {"graph", graph}});
        }
        refresh();
        selector->setCurrentIndex(builtins.size() + index);
      });
  connect(remove, &QPushButton::clicked, page,
          [saved, refresh, selector, builtins] {
            const auto index = selector->currentIndex() - builtins.size();
            if (index >= 0 && index < saved->size()) {
              saved->removeAt(index);
              refresh();
            }
          });
  connect(import, &QPushButton::clicked, page, [page, saved, refresh, status] {
    const auto path = QFileDialog::getOpenFileName(
        page, uiTr("Импорт аудиопресетов"), {}, "JSON (*.json)");
    if (path.isEmpty())
      return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
      status->setText(uiTr("Не удалось прочитать JSON (не более 1 МиБ)."));
      return;
    }
    const auto document = QJsonDocument::fromJson(file.readAll()).object();
    const auto array = document.value("presets").toArray();
    const auto valid =
        presets(QJsonDocument(array).toJson(QJsonDocument::Compact));
    if (document.value("format") != "FoxicMP-audio-filters-v1" ||
        !document.value("presets").isArray() || valid != array) {
      status->setText(
          uiTr("Неверный формат аудиопресетов или превышены ограничения."));
      return;
    }
    *saved = valid;
    refresh();
    status->setText(uiTr(
        "Список моих пресетов заменён в окне. Нажмите OK для сохранения."));
  });
  connect(exportButton, &QPushButton::clicked, page, [page, saved, status] {
    const auto path =
        QFileDialog::getSaveFileName(page, uiTr("Экспорт аудиопресетов"),
                                     "audio-presets.json", "JSON (*.json)");
    if (path.isEmpty())
      return;
    const auto bytes =
        QJsonDocument(QJsonObject{{"format", "FoxicMP-audio-filters-v1"},
                                  {"presets", *saved}})
            .toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
        !file.commit())
      status->setText(uiTr("Не удалось записать JSON."));
    else
      status->setText(uiTr("Мои пресеты экспортированы в JSON."));
  });
  apply->append([this, enabled, editor, saved] {
    profile.settings.setValue("audio/customEnabled", enabled->isChecked());
    profile.settings.setValue("audio/customGraph",
                              editor->toPlainText().trimmed());
    profile.settings.setValue(
        "audio/filterPresetsV1",
        QJsonDocument(*saved).toJson(QJsonDocument::Compact));
  });
}

bool Window::validateAudioFilterPreferences(QDialog *dialog) {
  auto *editor = dialog->findChild<QPlainTextEdit *>("audio/customGraph");
  auto *enabled = dialog->findChild<QCheckBox *>("audio/customEnabled");
  if (!editor || !enabled)
    return true;
  const auto text = editor->toPlainText().trimmed();
  QString error;
  if (text.size() > 8192 || text.contains(QChar(0)))
    error = uiTr("Цепочка должна содержать до 8192 символов без NUL.");
  else if (enabled->isChecked())
    error = AudioProcessing::validateGraph(text);
  if (error.isEmpty())
    return true;
  auto *page = dialog->findChild<QWidget *>("audioFiltersPage");
  ClassicPreferences::selectPage(dialog->findChild<QTabWidget *>(), page);
  dialog->findChild<QLabel *>("audioFilterStatus")->setText(error);
  return false;
}
