#include "i18n.h"
#include "classicpreferences.h"
#include "player.h"
#include "videoshaders.h"
#include "window.h"
#include <QtWidgets>

void Window::attachVideoShaderPreferences(QTabWidget *tabs,
                                          QList<std::function<void()>> *apply) {
  auto *page = new QWidget;
  page->setObjectName("videoShadersPage");
  auto *layout = new QVBoxLayout(page);
  tabs->addTab(page, uiTr("Видео — шейдеры"));
  auto *master = new QCheckBox(uiTr("Включить цепочку GLSL (OpenGL)"));
  master->setObjectName("video/shadersEnabled");
  master->setChecked(
      profile.settings.value("video/shadersEnabled", false).toBool());
  layout->addWidget(master);
  auto *split = new QSplitter;
  layout->addWidget(split, 1);
  auto *list = new QListWidget;
  list->setObjectName("videoShaderList");
  split->addWidget(list);
  auto *editorPanel = new QWidget;
  auto *form = new QFormLayout(editorPanel);
  split->addWidget(editorPanel);
  split->setStretchFactor(1, 3);
  auto *name = new QLineEdit;
  name->setObjectName("videoShaderName");
  name->setMaxLength(80);
  form->addRow(uiTr("Имя:"), name);
  auto *stage = new QComboBox;
  stage->setObjectName("videoShaderStage");
  stage->addItem(uiTr("До масштабирования (MAIN)"), "MAIN");
  stage->addItem(uiTr("После масштабирования, до цвета (SCALED)"), "SCALED");
  form->addRow(uiTr("Этап:"), stage);
  auto *code = new QPlainTextEdit;
  code->setObjectName("videoShaderSource");
  code->setLineWrapMode(QPlainTextEdit::NoWrap);
  code->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  form->addRow(code);
  auto *preset = new QComboBox;
  preset->setObjectName("videoShaderPreset");
  preset->addItem(uiTr("Без изменения"), "identity");
  preset->addItem(uiTr("Оттенки серого"), "gray");
  preset->addItem(uiTr("Инверсия"), "invert");
  preset->addItem(uiTr("Мягкая резкость"), "sharpen");
  auto *row = new QHBoxLayout;
  row->addWidget(preset);
  const auto button = [&](const QString &title, const QString &id) {
    auto *b = new QPushButton(title);
    b->setObjectName(id);
    row->addWidget(b);
    return b;
  };
  auto *add = button(uiTr("Добавить"), "videoShaderAdd"),
       *remove = button(uiTr("Удалить"), "videoShaderRemove"),
       *up = button(uiTr("Выше"), "videoShaderUp"),
       *down = button(uiTr("Ниже"), "videoShaderDown");
  layout->addLayout(row);
  auto *tools = new QHBoxLayout;
  auto *check = new QPushButton(uiTr("Проверить GLSL"));
  check->setObjectName("videoShaderCheck");
  tools->addWidget(check);
  auto *import = new QPushButton(uiTr("Импортировать GLSL…"));
  import->setObjectName("videoShaderImport");
  tools->addWidget(import);
  auto *reset = new QPushButton(uiTr("Очистить цепочку шейдеров"));
  reset->setObjectName("videoShaderReset");
  tools->addWidget(reset);
  layout->addLayout(tools);
  auto *status = new QLabel;
  status->setObjectName("videoShaderStatus");
  status->setWordWrap(true);
  status->setTextFormat(Qt::PlainText);
  layout->addWidget(status);
  auto *note = new QLabel(uiTr(
      "Редактор одного GLSL hook на элемент: vec4 hook(), HOOKED_tex/texOff и "
      "координаты HOOKED_pos. До 16 элементов по 64 КиБ. Метаданные mpv, "
      "кроме //!HOOK, //!DESC и //!BIND HOOKED, не поддерживаются. Импорт "
      "mpv-GLSL добавляет несколько независимых проходов; //!SAVE, //!WHEN и "
      "compute отклоняются. "
      "Порядок действует внутри каждого этапа; MAIN всегда раньше SCALED. "
      "Проверка — компиляция OpenGL 3.3 в отдельном процессе, а не проверка "
      "результата на GPU плеера. В Software шейдеры не действуют. OK сохраняет "
      "текст в INI и генерирует файлы Shaders рядом с программой; Отмена "
      "ничего не применяет."));
  note->setWordWrap(true);
  layout->addWidget(note);
  auto selected = std::make_shared<bool>(false);
  const auto entries = [list] {
    QList<VideoShaders::Entry> out;
    for (int i = 0; i < list->count(); ++i) {
      const auto *item = list->item(i);
      const auto m = item->data(Qt::UserRole).toMap();
      out.append({m.value("name").toString(), m.value("stage").toString(),
                  m.value("source").toString(),
                  item->checkState() == Qt::Checked});
    }
    return out;
  };
  const auto insert = [list](const VideoShaders::Entry &entry) {
    auto *item = new QListWidgetItem(entry.name, list);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);
    item->setData(Qt::UserRole, QVariantMap{{"name", entry.name},
                                            {"stage", entry.stage},
                                            {"source", entry.source}});
  };
  QString error;
  for (const auto &entry : VideoShaders::read(profile.settings, &error))
    insert(entry);
  status->setText(
      error.isEmpty()
          ? player->QObject::property("foxicmpShaderStatus").toString()
          : error);
  connect(list, &QListWidget::currentRowChanged, page, [=](int row) {
    *selected = true;
    const auto m =
        row >= 0 ? list->item(row)->data(Qt::UserRole).toMap() : QVariantMap();
    name->setText(m.value("name").toString());
    stage->setCurrentIndex(std::max(0, stage->findData(m.value("stage"))));
    code->setPlainText(m.value("source").toString());
    name->setEnabled(row >= 0);
    stage->setEnabled(row >= 0);
    code->setEnabled(row >= 0);
    *selected = false;
  });
  const auto store = [=] {
    if (*selected || !list->currentItem())
      return;
    auto *item = list->currentItem();
    item->setText(name->text());
    item->setData(Qt::UserRole, QVariantMap{{"name", name->text()},
                                            {"stage", stage->currentData()},
                                            {"source", code->toPlainText()}});
  };
  connect(name, &QLineEdit::textChanged, page, store);
  connect(stage, &QComboBox::currentIndexChanged, page, store);
  connect(code, &QPlainTextEdit::textChanged, page, store);
  connect(add, &QPushButton::clicked, page, [=] {
    if (list->count() >= 16)
      return;
    insert({preset->currentText(), "MAIN",
            VideoShaders::preset(preset->currentData().toString()), true});
    list->setCurrentRow(list->count() - 1);
  });
  connect(remove, &QPushButton::clicked, page,
          [=] { delete list->takeItem(list->currentRow()); });
  const auto move = [=](int delta) {
    const int old = list->currentRow(), next = old + delta;
    if (old < 0 || next < 0 || next >= list->count())
      return;
    auto *item = list->takeItem(old);
    list->insertItem(next, item);
    list->setCurrentRow(next);
  };
  connect(up, &QPushButton::clicked, page, [move] { move(-1); });
  connect(down, &QPushButton::clicked, page, [move] { move(1); });
  connect(check, &QPushButton::clicked, page, [=] {
    const auto structure = VideoShaders::validateStructure(entries());
    const auto failure = !structure.isEmpty()
                             ? structure
                             : VideoShaders::compile(code->toPlainText());
    status->setText(failure.isEmpty()
                        ? uiTr("GLSL скомпилирован. Проверьте изображение и "
                               "проходы в статистике.")
                        : failure);
  });
  connect(import, &QPushButton::clicked, page, [=] {
    const auto path = QFileDialog::getOpenFileName(
        page, uiTr("Импортировать mpv GLSL"), QString(),
        uiTr("GLSL-файлы (*.glsl *.hook *.txt);;Все файлы (*)"));
    if (path.isEmpty())
      return;
    QList<VideoShaders::Entry> imported;
    QString failure;
    if (!VideoShaders::importMpvFile(path, &imported, &failure)) {
      status->setText(failure);
      return;
    }
    if (list->count() + imported.size() > 16) {
      status->setText(uiTr("Шейдеры: импорт превышает ограничение 16 элементов."));
      return;
    }
    for (const auto &entry : imported)
      insert(entry);
    list->setCurrentRow(list->count() - imported.size());
    status->setText(uiTr("Импортировано проходов: %1.").arg(imported.size()));
  });
  connect(reset, &QPushButton::clicked, page, [=] {
    list->clear();
    master->setChecked(false);
    name->clear();
    code->clear();
  });
  list->setCurrentRow(list->count() ? 0 : -1);
  apply->append([this, master, entries] {
    profile.settings.setValue("video/shadersEnabled", master->isChecked());
    profile.settings.setValue("video/shadersV1",
                              VideoShaders::encode(entries()));
  });
}
bool Window::validateVideoShaderPreferences(QDialog *dialog) {
  auto *list = dialog->findChild<QListWidget *>("videoShaderList");
  if (!list)
    return true;
  QList<VideoShaders::Entry> entries;
  for (int i = 0; i < list->count(); ++i) {
    auto *item = list->item(i);
    const auto m = item->data(Qt::UserRole).toMap();
    entries.append({m.value("name").toString(), m.value("stage").toString(),
                    m.value("source").toString(),
                    item->checkState() == Qt::Checked});
  }
  auto error = VideoShaders::validateStructure(entries);
  if (error.isEmpty() &&
      dialog->findChild<QCheckBox *>("video/shadersEnabled")->isChecked())
    for (int i = 0; i < entries.size(); ++i) {
      if (!entries[i].enabled)
        continue;
      error = VideoShaders::compile(entries[i].source);
      if (!error.isEmpty()) {
        list->setCurrentRow(i);
        break;
      }
    }
  if (error.isEmpty())
    return true;
  dialog->findChild<QLabel *>("videoShaderStatus")->setText(error);
  ClassicPreferences::selectPage(dialog->findChild<QTabWidget *>(),
      dialog->findChild<QWidget *>("videoShadersPage"));
  return false;
}
