#pragma once
#include "overlay_settings.h"
#include <functional>

constexpr UINT kPositionPicked = WM_APP + 40;
struct SettingsCallbacks {
    std::function<void(HWND)> opened;
    std::function<bool(const OverlaySettings&, std::wstring&)> apply;
    std::function<void(const OverlaySettings&)> preview;
    std::function<void(HWND, const OverlaySettings&)> pickPosition;
};
void ShowSettingsDialog(HWND owner, HINSTANCE instance, const OverlaySettings& settings, const SettingsCallbacks& callbacks);
bool CaptureRegisteredShortcut(HWND dialog, const OverlaySettings& settings);
