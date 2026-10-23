#pragma once
#include <QList>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include "profilereset.h"
struct Favorite {
  QString id, title, file;
  double position = 0;
  bool rememberPosition = true, portable = false;
};
class Profile {
public:
  explicit Profile(const QString &directory);
  bool writable() const;
  void savePosition(const QString &file, double position, double duration);
  double position(const QString &file) const;
  void recordRecent(const QString &file);
  QStringList recentFiles() const;
  void clearHistory();
  void removeRecent(const QStringList &files);
  void setHistoryLimits(int stored, int menu);
  int historyLimit() const;
  int historyMenuLimit() const;
  static bool sourceMissing(const QString &source);
  static bool canUsePortablePath(const QString &source);
  static bool canRememberFavoritePosition(const QString &source);
  static QString favoriteCategory(const QString &source);
  QList<Favorite> favorites() const;
  QString addFavorite(const QString &title, const QString &file,
                      double position, bool remember = true,
                      bool portable = false);
  void renameFavorite(const QString &id, const QString &title);
  void removeFavorite(const QString &id);
  bool updateFavorite(const QString &id, const QString &title, double position,
                      bool remember, bool portable);
  bool reorderFavorites(const QStringList &ids);
  ProfileLease lease;
  QSettings settings;
  QString directory;

private:
  void storeFavorites(const QList<Favorite> &entries);
  QSet<QString> observedSources, forgottenPositions;
};
