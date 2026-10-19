#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace OverlayMetric {
constexpr DWORD CpuUsage = 1u << 0, Frequency = 1u << 1, Temperature = 1u << 2, Power = 1u << 3;
constexpr DWORD Memory = 1u << 4, Gpu = 1u << 5, Keyboard = 1u << 6, Mouse = 1u << 7;
constexpr DWORD All = 0xff;
constexpr unsigned Count = 8;
}

struct OverlaySettings {
    DWORD position = 4; // Existing corner IDs: TR, TL, BR, BL, top center; 5 = custom.
    DWORD vertical = 0;
    int x = 16, y = 16, margin = 16; // Logical pixels, relative to monitor work area.
    int fontPoints = 18;
    int bold = 1, italic = 0;
    COLORREF color = RGB(255, 255, 0), outlineColor = RGB(15, 23, 35);
    int outlineWidth = 2, opacity = 100, gap = 8;
    UINT hotkeyModifiers = MOD_ALT, hotkeyKey = VK_F1;
    DWORD visibleMetrics = OverlayMetric::All;
    std::wstring font = L"Microsoft YaHei UI", monitor;
};

struct MonitorEntry { HMONITOR handle; MONITORINFOEXW info; };
std::vector<MonitorEntry> ListMonitors();
OverlaySettings LoadOverlaySettings(const wchar_t* key = L"Software\\sixunited\\CpuOverlay");
LSTATUS SaveOverlaySettings(const OverlaySettings& settings, const wchar_t* key = L"Software\\sixunited\\CpuOverlay");
bool ValidateOverlaySettings(const OverlaySettings& settings, std::wstring& error);
std::wstring ShortcutText(const OverlaySettings& settings);
POINT OverlayPosition(const OverlaySettings& settings, RECT work, SIZE size, UINT dpi);

// Reserve a replacement before releasing the current shortcut. Persisting the
// settings is the caller's transaction boundary; Cancel preserves the old key.
class OverlayShortcut {
public:
    explicit OverlayShortcut(HWND owner) : owner_(owner) {}
    ~OverlayShortcut();
    bool Prepare(UINT modifiers, UINT key, DWORD& error);
    void Commit();
    void Cancel();
    bool Registered() const { return active_ != 0; }
    bool Matches(WPARAM id) const { return active_ && id == static_cast<WPARAM>(active_); }
private:
    HWND owner_;
    int active_ = 0, pending_ = 0;
    UINT modifiers_ = 0, key_ = 0, nextModifiers_ = 0, nextKey_ = 0;
};
