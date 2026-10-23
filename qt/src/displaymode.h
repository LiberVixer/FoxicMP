#pragma once
#include <QList>
#include <QStringList>
#include <functional>

// Only touches a chosen active X11 output. Never invents modelines.
class DisplayMode {
public:
  struct Result {
    bool ok = false;
    QString text;
  };
  struct Mode {
    QString output, name, rate;
    bool current = false;
  };
  using Command = std::function<Result(const QStringList &)>;
  explicit DisplayMode(Command command = {});
  ~DisplayMode();
  QList<Mode> modes(QString *error = nullptr) const;
  bool enter(const QString &output, const QString &name, const QString &rate,
             QString *error = nullptr);
  bool restore(QString *error = nullptr);
  static QList<Mode> parse(const QString &text);

private:
  Command command;
  Mode previous;
  bool changed = false;
};
