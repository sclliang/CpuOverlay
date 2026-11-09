#include "overlay_settings.h"
#include "overlay_renderer.h"
#include "settings_dialog.h"
#include "game_overlay_applications.h"
#include "../resources/settings_ids.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <commctrl.h>

namespace {
unsigned checks = 0, failures = 0;
void Check(bool condition, const char* label) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
// Changing only the sampled numbers isolates the rendered value pixels from
// labels. This checks actual column placement without sharing layout calculations.
std::vector<int> ChangedValueStarts(Gdiplus::Bitmap& before, Gdiplus::Bitmap& after, int blankRows) {
    const int width = static_cast<int>(std::max(before.GetWidth(), after.GetWidth()));
    const int height = static_cast<int>(std::max(before.GetHeight(), after.GetHeight()));
    auto pixel = [](Gdiplus::Bitmap& bitmap, int x, int y) {
        Gdiplus::Color color(0, 0, 0, 0);
        if (x < static_cast<int>(bitmap.GetWidth()) && y < static_cast<int>(bitmap.GetHeight())) bitmap.GetPixel(x, y, &color);
        return color.GetValue();
    };
    std::vector<int> starts;
    int left = width, empty = 0;
    for (int y = 0; y < height + blankRows; ++y) {
        int rowLeft = width;
        if (y < height) for (int x = 0; x < width; ++x)
            if (pixel(before, x, y) != pixel(after, x, y)) { rowLeft = x; break; }
        if (rowLeft < width) { left = std::min(left, rowLeft); empty = 0; }
        else if (left < width && ++empty >= blankRows) { starts.push_back(left); left = width; empty = 0; }
    }
    return starts;
}
bool AlignedValues(const std::vector<int>& starts, size_t rows) {
    return starts.size() == rows && *std::max_element(starts.begin(), starts.end()) - *std::min_element(starts.begin(), starts.end()) <= 1;
}
bool SaveBitmap(Gdiplus::Bitmap& bitmap, const std::filesystem::path& path) {
    UINT count = 0, bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    if (!bytes) return false;
    std::vector<BYTE> buffer(bytes);
    const auto encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < count; ++i)
        if (wcscmp(encoders[i].MimeType, L"image/png") == 0) return bitmap.Save(path.c_str(), &encoders[i].Clsid, nullptr) == Gdiplus::Ok;
    return false;
}

