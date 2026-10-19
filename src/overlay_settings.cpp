#include "overlay_settings.h"
#include <algorithm>
#include <iterator>

namespace {
constexpr wchar_t kValue[] = L"DisplaySettingsV2";
struct SavedSettings {
    DWORD version = 1, bytes = sizeof(SavedSettings);
    DWORD position, vertical;
    int x, y, margin, fontPoints, bold, italic;
    COLORREF color, outlineColor;
    int outlineWidth, opacity, gap;
    UINT modifiers, key;
    wchar_t font[LF_FACESIZE], monitor[CCHDEVICENAME];
};
struct SavedSettingsV2 { SavedSettings base; DWORD visibleMetrics = OverlayMetric::All; };
DWORD Dword(const wchar_t* key, const wchar_t* name, DWORD fallback) {
    DWORD value = 0, bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS ? value : fallback;
}
}

std::vector<MonitorEntry> ListMonitors() {
    std::vector<MonitorEntry> result;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) reinterpret_cast<std::vector<MonitorEntry>*>(context)->push_back({monitor, info});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

bool ValidateOverlaySettings(const OverlaySettings& s, std::wstring& error) {
    if (s.position > 5 || s.vertical > 1 || s.x < 0 || s.x > 32767 || s.y < 0 || s.y > 32767 || s.margin < 0 || s.margin > 256)
        error = L"位置坐标需在 0–32767 之间，边缘留白需在 0–256 之间。";
    else if (s.font.empty() || s.font.size() >= LF_FACESIZE || s.monitor.size() >= CCHDEVICENAME || s.fontPoints < 8 || s.fontPoints > 96)
        error = L"请选择有效字体，字号需在 8–96 磅之间。";
    else if ((s.bold != 0 && s.bold != 1) || (s.italic != 0 && s.italic != 1) || s.outlineWidth < 0 || s.outlineWidth > 6 ||
        s.opacity < 10 || s.opacity > 100 || s.gap < 0 || s.gap > 40 || s.color > 0xffffff || s.outlineColor > 0xffffff)
        error = L"不透明度需在 10–100% 之间，间距需在 0–40 之间，描边宽度需在 0–6 之间。";
    else if (s.visibleMetrics & ~OverlayMetric::All)
        error = L"显示内容包含无效选项。";
    else if ((s.hotkeyModifiers & ~(MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN)) || !s.hotkeyKey || s.hotkeyKey > 254 ||
        s.hotkeyKey == VK_F12 || s.hotkeyKey == VK_SHIFT || s.hotkeyKey == VK_CONTROL || s.hotkeyKey == VK_MENU ||
        s.hotkeyKey == VK_LWIN || s.hotkeyKey == VK_RWIN || s.hotkeyKey == VK_ESCAPE || s.hotkeyKey == VK_TAB ||
        s.hotkeyKey == VK_RETURN || s.hotkeyKey == VK_SPACE ||
        (!(s.hotkeyModifiers & (MOD_ALT | MOD_CONTROL | MOD_WIN)) && !(s.hotkeyKey >= VK_F1 && s.hotkeyKey <= VK_F11)))
        error = L"请使用 Ctrl、Alt 或 Win 加一个按键，或 F1–F11。F12 及 Esc、Tab、回车、空格不作为全局快捷键。";
    else { error.clear(); return true; }
    return false;
}

OverlaySettings LoadOverlaySettings(const wchar_t* key) {
    // Read the current atomic profile first, then the unmodified V1 profile.
    // Upgrades keep existing appearance/shortcut settings and show all metrics.
    for (DWORD version : {2u, 1u}) {
        SavedSettingsV2 stored{};
        auto& saved = stored.base;
        const DWORD expected = version == 2 ? sizeof(stored) : sizeof(saved);
        DWORD bytes = expected;
        if (RegGetValueW(HKEY_CURRENT_USER, key, version == 2 ? kValue : L"DisplaySettingsV1", RRF_RT_REG_BINARY,
            nullptr, &stored, &bytes) != ERROR_SUCCESS || bytes != expected || saved.version != version || saved.bytes != expected) continue;
        if (std::find(std::begin(saved.font), std::end(saved.font), L'\0') == std::end(saved.font) ||
            std::find(std::begin(saved.monitor), std::end(saved.monitor), L'\0') == std::end(saved.monitor)) continue;
        OverlaySettings s;
        s.position = saved.position; s.vertical = saved.vertical;
        s.x = saved.x; s.y = saved.y; s.margin = saved.margin;
        s.fontPoints = saved.fontPoints; s.bold = saved.bold; s.italic = saved.italic;
        s.color = saved.color; s.outlineColor = saved.outlineColor;
        s.outlineWidth = saved.outlineWidth; s.opacity = saved.opacity; s.gap = saved.gap;
        s.hotkeyModifiers = saved.modifiers; s.hotkeyKey = saved.key;
        s.visibleMetrics = version == 2 ? stored.visibleMetrics : OverlayMetric::All;
        s.font = saved.font; s.monitor = saved.monitor;
        std::wstring error;
        if (ValidateOverlaySettings(s, error)) return s;
    }
    OverlaySettings s;
    // Migrate the old three sizes and monitor selection without changing startup preferences.
    if (Dword(key, L"PositionVersion", 0) >= 2) s.position = std::min<DWORD>(Dword(key, L"Corner", 4), 4);
    const int points[]{14, 18, 23};
    s.fontPoints = points[std::min<DWORD>(Dword(key, L"Size", 1), 2)];
    wchar_t monitor[CCHDEVICENAME]{}; DWORD bytes = sizeof(monitor);
    if (RegGetValueW(HKEY_CURRENT_USER, key, L"Monitor", RRF_RT_REG_SZ, nullptr, monitor, &bytes) == ERROR_SUCCESS) s.monitor = monitor;
    return s;
}

