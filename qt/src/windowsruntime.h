#pragma once
#include <QtGlobal>
#ifdef Q_OS_WIN
#include <roapi.h>
// Qt's locale/GUI code caches WinRT factories. Their apartment must outlive
// QApplication, including startup CLI queries made before the first window.
class WindowsRuntime {
  CO_MTA_USAGE_COOKIE cookie = nullptr;
  HRESULT status = E_FAIL;
public:
  WindowsRuntime() {
    // Agile system factories can be activated in the implicit MTA even while
    // Qt's GUI uses an STA. Keep that MTA alive until Qt releases its caches.
    status = CoIncrementMTAUsage(&cookie);
    if (SUCCEEDED(status)) status = RoInitialize(RO_INIT_SINGLETHREADED);
  }
  WindowsRuntime(const WindowsRuntime &) = delete;
  WindowsRuntime &operator=(const WindowsRuntime &) = delete;
  bool valid() const { return SUCCEEDED(status); }
  ~WindowsRuntime() {
    if (valid()) RoUninitialize();
    if (cookie) CoDecrementMTAUsage(cookie);
  }
};
#endif
