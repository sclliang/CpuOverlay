#include "overlay_settings.h"
#include <algorithm>
#include <iterator>
#include <cwctype>

namespace {
constexpr wchar_t kValue[] = L"DisplaySettingsV8";
constexpr unsigned kLegacyMetricCount = 15;
constexpr unsigned kVersion6MetricCount = 17;
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
// Freeze historical binary layouts when adding display items.
struct SavedSettingsV4 { SavedSettingsV2 profile; std::array<DWORD, kLegacyMetricCount> order{}; };
struct SavedSettingsV5 { SavedSettingsV4 profile; int valueGap = 0; DWORD pumpEnabled = 0; int refreshIntervalMs = 1000; };
struct SavedSettingsV6 { SavedSettingsV5 legacy; std::array<DWORD, kVersion6MetricCount> order{}; };
struct SavedSettingsV7 {
    SavedSettingsV5 legacy;
    std::array<std::array<wchar_t, OverlayMetric::MaxLabelLength + 1>, OverlayMetric::Count> labels{};
};
struct SavedSettingsV8 { SavedSettingsV7 profile; DWORD gameEnabled = 1, gameBackends = 15; };
DWORD Dword(const wchar_t* key, const wchar_t* name, DWORD fallback) {
    DWORD value = 0, bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS ? value : fallback;
}
}

bool OverlayMetric::ValidLabel(const std::wstring& label) {
    if (label.empty() || label.size() > MaxLabelLength) return false;
    bool visible = false;
    for (size_t i = 0; i < label.size(); ++i) {
        const auto c = static_cast<unsigned>(label[i]);
        if (c < 32 || (c >= 0x7f && c <= 0x9f) || c == 0x2028 || c == 0x2029) return false;
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == label.size() || label[i] < 0xdc00 || label[i] > 0xdfff) return false;
        } else if (c >= 0xdc00 && c <= 0xdfff) return false;
        if (!std::iswspace(static_cast<wint_t>(c))) visible = true;
    }
    return visible;
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
        s.opacity < 10 || s.opacity > 100 || s.gap < 0 || s.gap > 40 || s.valueGap < 0 || s.valueGap > 40 ||
        s.color > 0xffffff || s.outlineColor > 0xffffff)
        error = L"不透明度需在 10–100% 之间，项目/标签与数值间距需在 0–40 之间，描边宽度需在 0–6 之间。";
    else if (s.visibleMetrics & ~OverlayMetric::All)
        error = L"显示内容包含无效选项。";
    else if (s.refreshIntervalMs < 200 || s.refreshIntervalMs > 5000)
        error = L"信息刷新间隔需在 200–5000 毫秒之间。";
    else if ((s.gameOverlayBackends & ~15u) || (s.gameOverlayEnabled && !s.gameOverlayBackends))
        error = L"启用游戏内显示时，请至少选择一种图形接口。";
    else if ((s.hotkeyModifiers & ~(MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN)) || !s.hotkeyKey || s.hotkeyKey > 254 ||
        s.hotkeyKey == VK_F12 || s.hotkeyKey == VK_SHIFT || s.hotkeyKey == VK_CONTROL || s.hotkeyKey == VK_MENU ||
        s.hotkeyKey == VK_LWIN || s.hotkeyKey == VK_RWIN || s.hotkeyKey == VK_ESCAPE || s.hotkeyKey == VK_TAB ||
        s.hotkeyKey == VK_RETURN || s.hotkeyKey == VK_SPACE ||
        (!(s.hotkeyModifiers & (MOD_ALT | MOD_CONTROL | MOD_WIN)) && !(s.hotkeyKey >= VK_F1 && s.hotkeyKey <= VK_F11)))
        error = L"请使用 Ctrl、Alt 或 Win 加一个按键，或 F1–F11。F12 及 Esc、Tab、回车、空格不作为全局快捷键。";
    else {
        DWORD seen = 0;
        for (const auto metric : s.metricOrder) {
            if (metric >= OverlayMetric::Count || (seen & (1u << metric))) {
                error = L"显示顺序需包含每个指标且不能重复。"; return false;
            }
            seen |= 1u << metric;
        }
        for (unsigned i = 0; i < OverlayMetric::Count; ++i) {
            if (!OverlayMetric::ValidLabel(s.metricLabels[i])) {
                error = std::wstring(OverlayMetric::Names[i]) + L"的显示名称需为 1–32 个字符，不能全为空格或包含换行、控制字符。";
                return false;
            }
        }
        error.clear(); return true;
    }
    return false;
}

