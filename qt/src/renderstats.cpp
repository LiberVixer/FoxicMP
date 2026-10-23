#include "i18n.h"
#include "player.h"
#include "window.h"
#include <QtWidgets>
namespace {
class TearingPattern : public QWidget {
  QElapsedTimer elapsed;

public:
  explicit TearingPattern(Player *p) : QWidget(p) {
    setObjectName("videoTearingPattern");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_OpaquePaintEvent);
    elapsed.start();
    auto *timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(16);
    connect(timer, &QTimer::timeout, this, [this] {
      setGeometry(parentWidget()->rect());
      update();
    });
    timer->start();
    setGeometry(p->rect());
    show();
    raise();
  }
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.fillRect(rect(), QColor(20, 20, 20));
    for (int x = 0; x < width(); x += 80)
      p.fillRect(x, 0, 40, height(), QColor(90, 90, 90));
    const int x = int((elapsed.elapsed() % 3000) * width() / 3000);
    p.fillRect(x, 0, 8, height(), Qt::white);
    p.setPen(Qt::yellow);
    p.drawText(rect().adjusted(12, 12, -12, -12),
               Qt::AlignTop | Qt::TextWordWrap,
               uiTr("Тест Qt-композитора: движущаяся вертикальная линия. "
                    "Смотрите на экран; снимок не подтверждает отсутствие "
                    "разрывов. Видео продолжает идти под шаблоном."));
  }
};
QString scalar(const QVariant &v) {
  if (!v.isValid())
    return QString::fromUtf8("—");
  if (v.typeId() == QMetaType::Bool)
    return v.toBool() ? QStringLiteral("yes") : QStringLiteral("no");
  if (v.typeId() == QMetaType::Double)
    return QString::number(v.toDouble(), 'f', 4);
  return v.toString();
}
} // namespace
void Window::showRenderStatistics() {
  if (auto *open = findChild<QDialog *>("renderStatisticsDialog")) {
    open->show();
    open->raise();
    return;
  }
  auto *dialog = new QDialog(this);
  dialog->setObjectName("renderStatisticsDialog");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(uiTr("Статистика видеовывода"));
  dialog->resize(920, 720);
  auto *layout = new QVBoxLayout(dialog);
  auto *summary = new QLabel;
  summary->setObjectName("renderStatisticsSummary");
  summary->setWordWrap(true);
  summary->setTextFormat(Qt::PlainText);
  layout->addWidget(summary);
  auto *table = new QTableWidget(0, 2);
  table->setObjectName("renderStatisticsTable");
  table->setHorizontalHeaderLabels({uiTr("Параметр"), uiTr("Значение")});
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->horizontalHeader()->setStretchLastSection(true);
  layout->addWidget(table, 2);
  auto *passes = new QTableWidget(0, 5);
  passes->setObjectName("renderPassesTable");
  passes->setHorizontalHeaderLabels({uiTr("Кадр"), uiTr("Проход"),
                                     uiTr("Последний, мс"), uiTr("Среднее, мс"),
                                     uiTr("Пик, мс")});
  passes->setEditTriggers(QAbstractItemView::NoEditTriggers);
  passes->horizontalHeader()->setStretchLastSection(true);
  layout->addWidget(passes, 1);
  auto *note = new QLabel(uiTr(
      "Время Qt — длительность вызова отрисовки на UI-потоке; среднее и пик по "
      "последним 120 вызовам. Это не задержка экрана и не FPS видео. Проходы "
      "GPU доступны только если их сообщает libmpv. «—» означает недоступное "
      "значение. Сброс обнуляет местные измерения и базу счётчиков, не меняя "
      "настройки и воспроизведение; новый файл начинает измерения заново."));
  note->setWordWrap(true);
  layout->addWidget(note);
  auto *row = new QHBoxLayout;
  auto *reset = new QPushButton(uiTr("Сбросить измерения"));
  reset->setObjectName("renderStatisticsReset");
  row->addWidget(reset);
  auto *copy = new QPushButton(uiTr("Копировать JSON"));
  copy->setObjectName("renderStatisticsCopy");
  row->addWidget(copy);
  auto *pattern = new QCheckBox(uiTr("Тест разрывов Qt"));
  pattern->setObjectName("renderTearingToggle");
  row->addWidget(pattern);
  auto *close = new QPushButton(uiTr("Закрыть"));
  row->addWidget(close);
  layout->addLayout(row);
  auto overlay = std::make_shared<QPointer<QWidget>>();
  connect(pattern, &QCheckBox::toggled, dialog, [this, overlay](bool on) {
    if (*overlay)
      delete overlay->data();
    *overlay = on ? new TearingPattern(player) : nullptr;
  });
  connect(dialog, &QObject::destroyed, this, [overlay] {
    if (*overlay)
      delete overlay->data();
  });
  connect(close, &QPushButton::clicked, dialog, &QDialog::close);
  connect(reset, &QPushButton::clicked, player, &Player::resetRenderStatistics);
  connect(copy, &QPushButton::clicked, dialog, [this] {
    QApplication::clipboard()->setText(QString::fromUtf8(
        QJsonDocument::fromVariant(player->renderStatistics()).toJson()));
  });
  const auto update = [this, table, passes, summary] {
    const auto s = player->renderStatistics();
    summary->setText(
        uiTr("Фактический вывод: %1. Вызовов Qt: %2; последнее/среднее/пик: %3 "
             "/ %4 / %5 мс.")
            .arg(s.value("renderer").toString(),
                 s.value("renderCalls").toString(), scalar(s.value("lastMs")),
                 scalar(s.value("averageMs")), scalar(s.value("peakMs"))) +
        "\n" + player->QObject::property("foxicmpShaderStatus").toString());
    const QList<QPair<QString, QString>> values{
        {"glDriver", uiTr("Драйвер OpenGL")},
        {"video-codec", uiTr("Видеокодек")},
        {"hwdec-current", uiTr("Аппаратный декодер")},
        {"estimated-vf-fps", uiTr("Оценка FPS видео")},
        {"estimated-display-fps", uiTr("Оценка частоты дисплея")},
        {"video-sync", uiTr("Запрошенная синхронизация")},
        {"display-sync-active", uiTr("Синхронизация дисплея активна")},
        {"avsync", uiTr("A/V, с")},
        {"vsync-jitter", uiTr("Jitter VSync (оценка)")},
        {"frame-drop-count", uiTr("Пропуски вывода с начала измерений")},
        {"decoder-frame-drop-count",
         uiTr("Пропуски декодера с начала измерений")},
        {"mistimed-frame-count",
         uiTr("Несвоевременные кадры с начала измерений")},
        {"vo-delayed-frame-count",
         uiTr("Задержанные кадры с начала измерений")},
        {"swapIntervalRequested", uiTr("Запрос Qt swap interval")},
        {"swapIntervalReported", uiTr("Qt сообщает swap interval")}};
    table->setRowCount(values.size());
    for (int i = 0; i < values.size(); ++i) {
      table->setItem(i, 0, new QTableWidgetItem(values[i].second));
      table->setItem(i, 1,
                     new QTableWidgetItem(scalar(s.value(values[i].first))));
    }
    table->resizeColumnToContents(0);
    passes->setRowCount(0);
    const auto types = s.value("vo-passes").toMap();
    for (const auto &kind : {"fresh", "redraw"})
      for (const auto &value : types.value(kind).toList()) {
        const auto pass = value.toMap();
        const int row = passes->rowCount();
        passes->insertRow(row);
        passes->setItem(row, 0, new QTableWidgetItem(kind));
        passes->setItem(row, 1,
                        new QTableWidgetItem(pass.value("desc").toString()));
        int column = 2;
        for (const auto &key : {"last", "avg", "peak"})
          passes->setItem(row, column++,
                          new QTableWidgetItem(QString::number(
                              pass.value(key).toDouble() / 1000000., 'f', 4)));
      }
    passes->resizeColumnToContents(1);
  };
  auto *timer = new QTimer(dialog);
  timer->setInterval(500);
  connect(timer, &QTimer::timeout, dialog, [dialog, update] {
    if (dialog->isVisible())
      update();
  });
  timer->start();
  update();
  dialog->show();
}
