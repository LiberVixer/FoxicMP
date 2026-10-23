#pragma once
#include <QListWidget>
#include <QPointer>
#include <QWidget>
#include <functional>
class Profile;
class QTabWidget;
class QLineEdit;
class QLabel;
class QProcess;
class PlaylistPanel : public QWidget {
  Q_OBJECT
public:
  enum Role {
    Source = Qt::UserRole,
    Id,
    Duration,
    OriginalOrder,
    FileSize,
    Modified,
    Metadata,
    Invalid
  };
  explicit PlaylistPanel(Profile &profile, QWidget *parent = nullptr);
  ~PlaylistPanel() override;
  QListWidget *activeList() const;
  QListWidget *savedPlayingList() const;
  QList<QListWidget *> lists() const;
  QListWidget *createList(const QString &name);
  void append(QListWidget *list, const QStringList &sources,
              bool expandPlaylists = true);
  void save(QListWidget *playing, const QString &currentId);
  bool removeById(QListWidget *list, const QString &id);
  void removeSelected();
  void clearActive();
  void moveSelected(int delta);
  void updateSummary();
  void applyOptions();
  QMenu *operationsMenu() const { return menu; }
signals:
  void playRequested(QListWidget *list, int row);
  void removing(QListWidget *list, const QList<QListWidgetItem *> &items);
  void removingList(QListWidget *list);
  void changed(QListWidget *list, bool resetOrder = false);
  void importFailed(const QString &message);

protected:
  bool eventFilter(QObject *, QEvent *) override;

private:
  void removeItems(QListWidget *, const QList<QListWidgetItem *> &);
  void closeList(int index);
  void filter();
  void sort(const QString &key);
  void copy(bool cut);
  void paste();
  void removeMissing();
  void trashSelected();
  void browseFolder();
  void information();
  void probeNext();
  void queueProbe(QListWidget *, QListWidgetItem *);
  Profile &profile;
  QTabWidget *tabs;
  QLineEdit *search;
  QLabel *summary;
  QMenu *menu;
  QProcess *probe = nullptr;
  QList<QPair<QPointer<QListWidget>, QString>> probes;
  QString savedPlayingId;
  bool restoring = true, arranging = false;
};