OverlaySettings LoadOverlaySettings(const wchar_t* key) {
    // Keep V1-V6 intact for rollback. Enable added metrics on a V2 upgrade,
    // except when the user deliberately hid every metric.
    for (DWORD version : {8u, 7u, 6u, 5u, 4u, 3u, 2u, 1u}) {
        SavedSettingsV8 version8{};
        SavedSettingsV7 current{};
        SavedSettingsV6 version6{};
        SavedSettingsV5 stored{};
        auto& saved = stored.profile.profile.base;
        const DWORD expected = version == 8 ? sizeof(version8) : version == 7 ? sizeof(current) : version == 6 ? sizeof(version6) :
            version == 5 ? sizeof(stored) : version == 4 ? sizeof(stored.profile) :
            version >= 2 ? sizeof(stored.profile.profile) : sizeof(saved);
        DWORD bytes = expected;
        const auto name = version == 8 ? kValue : version == 7 ? L"DisplaySettingsV7" : version == 6 ? L"DisplaySettingsV6" : version == 5 ? L"DisplaySettingsV5" : version == 4 ? L"DisplaySettingsV4" : version == 3 ? L"DisplaySettingsV3" :
            version == 2 ? L"DisplaySettingsV2" : L"DisplaySettingsV1";
        void* data = version == 8 ? static_cast<void*>(&version8) : version == 7 ? static_cast<void*>(&current) :
            version == 6 ? static_cast<void*>(&version6) : static_cast<void*>(&stored);
        if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_BINARY, nullptr, data, &bytes) != ERROR_SUCCESS || bytes != expected) continue;
        if (version == 8) { current = version8.profile; stored = current.legacy; }
        else if (version == 7) stored = current.legacy;
        else if (version == 6) stored = version6.legacy;
        if (saved.version != version || saved.bytes != expected) continue;
        if (std::find(std::begin(saved.font), std::end(saved.font), L'\0') == std::end(saved.font) ||
            std::find(std::begin(saved.monitor), std::end(saved.monitor), L'\0') == std::end(saved.monitor)) continue;
        OverlaySettings s;
        s.position = saved.position; s.vertical = saved.vertical;
        s.x = saved.x; s.y = saved.y; s.margin = saved.margin;
        s.fontPoints = saved.fontPoints; s.bold = saved.bold; s.italic = saved.italic;
        s.color = saved.color; s.outlineColor = saved.outlineColor;
        s.outlineWidth = saved.outlineWidth; s.opacity = saved.opacity; s.gap = saved.gap;
        s.hotkeyModifiers = saved.modifiers; s.hotkeyKey = saved.key;
        const auto visible = version >= 2 ? stored.profile.profile.visibleMetrics : OverlayMetric::All;
        const auto allowed = version == 6 ? (1u << kVersion6MetricCount) - 1 : OverlayMetric::All;
        if (visible & ~allowed) continue;
        s.visibleMetrics = visible & OverlayMetric::All;
        if (version == 6) {
            DWORD seen = 0;
            bool valid = true;
            size_t index = 0;
            for (const auto metric : version6.order) {
                if (metric >= kVersion6MetricCount || (seen & (1u << metric))) { valid = false; break; }
                seen |= 1u << metric;
                if (metric < OverlayMetric::Count) s.metricOrder[index++] = metric;
            }
            if (!valid || index != OverlayMetric::Count) continue;
        } else if (version >= 4) {
            DWORD seen = 0;
            bool valid = true;
            for (const auto metric : stored.profile.order) {
                if (metric >= kLegacyMetricCount || (seen & (1u << metric))) { valid = false; break; }
                seen |= 1u << metric;
            }
            if (!valid) continue;
            std::copy(stored.profile.order.begin(), stored.profile.order.end(), s.metricOrder.begin());
        }
        if (version >= 5) {
            if (stored.pumpEnabled > 1) continue;
            s.valueGap = stored.valueGap;
            s.pumpEnabled = stored.pumpEnabled != 0;
            s.refreshIntervalMs = stored.refreshIntervalMs;
        }
        if (version == 3 && s.visibleMetrics) s.visibleMetrics |= OverlayMetric::LowFps;
        if (version == 2) {
            if (s.visibleMetrics & ~0xffu) continue;
            if (s.visibleMetrics) s.visibleMetrics |= OverlayMetric::AddedMetrics;
        }
        if (version >= 7) {
            bool valid = true;
            for (unsigned i = 0; i < OverlayMetric::Count; ++i) {
                const auto& label = current.labels[i];
                if (std::find(label.begin(), label.end(), L'\0') == label.end()) { valid = false; break; }
                s.metricLabels[i] = label.data();
            }
            if (!valid) continue;
        }
        if (version == 8) {
            if (version8.gameEnabled > 1) continue;
            s.gameOverlayEnabled = version8.gameEnabled != 0; s.gameOverlayBackends = version8.gameBackends;
        }
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
    SavedSettingsV8 version8{};
    auto& current = version8.profile;
    auto& stored = current.legacy;
    auto& saved = stored.profile.profile.base;
    saved.version = 8; saved.bytes = sizeof(version8);
    version8.gameEnabled = s.gameOverlayEnabled ? 1 : 0; version8.gameBackends = s.gameOverlayBackends;
    stored.profile.profile.visibleMetrics = s.visibleMetrics;
    stored.profile.order = s.metricOrder;
    for (unsigned i = 0; i < OverlayMetric::Count; ++i)
        wcscpy_s(current.labels[i].data(), current.labels[i].size(), s.metricLabels[i].c_str());
    stored.valueGap = s.valueGap;
    stored.pumpEnabled = s.pumpEnabled ? 1 : 0;
    stored.refreshIntervalMs = s.refreshIntervalMs;
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
    result = RegSetValueExW(opened, kValue, 0, REG_BINARY, reinterpret_cast<const BYTE*>(&version8), sizeof(version8));
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