LSTATUS SaveOverlaySettings(const OverlaySettings& s, const wchar_t* key) {
    std::wstring error;
    if (!ValidateOverlaySettings(s, error)) return ERROR_INVALID_DATA;
    SavedSettingsV2 stored{};
    auto& saved = stored.base;
    saved.version = 2; saved.bytes = sizeof(stored);
    stored.visibleMetrics = s.visibleMetrics;
    saved.position = s.position; saved.vertical = s.vertical;
    saved.x = s.x; saved.y = s.y; saved.margin = s.margin;
    saved.fontPoints = s.fontPoints; saved.bold = s.bold; saved.italic = s.italic;
    saved.color = s.color; saved.outlineColor = s.outlineColor;
    saved.outlineWidth = s.outlineWidth; saved.opacity = s.opacity; saved.gap = s.gap;
    saved.modifiers = s.hotkeyModifiers; saved.key = s.hotkeyKey;
    wcscpy_s(saved.font, s.font.c_str()); wcscpy_s(saved.monitor, s.monitor.c_str());
    HKEY opened = nullptr;
    LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &opened, nullptr);
    if (result != ERROR_SUCCESS) return result;
    // One value prevents an interrupted save from leaving a partially applied profile.
    result = RegSetValueExW(opened, kValue, 0, REG_BINARY, reinterpret_cast<const BYTE*>(&stored), sizeof(stored));
    RegCloseKey(opened);
    return result;
}

std::wstring ShortcutText(const OverlaySettings& s) {
    std::wstring result;
    if (s.hotkeyModifiers & MOD_CONTROL) result += L"Ctrl+";
    if (s.hotkeyModifiers & MOD_ALT) result += L"Alt+";
    if (s.hotkeyModifiers & MOD_SHIFT) result += L"Shift+";
    if (s.hotkeyModifiers & MOD_WIN) result += L"Win+";
    if (s.hotkeyKey >= VK_F1 && s.hotkeyKey <= VK_F24) return result + L"F" + std::to_wstring(s.hotkeyKey - VK_F1 + 1);
    wchar_t name[64]{};
    UINT scan = MapVirtualKeyW(s.hotkeyKey, MAPVK_VK_TO_VSC);
    if ((s.hotkeyKey >= VK_PRIOR && s.hotkeyKey <= VK_DOWN) || s.hotkeyKey == VK_INSERT || s.hotkeyKey == VK_DELETE) scan |= 0x100;
    if (GetKeyNameTextW(static_cast<LONG>(scan << 16), name, ARRAYSIZE(name))) return result + name;
    return result + L"Key " + std::to_wstring(s.hotkeyKey);
}

POINT OverlayPosition(const OverlaySettings& s, RECT work, SIZE size, UINT dpi) {
    const int margin = MulDiv(s.margin, static_cast<int>(dpi), 96);
    POINT p{};
    if (s.position == 5) p = {work.left + MulDiv(s.x, static_cast<int>(dpi), 96), work.top + MulDiv(s.y, static_cast<int>(dpi), 96)};
    else {
        const bool left = s.position == 1 || s.position == 3, bottom = s.position == 2 || s.position == 3;
        p = {s.position == 4 ? work.left + (work.right - work.left - size.cx) / 2 :
            left ? work.left + margin : work.right - size.cx - margin,
            bottom ? work.bottom - size.cy - margin : work.top + margin};
    }
    p.x = std::clamp(p.x, work.left, std::max(work.left, work.right - size.cx));
    p.y = std::clamp(p.y, work.top, std::max(work.top, work.bottom - size.cy));
    return p;
}

OverlayShortcut::~OverlayShortcut() { Cancel(); if (active_) UnregisterHotKey(owner_, active_); }
bool OverlayShortcut::Prepare(UINT modifiers, UINT key, DWORD& error) {
    Cancel(); error = ERROR_SUCCESS;
    if (active_ && modifiers_ == modifiers && key_ == key) return true;
    const int next = active_ == 1 ? 2 : 1;
    if (!RegisterHotKey(owner_, next, modifiers | MOD_NOREPEAT, key)) { error = GetLastError(); return false; }
    pending_ = next; nextModifiers_ = modifiers; nextKey_ = key;
    return true;
}
void OverlayShortcut::Commit() {
    if (!pending_) return;
    if (active_) UnregisterHotKey(owner_, active_);
    active_ = pending_; pending_ = 0; modifiers_ = nextModifiers_; key_ = nextKey_;
}
void OverlayShortcut::Cancel() { if (pending_) UnregisterHotKey(owner_, pending_); pending_ = 0; }
