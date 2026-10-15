#include "overlay_settings.h"
#include "overlay_renderer.h"
#include <iostream>
#include <algorithm>

namespace {
unsigned checks = 0, failures = 0;
void Check(bool condition, const char* label) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
}

int main() {
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) return 1;
    {
        OverlaySettings s;
        std::wstring error;
        Check(ValidateOverlaySettings(s, error), "defaults valid");
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
        s.hotkeyModifiers = MOD_CONTROL | MOD_SHIFT; s.hotkeyKey = VK_F9; s.monitor = L"\\\\.\\DISPLAY2";
        Check(SaveOverlaySettings(s, key.c_str()) == ERROR_SUCCESS, "atomic settings save");
        const auto loaded = LoadOverlaySettings(key.c_str());
        Check(loaded.font == s.font && loaded.fontPoints == 33 && !loaded.bold && loaded.italic && loaded.color == s.color &&
            loaded.outlineColor == s.outlineColor && loaded.outlineWidth == 4 && loaded.opacity == 70 && loaded.gap == 12, "appearance survives reload");
        Check(loaded.position == 5 && loaded.vertical && loaded.x == 234 && loaded.y == 567 && loaded.monitor == s.monitor &&
            loaded.hotkeyModifiers == s.hotkeyModifiers && loaded.hotkeyKey == VK_F9, "position and shortcut survive reload");
        s.fontPoints = 1000;
        Check(SaveOverlaySettings(s, key.c_str()) == ERROR_INVALID_DATA && LoadOverlaySettings(key.c_str()).fontPoints == 33, "invalid save preserves previous configuration");
        HKEY testKey = nullptr;
        RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_SET_VALUE, &testKey);
        const DWORD junk = 0xffffffff;
        RegSetValueExW(testKey, L"DisplaySettingsV1", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&junk), sizeof(junk));
        RegCloseKey(testKey);
        Check(LoadOverlaySettings(key.c_str()).fontPoints == 18, "corrupt settings safely fall back");
        RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());

        s = {};
        auto horizontal = RenderOverlay(PreviewSample(), 1, 1920, 1040, s);
        s.vertical = 1;
        auto vertical = RenderOverlay(PreviewSample(), 1, 1920, 1040, s);
        Check(horizontal && vertical && horizontal->GetWidth() > 5 * horizontal->GetHeight() && vertical->GetHeight() > vertical->GetWidth(), "both arrangements render distinct geometry");
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
    }
    Gdiplus::GdiplusShutdown(token);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
