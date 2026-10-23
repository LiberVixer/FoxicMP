#pragma once
#include <QString>
class Profile;
// Every instance holds a shared lease; reset requires exclusive ownership.
class ProfileLease {
public:
  explicit ProfileLease(const QString &directory);
  ~ProfileLease();
  bool exclusive();
  void shared();
  bool valid() const { return fd >= 0; }
private:
  int fd = -1;
};
namespace ProfileReset {
// Call only after the player window has been destroyed. Assets and media stay.
bool apply(Profile &, bool keepLibrary, QString *backup, QString *error);
// Existing closed profile only; acquire exclusive lease before reading settings.
bool applyDirectory(const QString &, bool keepLibrary, QString *backup, QString *error);
}
