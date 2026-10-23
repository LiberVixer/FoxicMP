#pragma once
#include "subtitledocument.h"
#include <QDialog>
#include <functional>
class QLabel;
class QTableWidget;
class QCloseEvent;
class SubtitleEditor : public QDialog {
  Q_OBJECT
public:
  SubtitleEditor(const SubtitleDocument &document, const QString &source,
                 const QString &profileDirectory,
                 std::function<double()> position,
                 std::function<void(double)> seek, QWidget *parent = nullptr,
                 std::function<QString(const SubtitleDocument &, QString *)>
                     assExport = {});
  bool saveTo(const QString &path, const QString &encoding, QString *error);
signals:
  void applyRequested(const QString &path);

protected:
  void closeEvent(QCloseEvent *) override;
  void reject() override;

private:
  SubtitleDocument original, document;
  QString source, directory;
  std::function<double()> position;
  std::function<void(double)> seek;
  QTableWidget *table;
  QLabel *status, *anchors;
  bool dirty = false;
  qint64 source1 = -1, target1 = -1, source2 = -1, target2 = -1;
  bool collect();
  void populate();
  void message(const QString &);
  void updateAnchors();
};
