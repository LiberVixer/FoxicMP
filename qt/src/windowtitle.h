#pragma once
#include <QString>
class QSettings;
namespace WindowTitle {
struct Media {
  QString source, title, state, position, duration;
  bool active = false;
};
QString name(const QString &source);
QString path(const QString &source);
QString render(const QSettings &, const Media &);
QString format(const Media &, const QString &mode, const QString &pattern,
               bool showState, bool showPosition);
QString idle();
} // namespace WindowTitle
