#pragma once

// Scoped to settings windows: never changes the process/system colour scheme.
void ApplyClassicSettingsTheme(HWND root);
bool ClassicSettingsUseDarkTheme();
constexpr UINT WM_CLASSIC_SETTINGS_THEME = WM_APP + 187;