// Drive only the dialogs created by this test process. The production callbacks
// and registry transaction are exercised without touching the user's profile.
struct NamesUiTest {
    HWND owner = nullptr;
    int action = 0, stage = 0, ticks = 0, applied = 0;
    bool cancelMain = false, complete = false, editorLimits = true, previewed = false;
    OverlaySettings original, saved, preview;
    std::filesystem::path capture;
    std::wstring key;
};
HWND NamesEditor() {
    HWND result = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
        if (!GetDlgItem(window, IDC_LABEL_CPU)) return TRUE;
        *reinterpret_cast<HWND*>(data) = window; return FALSE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
HWND AppsEditor() {
    HWND result = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
        if (!GetDlgItem(window, IDC_APP_LIST)) return TRUE;
        *reinterpret_cast<HWND*>(data) = window; return FALSE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
void CaptureTestWindow(HWND window, const std::filesystem::path& path) {
    if (path.empty()) return;
    RECT rect{}; GetWindowRect(window, &rect);
    HDC screen = GetWindowDC(window), memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, rect.right - rect.left, rect.bottom - rect.top);
    const auto old = SelectObject(memory, bitmap);
    if (PrintWindow(window, memory, 2)) {
        Gdiplus::Bitmap picture(bitmap, nullptr);
        Check(SaveBitmap(picture, path), "export the actual light name-editor dialog");
    }
    SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(window, screen);
}
void CALLBACK NamesUiTimer(HWND window, UINT, UINT_PTR timer, DWORD) {
    auto& test = *reinterpret_cast<NamesUiTest*>(GetPropW(window, L"CpuOverlay.NamesTest"));
    if (++test.ticks > 200) {
        if (const auto editor = NamesEditor()) PostMessageW(editor, WM_CLOSE, 0, 0);
        KillTimer(window, timer); PostMessageW(window, WM_CLOSE, 0, 0); return;
    }
    if (!test.stage) {
        if (test.action >= 3) {
            const auto editor = AppsEditor(); if (!editor) return;
            const HWND list = GetDlgItem(editor, IDC_APP_LIST); bool found = false;
            for (int i = 0; i < ListView_GetItemCount(list); ++i) {
                wchar_t name[MAX_PATH]{}; ListView_GetItemText(list, i, 0, name, ARRAYSIZE(name));
                if (_wcsicmp(name, L"Weixin.exe") != 0) continue;
                found = true; Check(ListView_GetCheckState(list, i) != FALSE, "excluded running/recent applications open checked");
                ListView_SetCheckState(list, i, FALSE);
            }
            test.editorLimits = found;
            if (!test.capture.empty()) {
                RedrawWindow(editor, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                CaptureTestWindow(editor, test.capture.parent_path() / L"application-exclusions.png");
            }
            test.stage = 1; SendMessageW(editor, WM_COMMAND, test.action == 4 ? IDCANCEL : IDOK, 0); return;
        }
        const auto editor = NamesEditor();
        if (!editor) return;
        for (unsigned i = 0; i < OverlayMetric::Count; ++i) {
            const auto input = GetDlgItem(editor, IDC_LABEL_CPU + i);
            test.editorLimits = test.editorLimits && input && SendMessageW(input, EM_GETLIMITTEXT, 0, 0) == 32;
            SetDlgItemTextW(editor, IDC_LABEL_CPU + i, (L"传感器 " + std::to_wstring(i)).c_str());
        }
        if (test.action == 2) SendMessageW(editor, WM_COMMAND, IDC_NAMES_RESET, 0);
        RedrawWindow(editor, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        CaptureTestWindow(editor, test.capture);
        test.stage = 1;
        SendMessageW(editor, WM_COMMAND, test.action == 1 ? IDCANCEL : IDOK, 0);
        return;
    }
    if (!test.capture.empty()) {
        ShowWindow(window, SW_SHOWNOACTIVATE);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        CaptureTestWindow(window, test.capture.parent_path() / L"native-settings.png");
    }
    SendMessageW(window, WM_COMMAND, IDC_DESKTOP_PREVIEW, 0);
    if (!test.cancelMain) SendMessageW(window, WM_COMMAND, IDC_APPLY, 0);
    test.complete = true;
    KillTimer(window, timer); RemovePropW(window, L"CpuOverlay.NamesTest");
    SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
}
void TestNamesDialog(int action, bool cancelMain, const std::filesystem::path& capture = {}) {
    NamesUiTest test;
    test.action = action; test.cancelMain = cancelMain; test.capture = capture;
    test.original.metricLabels[0] = L"原始 CPU"; test.original.valueGap = 7; test.original.pumpEnabled = true;
    test.original.gameOverlayExcludedApps = {L"Weixin.exe"};
    test.key = L"Software\\sixunited\\CpuOverlay.NamesUiTests." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(action) + (cancelMain ? L".cancel" : L".apply");
    SaveOverlaySettings(test.original, test.key.c_str());
    SettingsCallbacks callbacks;
    callbacks.opened = [&test](HWND window) {
        test.owner = window;
        Check(IsDlgButtonChecked(window, IDC_GAME_ENABLED) == BST_CHECKED, "native display control reflects default configuration");
        for (unsigned i = 0; i < 4; ++i) Check(IsDlgButtonChecked(window, IDC_GAME_D3D11 + i) == BST_CHECKED, "all four backend controls initialize");
        SetPropW(window, L"CpuOverlay.NamesTest", reinterpret_cast<HANDLE>(&test));
        SetTimer(window, 42, 20, NamesUiTimer);
        PostMessageW(window, WM_COMMAND, test.action >= 3 ? IDC_GAME_APPS : IDC_EDIT_NAMES, 0);
    };
    callbacks.preview = [&test](const OverlaySettings& value) { test.previewed = true; test.preview = value; };
    callbacks.apply = [&test](const OverlaySettings& value, std::wstring&) {
        ++test.applied; test.saved = value;
        return SaveOverlaySettings(value, test.key.c_str()) == ERROR_SUCCESS;
    };
    ShowSettingsDialog(nullptr, GetModuleHandleW(nullptr), test.original, callbacks);
    auto expected = action == 1 || action >= 3 ? test.original.metricLabels : OverlayMetric::DefaultLabels();
    if (!action) for (unsigned i = 0; i < OverlayMetric::Count; ++i) expected[i] = L"传感器 " + std::to_wstring(i);
    Check(test.complete && test.editorLimits, "all fifteen name editors open with the intended input limit");
    Check(test.previewed && test.preview.metricLabels == expected, "accept, cancel and reset flow into the desktop preview correctly");
    const auto persisted = LoadOverlaySettings(test.key.c_str());
    Check(test.applied == (cancelMain ? 0 : 1) && persisted.metricLabels == (cancelMain ? test.original.metricLabels : expected),
        "only applying the main settings dialog persists changed names");
    Check(persisted.valueGap == 7 && persisted.pumpEnabled, "renaming and resetting names preserve unrelated settings");
    const std::vector<std::wstring> expectedApps = action == 3 && !cancelMain ? std::vector<std::wstring>{} : test.original.gameOverlayExcludedApps;
    Check(persisted.gameOverlayExcludedApps == expectedApps, "application exclusions honor child confirmation, child cancel and main cancel");
    RegDeleteTreeW(HKEY_CURRENT_USER, test.key.c_str());
}
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) return 1;
    {
        OverlaySettings s;
        std::wstring error;
        Check(ValidateOverlaySettings(s, error), "defaults valid");
        Check(!s.pumpEnabled && s.valueGap == 0 && s.refreshIntervalMs == 1000, "optional pump, label spacing and refresh defaults");
        s.valueGap = 41;
        Check(!ValidateOverlaySettings(s, error), "oversized label/value gap rejected");
        s.valueGap = -1;
        Check(!ValidateOverlaySettings(s, error), "negative label/value gap rejected");
        s = {}; s.refreshIntervalMs = 199;
        Check(!ValidateOverlaySettings(s, error), "refresh interval below 200 ms rejected");
        s.refreshIntervalMs = 5001;
        Check(!ValidateOverlaySettings(s, error), "refresh interval above 5000 ms rejected");
        s.refreshIntervalMs = 200;
        Check(ValidateOverlaySettings(s, error), "minimum refresh interval accepted");
        s.refreshIntervalMs = 5000;
        Check(ValidateOverlaySettings(s, error), "maximum refresh interval accepted");
        s = {};
        s.metricOrder[1] = s.metricOrder[0];
        Check(!ValidateOverlaySettings(s, error), "duplicate metric order rejected");
        s = {}; s.metricOrder[0] = OverlayMetric::Count;
        Check(!ValidateOverlaySettings(s, error), "out-of-range metric order rejected");
        s = {};
        Check(s.visibleMetrics == OverlayMetric::All, "all fifteen hardware metrics visible by default");
        Check(s.metricLabels == OverlayMetric::DefaultLabels(), "default hardware keys preserve the existing display");
        for (const auto& label : std::vector<std::wstring>{L"", L"   ", L"CPU\nTotal", L"CPU\t", std::wstring(33, L'A'),
            std::wstring(L"CPU\0Total", 9), std::wstring(1, static_cast<wchar_t>(0xd800))}) {
            auto invalid = s; invalid.metricLabels[0] = label;
            Check(!ValidateOverlaySettings(invalid, error), "empty, overlong and malformed names are rejected");
        }
        s.metricLabels[0] = std::wstring(32, L'A'); s.metricLabels[2] = L"CPU:核心温度";
        Check(ValidateOverlaySettings(s, error), "maximum-length keys and punctuation are supported");
        s.metricLabels = OverlayMetric::DefaultLabels();
        s.visibleMetrics = 0;
        Check(ValidateOverlaySettings(s, error), "all unchecked is a supported transparent overlay");
        s.visibleMetrics = 1u << OverlayMetric::Count;
        Check(!ValidateOverlaySettings(s, error), "undefined display bits rejected");
        s = {};
        Check(ShortcutText(s) == L"Alt+F1", "default shortcut label");
        s.hotkeyKey = 'A'; s.hotkeyModifiers = 0;
        Check(!ValidateOverlaySettings(s, error), "bare letters cannot steal normal typing");
        s.hotkeyModifiers = MOD_SHIFT;
        Check(!ValidateOverlaySettings(s, error), "shift-letter alone rejected");
        s.hotkeyModifiers = MOD_CONTROL | MOD_ALT; s.hotkeyKey = 'M';
        Check(ValidateOverlaySettings(s, error) && ShortcutText(s) == L"Ctrl+Alt+M", "modified letter shortcut");
        s.hotkeyKey = VK_F12;
        Check(!ValidateOverlaySettings(s, error), "debugger-reserved F12 rejected");
        s = {}; s.fontPoints = 0;
        Check(!ValidateOverlaySettings(s, error), "invalid font size rejected");
        s = {}; s.color = 0xff000000;
        Check(!ValidateOverlaySettings(s, error), "non-RGB color rejected");
        s = {}; s.opacity = 9;
        Check(!ValidateOverlaySettings(s, error), "invisible overlay opacity rejected");
        s = {}; s.gameOverlayBackends = 16;
        Check(!ValidateOverlaySettings(s, error), "unknown native backend rejected");
        s = {}; s.gameOverlayBackends = 0;
        Check(!ValidateOverlaySettings(s, error), "enabled native rendering needs a backend");
        s.gameOverlayEnabled = false;
        Check(ValidateOverlaySettings(s, error), "all native backends may be disabled");

        s = {};
        auto p = OverlayPosition(s, {-1920, 0, 0, 1040}, {800, 44}, 96);
        Check(p.x == -1360 && p.y == 16, "top center on negative-coordinate monitor");
        s.position = 5; s.x = 100; s.y = 200;
        p = OverlayPosition(s, {-1920, 0, 0, 1040}, {500, 300}, 144);
        Check(p.x == -1770 && p.y == 300, "custom placement scales with monitor DPI");
        s.x = 32767; s.y = 32767;
        p = OverlayPosition(s, {0, 0, 1920, 1040}, {500, 300}, 96);
        Check(p.x == 1420 && p.y == 740, "offscreen coordinates clamp to visible work area");
        p = OverlayPosition(s, {0, 0, 320, 200}, {500, 300}, 96);
        Check(p.x == 0 && p.y == 0, "oversized rectangle has safe clamping bounds");

        const std::wstring key = L"Software\\sixunited\\CpuOverlay.SettingsTests." + std::to_wstring(GetCurrentProcessId());
        s = {}; s.position = 5; s.vertical = 1; s.x = 234; s.y = 567;
        s.font = L"Microsoft YaHei"; s.fontPoints = 33; s.bold = 0; s.italic = 1;
        s.color = RGB(25, 130, 220); s.outlineColor = RGB(233, 244, 255); s.outlineWidth = 4;
        s.gap = 12; s.opacity = 70; s.margin = 22;
        s.valueGap = 17; s.pumpEnabled = true; s.refreshIntervalMs = 750;
        s.gameOverlayEnabled = false; s.gameOverlayBackends = 9;
        s.gameOverlayExcludedApps = {L"Weixin.exe", L"WeChat.exe", L"Nioh2.exe"};
        s.hotkeyModifiers = MOD_CONTROL | MOD_SHIFT; s.hotkeyKey = VK_F9; s.monitor = L"\\\\.\\DISPLAY2";
        s.visibleMetrics = OverlayMetric::Temperature | OverlayMetric::Keyboard | OverlayMetric::Mouse;
        s.metricLabels[0] = std::wstring(32, L'C'); s.metricLabels[2] = L"核心温度"; s.metricLabels[7] = L"Mouse Battery";
        std::swap(s.metricOrder[0], s.metricOrder[13]);
        Check(SaveOverlaySettings(s, key.c_str()) == ERROR_SUCCESS, "atomic settings save");
        const auto loaded = LoadOverlaySettings(key.c_str());
        Check(loaded.visibleMetrics == s.visibleMetrics, "selected metrics survive reload");
        Check(loaded.metricOrder == s.metricOrder, "custom display order survives reload");
        Check(loaded.metricLabels == s.metricLabels, "per-metric Chinese and English keys survive reload");
        Check(!loaded.gameOverlayEnabled && loaded.gameOverlayBackends == 9, "native backend selection survives atomic save/reload");
        Check(loaded.gameOverlayExcludedApps == s.gameOverlayExcludedApps &&
            GameOverlay::ContainsApplication(loaded.gameOverlayExcludedApps, L"WEIXIN.EXE") &&
            !GameOverlay::ContainsApplication(loaded.gameOverlayExcludedApps, L"eldenring.exe"), "per-application exclusions persist and match without case sensitivity");
        auto invalidApps = loaded; invalidApps.gameOverlayExcludedApps.push_back(L"weixin.exe");
        Check(SaveOverlaySettings(invalidApps, key.c_str()) == ERROR_INVALID_DATA, "duplicate executable exclusions preserve valid settings");
        invalidApps = loaded; invalidApps.gameOverlayExcludedApps = {L"C:\\Apps\\Weixin.exe"};
        Check(SaveOverlaySettings(invalidApps, key.c_str()) == ERROR_INVALID_DATA, "exclusions accept executable names rather than ambiguous full paths");
        Check(loaded.valueGap == 17 && loaded.pumpEnabled && loaded.refreshIntervalMs == 750,
            "label/value spacing, optional pump and refresh interval survive reload");
        Check(loaded.font == s.font && loaded.fontPoints == 33 && !loaded.bold && loaded.italic && loaded.color == s.color &&
            loaded.outlineColor == s.outlineColor && loaded.outlineWidth == 4 && loaded.opacity == 70 && loaded.gap == 12, "appearance survives reload");
        Check(loaded.position == 5 && loaded.vertical && loaded.x == 234 && loaded.y == 567 && loaded.monitor == s.monitor &&
            loaded.hotkeyModifiers == s.hotkeyModifiers && loaded.hotkeyKey == VK_F9, "position and shortcut survive reload");
        s.fontPoints = 1000;
        Check(SaveOverlaySettings(s, key.c_str()) == ERROR_INVALID_DATA && LoadOverlaySettings(key.c_str()).fontPoints == 33, "invalid save preserves previous configuration");
        HKEY testKey = nullptr;
        RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &testKey);
        DWORD size = 0;
        RegQueryValueExW(testKey, L"DisplaySettingsV9", nullptr, nullptr, nullptr, &size);
        // Frozen fixture offsets describe the historical on-disk formats.
        constexpr DWORD v1Bytes = 196, v5Bytes = 272, v6Bytes = 340, visibleOffset = 196, orderOffset = 200;
        if (size <= v6Bytes) {
            std::cerr << "Cannot read test registry profile.\n";
            if (testKey) RegCloseKey(testKey);
            return 1;
        }
        std::vector<BYTE> profile(size);
        RegQueryValueExW(testKey, L"DisplaySettingsV9", nullptr, nullptr, profile.data(), &size);
        const auto savedProfileV9 = profile;
        auto savedProfileV8 = profile; savedProfileV8.resize(1272);
        auto write = [testKey](const wchar_t* name, const std::vector<BYTE>& bytes) {
            return RegSetValueExW(testKey, name, 0, REG_BINARY, bytes.data(), static_cast<DWORD>(bytes.size()));
        };
        auto header = [](std::vector<BYTE>& bytes, DWORD version) {
            const DWORD value[]{version, static_cast<DWORD>(bytes.size())};
            std::memcpy(bytes.data(), value, sizeof(value));
        };
        auto visible = [](std::vector<BYTE>& bytes, DWORD value) {
            std::memcpy(bytes.data() + visibleOffset, &value, sizeof(value));
        };
        header(savedProfileV8, 8); write(L"DisplaySettingsV8", savedProfileV8); RegDeleteValueW(testKey, L"DisplaySettingsV9");
        const auto migratedV8 = LoadOverlaySettings(key.c_str());
        Check(migratedV8.gameOverlayExcludedApps.empty() && !migratedV8.gameOverlayEnabled && migratedV8.gameOverlayBackends == 9 &&
            migratedV8.metricLabels == loaded.metricLabels, "V8 migration preserves display preferences and starts with an empty exclusion list");
        RegDeleteValueW(testKey, L"DisplaySettingsV8");
        const std::vector<BYTE> savedLegacyPrefix(profile.begin(), profile.begin() + v5Bytes);
        auto version7 = savedProfileV8; version7.resize(version7.size() - 2 * sizeof(DWORD)); header(version7, 7);
        write(L"DisplaySettingsV7", version7); RegDeleteValueW(testKey, L"DisplaySettingsV8");
        const auto migratedV7 = LoadOverlaySettings(key.c_str());
        Check(migratedV7.metricLabels == loaded.metricLabels && migratedV7.fontPoints == 33 &&
            migratedV7.gameOverlayEnabled && migratedV7.gameOverlayBackends == 15, "V7 preserves labels and enables the four built-in backends");
        RegDeleteValueW(testKey, L"DisplaySettingsV7");
        auto oldProfile = savedLegacyPrefix;
        oldProfile.resize(v6Bytes); header(oldProfile, 6);
        std::vector<DWORD> expandedOrder{16};
        expandedOrder.insert(expandedOrder.end(), loaded.metricOrder.begin(), loaded.metricOrder.end());
        expandedOrder.insert(expandedOrder.begin() + 5, 15);
        std::memcpy(oldProfile.data() + v5Bytes, expandedOrder.data(), 17 * sizeof(DWORD));
        visible(oldProfile, loaded.visibleMetrics | (3u << 15));
        write(L"DisplaySettingsV6", oldProfile); RegDeleteValueW(testKey, L"DisplaySettingsV8");
        const auto migratedV6 = LoadOverlaySettings(key.c_str());
        Check(migratedV6.metricOrder == loaded.metricOrder && migratedV6.visibleMetrics == loaded.visibleMetrics &&
            migratedV6.metricLabels == OverlayMetric::DefaultLabels(), "V6 removes retired interleaved metrics and restores default hardware labels");
        Check(migratedV6.valueGap == 17 && migratedV6.pumpEnabled && migratedV6.refreshIntervalMs == 750 &&
            migratedV6.fontPoints == 33 && migratedV6.hotkeyKey == VK_F9, "V6 preserves hardware, appearance and shortcut preferences");
        visible(oldProfile, 3u << 15); write(L"DisplaySettingsV6", oldProfile);
        Check(LoadOverlaySettings(key.c_str()).visibleMetrics == 0, "a V6 profile with only removed metrics remains fully hidden");
        visible(oldProfile, 0); write(L"DisplaySettingsV6", oldProfile);
        Check(LoadOverlaySettings(key.c_str()).visibleMetrics == 0, "V6 all-hidden choice remains hidden");
        std::memcpy(oldProfile.data() + v5Bytes + sizeof(DWORD), expandedOrder.data(), sizeof(DWORD));
        write(L"DisplaySettingsV6", oldProfile);
        Check(LoadOverlaySettings(key.c_str()).fontPoints == 18, "duplicate V6 entries safely reject corrupt migration");
        RegDeleteValueW(testKey, L"DisplaySettingsV6");

        oldProfile = savedLegacyPrefix; header(oldProfile, 5); write(L"DisplaySettingsV5", oldProfile);
        const auto migratedV5 = LoadOverlaySettings(key.c_str());
        Check(migratedV5.metricOrder == loaded.metricOrder && migratedV5.valueGap == 17 && migratedV5.pumpEnabled &&
            migratedV5.refreshIntervalMs == 750 && migratedV5.visibleMetrics == loaded.visibleMetrics &&
            migratedV5.metricLabels == OverlayMetric::DefaultLabels(), "V5 preserves settings and supplies default label names");
        visible(oldProfile, 0); write(L"DisplaySettingsV5", oldProfile);
        Check(LoadOverlaySettings(key.c_str()).visibleMetrics == 0, "V5 all-hidden choice remains hidden");
        oldProfile = savedLegacyPrefix; oldProfile.resize(260); header(oldProfile, 4);
        write(L"DisplaySettingsV4", oldProfile); RegDeleteValueW(testKey, L"DisplaySettingsV5");
        const auto migratedV4 = LoadOverlaySettings(key.c_str());
        Check(migratedV4.metricOrder == loaded.metricOrder && migratedV4.valueGap == 0 && !migratedV4.pumpEnabled &&
            migratedV4.refreshIntervalMs == 1000 && migratedV4.visibleMetrics == loaded.visibleMetrics, "V4 preserves order and visibility");
        RegDeleteValueW(testKey, L"DisplaySettingsV4");
        oldProfile.resize(200); header(oldProfile, 3); write(L"DisplaySettingsV3", oldProfile);
        Check(LoadOverlaySettings(key.c_str()).visibleMetrics == (loaded.visibleMetrics | OverlayMetric::LowFps) &&
            LoadOverlaySettings(key.c_str()).metricOrder == OverlayMetric::DefaultOrder(), "V3 supplies original order and low-frame-rate metric");
        RegDeleteValueW(testKey, L"DisplaySettingsV3");
        header(oldProfile, 2); write(L"DisplaySettingsV2", oldProfile);
        Check(LoadOverlaySettings(key.c_str()).visibleMetrics == (loaded.visibleMetrics | OverlayMetric::AddedMetrics), "V2 enables added GPU and frame metrics");
        visible(oldProfile, 0); write(L"DisplaySettingsV2", oldProfile);
        Check(LoadOverlaySettings(key.c_str()).visibleMetrics == 0, "V2 all-hidden choice remains hidden");
        RegDeleteValueW(testKey, L"DisplaySettingsV2");
        oldProfile.resize(v1Bytes); header(oldProfile, 1); write(L"DisplaySettingsV1", oldProfile);
        const auto migrated = LoadOverlaySettings(key.c_str());
        Check(migrated.fontPoints == 33 && migrated.hotkeyKey == VK_F9 && migrated.x == 234 && migrated.color == loaded.color &&
            migrated.visibleMetrics == OverlayMetric::All, "V1 preserves appearance and defaults hardware metrics on");
        s = migrated; s.visibleMetrics = OverlayMetric::Mouse; s.metricLabels[7] = L"鼠标电量";
        Check(SaveOverlaySettings(s, key.c_str()) == ERROR_SUCCESS && LoadOverlaySettings(key.c_str()).visibleMetrics == OverlayMetric::Mouse &&
            LoadOverlaySettings(key.c_str()).metricLabels == s.metricLabels, "V8 names and selections override retained legacy profiles");
        s.metricLabels[7].clear();
        Check(SaveOverlaySettings(s, key.c_str()) == ERROR_INVALID_DATA &&
            LoadOverlaySettings(key.c_str()).metricLabels[7] == L"鼠标电量", "invalid label saves preserve the last valid configuration");
        const DWORD duplicate = 13;
        auto corruptProfile = savedProfileV9;
        std::memcpy(corruptProfile.data() + orderOffset + sizeof(DWORD), &duplicate, sizeof(duplicate));
        write(L"DisplaySettingsV9", corruptProfile);
        Check(LoadOverlaySettings(key.c_str()).metricOrder == OverlayMetric::DefaultOrder(), "corrupt V8 order falls back safely");
        corruptProfile = savedProfileV9;
        const DWORD invalidPump = 2;
        std::memcpy(corruptProfile.data() + v5Bytes - 2 * sizeof(DWORD), &invalidPump, sizeof(invalidPump));
        write(L"DisplaySettingsV9", corruptProfile);
        Check(!LoadOverlaySettings(key.c_str()).pumpEnabled, "invalid pump flag never enables hardware access");
        corruptProfile = savedProfileV9;
        std::array<wchar_t, 33> unterminated; unterminated.fill(L'A');
        std::memcpy(corruptProfile.data() + v5Bytes, unterminated.data(), sizeof(unterminated));
        write(L"DisplaySettingsV9", corruptProfile);
        Check(LoadOverlaySettings(key.c_str()).metricLabels == OverlayMetric::DefaultLabels(), "unterminated labels safely fall back");
        corruptProfile = savedProfileV9;
        const wchar_t newline = L'\n';
        std::memcpy(corruptProfile.data() + v5Bytes, &newline, sizeof(newline));
        write(L"DisplaySettingsV9", corruptProfile);
        Check(LoadOverlaySettings(key.c_str()).metricLabels == OverlayMetric::DefaultLabels(), "persisted control characters are rejected");
        corruptProfile = savedProfileV9;
        const DWORD invalidAppCount = 65;
        std::memcpy(corruptProfile.data() + 1272, &invalidAppCount, sizeof(invalidAppCount));
        write(L"DisplaySettingsV9", corruptProfile);
        Check(LoadOverlaySettings(key.c_str()).gameOverlayExcludedApps.empty(), "oversized persisted application count rejects the corrupt profile");
        const DWORD junk = 0xffffffff;
        RegSetValueExW(testKey, L"DisplaySettingsV9", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&junk), sizeof(junk));
        Check(LoadOverlaySettings(key.c_str()).fontPoints == 33 && LoadOverlaySettings(key.c_str()).visibleMetrics == OverlayMetric::All, "damaged V8 uses intact legacy profile");
        RegDeleteValueW(testKey, L"DisplaySettingsV1"); RegCloseKey(testKey);
        Check(LoadOverlaySettings(key.c_str()).fontPoints == 18, "corrupt settings safely fall back");
        RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());

        s = {};
        auto batteryPreview = PreviewSample();
        auto text = FormatOverlayText(batteryPreview);
        Check(text.find(L"键盘:14% | 鼠标:49%") != std::wstring::npos, "battery metrics share the horizontal separator layout");
        UpdateBattery(batteryPreview.peripherals.keyboard, {}, true, 0, 1460);
        UpdateBattery(batteryPreview.peripherals.mouse, {}, false, 0, 1167);
        text = FormatOverlayText(batteryPreview);
        Check(text.find(L"键盘:14%(未更新)") != std::wstring::npos && text.find(L"鼠标:49%(断开)") != std::wstring::npos, "stale/disconnected readings visibly distinguished from live values");
        Check(FormatOverlayText(CpuSample{}).find(L"键盘:--% | 鼠标:--%") != std::wstring::npos, "unknown batteries are never shown as zero");
        s.visibleMetrics = OverlayMetric::Temperature | OverlayMetric::Mouse;
        Check(FormatOverlayText(PreviewSample(), s) == L"温度:55℃ | 鼠标:49%", "selection removes excluded values and extra separators");
        s.visibleMetrics |= OverlayMetric::Fps;
        std::swap(s.metricOrder[0], s.metricOrder[13]);
        Check(FormatOverlayText(PreviewSample(), s) == L"帧率:144FPS | 温度:55℃ | 鼠标:49%", "custom order places FPS first without changing metric selection");
        s.visibleMetrics &= ~OverlayMetric::Fps;
        Check(FormatOverlayText(PreviewSample(), s) == L"温度:55℃ | 鼠标:49%", "hidden reordered metrics leave no extra separators");
        s = {};
        const wchar_t* names[] = {L"处理器:", L"频率:", L"温度:", L"功耗:", L"内存:", L"显卡:", L"键盘:", L"鼠标:",
            L"型号:", L"显卡温度:", L"显卡频率:", L"显卡功耗:", L"显存:", L"帧率:", L"1% Low:"};
        for (unsigned i = 0; i < OverlayMetric::Count; ++i) {
            s.visibleMetrics = 1u << i;
            const auto single = FormatOverlayText(PreviewSample(), s);
            Check(single.find(names[i]) == 0 && single.find(L'|') == std::wstring::npos, "each checkbox maps to its own metric without separators");
        }
        s.visibleMetrics = 0;
        const auto unknownGpu = FormatOverlayText(CpuSample{});
        Check(unknownGpu.find(L"显卡温度:--℃") != std::wstring::npos && unknownGpu.find(L"帧率:--FPS") != std::wstring::npos,
            "unavailable GPU sensors and frame rate stay unknown");
        Check(unknownGpu.find(L"1% Low:--FPS") != std::wstring::npos, "insufficient frame history stays unknown for 1 percent low");
        Check(unknownGpu.find(L"Codex") == std::wstring::npos && OverlayMetric::Count == 15, "retired quota items are absent from the overlay");
        for (unsigned i = 0; i < OverlayMetric::Count; ++i) {
            OverlaySettings original; original.visibleMetrics = 1u << i;
            const auto expected = FormatOverlayText(PreviewSample(), original);
            auto renamed = original; renamed.metricLabels[i] = L"自定义 " + std::to_wstring(i);
            Check(FormatOverlayText(PreviewSample(), renamed) == renamed.metricLabels[i] + expected.substr(expected.find(L':')),
                "each custom key preserves its original value and unit");
        }
        s = {}; s.visibleMetrics = OverlayMetric::CpuUsage | OverlayMetric::Mouse;
        s.metricLabels[0] = L"CPU:Total"; s.metricLabels[7] = L"鼠标电池";
        std::swap(s.metricOrder[0], s.metricOrder[7]);
        Check(FormatOverlayText(PreviewSample(), s) == L"鼠标电池:49% | CPU:Total:12.3%", "custom keys follow identity through reordering");
        s.visibleMetrics = OverlayMetric::GpuMemory;
        Check(FormatOverlayText(PreviewSample(), s) == L"显存:6.0GB", "VRAM overlay shows used capacity only");
        Check(FormatOverlayText(CpuSample{}, s) == L"显存:--GB", "missing VRAM use never falls back to total capacity");
        auto zeroVram = PreviewSample(); zeroVram.gpu.memoryUsedBytes = 0;
        Check(FormatOverlayText(zeroVram, s) == L"显存:0.0GB", "zero VRAM use remains a valid measurement");
        s.visibleMetrics = 0;
        auto empty = RenderOverlay(PreviewSample(), 1, 1920, 1040, s);
        Gdiplus::Color emptyPixel; if (empty) empty->GetPixel(0, 0, &emptyPixel);
        Check(FormatOverlayText(PreviewSample(), s).empty() && empty && empty->GetWidth() == 1 && empty->GetHeight() == 1 && !emptyPixel.GetA(),
            "all unchecked renders a truly transparent pixel instead of failing");
        const auto positioning = RenderOverlay(PreviewSample(), 1, 1920, 1040, s, true);
        Check(positioning && positioning->GetWidth() > 100, "all unchecked still supports explicit drag positioning");
        s = {};
        auto horizontal = RenderOverlay(PreviewSample(), 1, 1920, 1040, s);
        s.vertical = 1;
        auto vertical = RenderOverlay(PreviewSample(), 1, 1920, 1040, s);
        Check(horizontal && vertical && horizontal->GetWidth() > 5 * horizontal->GetHeight() && vertical->GetHeight() > vertical->GetWidth(), "both arrangements render distinct geometry");
        s = {}; s.visibleMetrics = OverlayMetric::CpuUsage; s.valueGap = 10;
        auto narrowValueGap = RenderOverlay(PreviewSample(), 1, 0, 0, s);
        auto narrowScaledGap = RenderOverlay(PreviewSample(), 2, 0, 0, s);
        s.valueGap = 30;
        auto wideValueGap = RenderOverlay(PreviewSample(), 1, 0, 0, s);
        auto wideScaledGap = RenderOverlay(PreviewSample(), 2, 0, 0, s);
        Check(narrowValueGap && wideValueGap && wideValueGap->GetHeight() == narrowValueGap->GetHeight() &&
            abs(static_cast<int>(wideValueGap->GetWidth()) - static_cast<int>(narrowValueGap->GetWidth()) - 20) <= 1,
            "label/value spacing adds logical pixels without changing font height");
        Check(narrowScaledGap && wideScaledGap &&
            abs(static_cast<int>(wideScaledGap->GetWidth()) - static_cast<int>(narrowScaledGap->GetWidth()) - 40) <= 1,
            "label/value spacing follows DPI scaling");
        s.vertical = 1; s.valueGap = 10; s.visibleMetrics |= OverlayMetric::Power;
        narrowValueGap = RenderOverlay(PreviewSample(), 1, 0, 0, s);
        s.valueGap = 30; wideValueGap = RenderOverlay(PreviewSample(), 1, 0, 0, s);
        Check(narrowValueGap && wideValueGap && wideValueGap->GetHeight() == narrowValueGap->GetHeight() &&
            abs(static_cast<int>(wideValueGap->GetWidth()) - static_cast<int>(narrowValueGap->GetWidth()) - 20) <= 1,
            "vertical label/value spacing does not change row spacing");
        auto beforeValues = PreviewSample(), afterValues = PreviewSample();
        beforeValues.usagePercent = beforeValues.memoryPercent = beforeValues.gpuPercent = 12.3;
        afterValues.usagePercent = afterValues.memoryPercent = afterValues.gpuPercent = 56.7;
        s = {}; s.vertical = 1; s.outlineWidth = 0; s.valueGap = 12;
        s.visibleMetrics = OverlayMetric::CpuUsage | OverlayMetric::Memory | OverlayMetric::Gpu;
        s.metricLabels[0] = L"CPU:Total"; s.metricLabels[4] = L"物理内存"; s.metricLabels[5] = L"显卡核心负载";
        auto renamedBefore = RenderOverlay(beforeValues, 1.5f, 0, 0, s), renamedAfter = RenderOverlay(afterValues, 1.5f, 0, 0, s);
        Check(renamedBefore && renamedAfter && AlignedValues(ChangedValueStarts(*renamedBefore, *renamedAfter, 6), 3),
            "renamed labels containing colons still share one vertical value column");
        for (const float dpiScale : {1.0f, 1.5f, 2.0f}) {
            s = {}; s.vertical = 1; s.outlineWidth = 0;
            s.visibleMetrics = OverlayMetric::CpuUsage | OverlayMetric::Memory | OverlayMetric::Gpu | OverlayMetric::GpuTemperature;
            std::vector<int> zeroGapStarts, wideGapStarts;
            for (const int gap : {0, 20}) {
                s.valueGap = gap;
                auto before = RenderOverlay(beforeValues, dpiScale, 0, 0, s);
                auto after = RenderOverlay(afterValues, dpiScale, 0, 0, s);
                const auto starts = before && after ? ChangedValueStarts(*before, *after, static_cast<int>(4 * dpiScale)) : std::vector<int>{};
                Check(AlignedValues(starts, 3), "vertical values share one rendered column at zero/custom spacing and multiple DPIs");
                (gap ? wideGapStarts : zeroGapStarts) = starts;
            }
            Check(!zeroGapStarts.empty() && !wideGapStarts.empty() &&
                abs(wideGapStarts.front() - zeroGapStarts.front() - static_cast<int>(20 * dpiScale)) <= 1,
                "vertical spacing moves the shared value column by the configured logical pixels");
            s.valueGap = 0;
            std::swap(s.metricOrder[0], s.metricOrder[5]);
            auto before = RenderOverlay(beforeValues, dpiScale, 0, 0, s);
            auto after = RenderOverlay(afterValues, dpiScale, 0, 0, s);
            const auto reordered = before && after ? ChangedValueStarts(*before, *after, static_cast<int>(4 * dpiScale)) : std::vector<int>{};
            Check(AlignedValues(reordered, 3) && !zeroGapStarts.empty() && abs(reordered.front() - zeroGapStarts.front()) <= 1,
                "reordering metrics preserves vertical value column placement");
            s.visibleMetrics &= ~OverlayMetric::GpuTemperature;
            before = RenderOverlay(beforeValues, dpiScale, 0, 0, s);
            after = RenderOverlay(afterValues, dpiScale, 0, 0, s);
            const auto shorter = before && after ? ChangedValueStarts(*before, *after, static_cast<int>(4 * dpiScale)) : std::vector<int>{};
            Check(AlignedValues(shorter, 3) && !zeroGapStarts.empty() && shorter.front() < zeroGapStarts.front() - 4 * dpiScale,
                "hidden widest label releases column width while retaining value alignment");
        }
        if (argc > 1) {
            const std::filesystem::path directory(argv[1]);
            std::filesystem::create_directories(directory);
            for (const int gap : {0, 24}) {
                s = {}; s.vertical = 1; s.valueGap = gap;
                auto example = RenderOverlay(PreviewSample(), 1.5f, 0, 0, s);
                Check(example && SaveBitmap(*example, directory / (L"vertical-gap-" + std::to_wstring(gap) + L".png")),
                    "export vertical layout visual example");
            }
        }
        s = {}; s.vertical = 1; s.valueGap = 30; s.visibleMetrics = OverlayMetric::CpuUsage | OverlayMetric::Power;
        s.fontPoints = 96;
        auto constrained = RenderOverlay(PreviewSample(), 2, 220, 160, s);
        Check(constrained && constrained->GetWidth() <= 220 && constrained->GetHeight() <= 160, "large vertical text fits both dimensions");
        s = {}; s.color = RGB(20, 140, 230); s.outlineWidth = 0; s.opacity = 50;
        auto colored = RenderOverlay(PreviewSample(), 1, 0, 0, s);
        unsigned opaque = 0, transparent = 0;
        BYTE maxAlpha = 0;
        if (colored) for (UINT y = 0; y < colored->GetHeight(); ++y) for (UINT x = 0; x < colored->GetWidth(); ++x) {
            Gdiplus::Color pixel; colored->GetPixel(x, y, &pixel);
            maxAlpha = std::max(maxAlpha, pixel.GetA());
            if (!pixel.GetA()) ++transparent;
            if (pixel.GetA() > 120 && abs(pixel.GetB() - 230) < 4 && abs(pixel.GetR() - 20) < 4) ++opaque;
        }
        Check(colored && maxAlpha <= 128 && maxAlpha >= 120 && opaque > 50 && transparent > 500, "custom color and alpha rendered on a transparent background");
        s.outlineWidth = 4;
        auto outlined = RenderOverlay(PreviewSample(), 1, 0, 0, s);
        maxAlpha = 0;
        if (outlined) for (UINT y = 0; y < outlined->GetHeight(); ++y) for (UINT x = 0; x < outlined->GetWidth(); ++x) {
            Gdiplus::Color pixel; outlined->GetPixel(x, y, &pixel); maxAlpha = std::max(maxAlpha, pixel.GetA());
        }
        Check(outlined && maxAlpha <= 128 && maxAlpha >= 120, "opacity applies once across overlapping fill and outline");
        s.font = L"missing-font-for-test";
        Check(RenderOverlay(PreviewSample(), 1, 0, 0, s) != nullptr, "removed font falls back instead of hiding overlay");

        HWND owner = CreateWindowExW(0, L"STATIC", L"CpuOverlay shortcut test", 0, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        HWND blocker = CreateWindowExW(0, L"STATIC", L"CpuOverlay shortcut blocker", 0, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        {
            OverlayShortcut shortcut(owner);
            DWORD status = 0;
            const UINT modifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT;
            Check(shortcut.Prepare(modifiers, VK_F9, status), "reserve original shortcut"); shortcut.Commit();
            Check(shortcut.Registered() && shortcut.Matches(1), "original shortcut committed");
            Check(RegisterHotKey(blocker, 9, modifiers | MOD_NOREPEAT, VK_F10) != FALSE, "external conflict fixture");
            Check(!shortcut.Prepare(modifiers, VK_F10, status) && shortcut.Matches(1), "conflict preserves original shortcut");
            Check(!RegisterHotKey(blocker, 10, modifiers, VK_F9), "original key remains reserved after conflict");
            UnregisterHotKey(blocker, 9);
            Check(shortcut.Prepare(modifiers, VK_F10, status), "reserve candidate after conflict clears");
            shortcut.Cancel();
            Check(shortcut.Matches(1) && RegisterHotKey(blocker, 11, modifiers, VK_F10), "cancel releases candidate and retains original");
            UnregisterHotKey(blocker, 11);
            Check(shortcut.Prepare(modifiers, VK_F10, status), "reserve final replacement"); shortcut.Commit();
            Check(shortcut.Matches(2) && RegisterHotKey(blocker, 12, modifiers, VK_F9), "commit swaps key and releases old reservation");
            UnregisterHotKey(blocker, 12);
        }
        Check(RegisterHotKey(blocker, 13, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F10) != FALSE, "destructor releases shortcut");
        UnregisterHotKey(blocker, 13); DestroyWindow(blocker); DestroyWindow(owner);
        std::filesystem::path capture;
        if (argc > 1) capture = std::filesystem::path(argv[1]) / L"name-editor.png";
        TestNamesDialog(0, false, capture);
        TestNamesDialog(1, false);
        TestNamesDialog(2, false);
        TestNamesDialog(0, true);
        TestNamesDialog(3, false, capture);
        TestNamesDialog(4, false);
        TestNamesDialog(3, true);
    }
    Gdiplus::GdiplusShutdown(token);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
