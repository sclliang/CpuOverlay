#include "overlay_settings.h"
#include "overlay_renderer.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <filesystem>

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
}

int wmain(int argc, wchar_t** argv) {
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
        Check(s.visibleMetrics == OverlayMetric::All, "all fifteen metrics visible by default");
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
        s.hotkeyModifiers = MOD_CONTROL | MOD_SHIFT; s.hotkeyKey = VK_F9; s.monitor = L"\\\\.\\DISPLAY2";
        s.visibleMetrics = OverlayMetric::Temperature | OverlayMetric::Keyboard | OverlayMetric::Mouse;
        std::swap(s.metricOrder[0], s.metricOrder[13]);
        Check(SaveOverlaySettings(s, key.c_str()) == ERROR_SUCCESS, "atomic settings save");
        const auto loaded = LoadOverlaySettings(key.c_str());
        Check(loaded.visibleMetrics == s.visibleMetrics, "selected metrics survive reload");
        Check(loaded.metricOrder == s.metricOrder, "custom display order survives reload");
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
        RegQueryValueExW(testKey, L"DisplaySettingsV5", nullptr, nullptr, nullptr, &size);
        std::vector<BYTE> oldProfile(size);
        RegQueryValueExW(testKey, L"DisplaySettingsV5", nullptr, nullptr, oldProfile.data(), &size);
        const auto savedProfileV5 = oldProfile;
        constexpr size_t v5TailBytes = 3 * sizeof(DWORD);
        oldProfile.resize(size - v5TailBytes);
        size = static_cast<DWORD>(oldProfile.size());
        const DWORD v4Header[] = {4, size};
        std::memcpy(oldProfile.data(), v4Header, sizeof(v4Header));
        RegSetValueExW(testKey, L"DisplaySettingsV4", 0, REG_BINARY, oldProfile.data(), size);
        RegDeleteValueW(testKey, L"DisplaySettingsV5");
        const auto migratedV4 = LoadOverlaySettings(key.c_str());
        Check(migratedV4.metricOrder == s.metricOrder && migratedV4.valueGap == 0 && !migratedV4.pumpEnabled &&
            migratedV4.refreshIntervalMs == 1000 && migratedV4.visibleMetrics == s.visibleMetrics,
            "V4 migration preserves order and visibility, disables opt-in pump and keeps original spacing/refresh");
        RegDeleteValueW(testKey, L"DisplaySettingsV4");
        oldProfile.resize(size - sizeof(s.metricOrder));
        size = static_cast<DWORD>(oldProfile.size());
        const DWORD v3Header[] = {3, size};
        std::memcpy(oldProfile.data(), v3Header, sizeof(v3Header));
        RegSetValueExW(testKey, L"DisplaySettingsV3", 0, REG_BINARY, oldProfile.data(), size);
        RegDeleteValueW(testKey, L"DisplaySettingsV5");
        Check(LoadOverlaySettings(key.c_str()).visibleMetrics == (s.visibleMetrics | OverlayMetric::LowFps) &&
            LoadOverlaySettings(key.c_str()).metricOrder == OverlayMetric::DefaultOrder(),
            "V3 migration preserves visibility and supplies the original metric order");
        RegDeleteValueW(testKey, L"DisplaySettingsV3");
        const DWORD v2Header[] = {2, size};
        std::memcpy(oldProfile.data(), v2Header, sizeof(v2Header));
        RegSetValueExW(testKey, L"DisplaySettingsV2", 0, REG_BINARY, oldProfile.data(), size);
        RegDeleteValueW(testKey, L"DisplaySettingsV5");
        Check(LoadOverlaySettings(key.c_str()).visibleMetrics == (s.visibleMetrics | OverlayMetric::AddedMetrics),
            "V2 upgrade preserves old choices and enables new GPU/FPS metrics");
        const DWORD hidden = 0;
        std::memcpy(oldProfile.data() + oldProfile.size() - sizeof(DWORD), &hidden, sizeof(hidden));
        RegSetValueExW(testKey, L"DisplaySettingsV2", 0, REG_BINARY, oldProfile.data(), size);
        Check(LoadOverlaySettings(key.c_str()).visibleMetrics == 0, "V2 all-hidden choice remains hidden after upgrade");
        RegDeleteValueW(testKey, L"DisplaySettingsV2");
        oldProfile.resize(size - sizeof(DWORD));
        const DWORD v1Header[] = {1, static_cast<DWORD>(oldProfile.size())};
        std::memcpy(oldProfile.data(), v1Header, sizeof(v1Header));
        RegSetValueExW(testKey, L"DisplaySettingsV1", 0, REG_BINARY, oldProfile.data(), static_cast<DWORD>(oldProfile.size()));
        RegDeleteValueW(testKey, L"DisplaySettingsV5");
        const auto migrated = LoadOverlaySettings(key.c_str());
        Check(migrated.fontPoints == 33 && migrated.hotkeyKey == VK_F9 && migrated.x == 234 && migrated.color == s.color &&
            migrated.visibleMetrics == OverlayMetric::All, "V1 migration preserves appearance/shortcut and defaults all metrics on");
        s = migrated; s.visibleMetrics = OverlayMetric::Mouse;
        Check(SaveOverlaySettings(s, key.c_str()) == ERROR_SUCCESS && LoadOverlaySettings(key.c_str()).visibleMetrics == OverlayMetric::Mouse,
            "new selections override the retained legacy profile");
        const DWORD duplicate = 13;
        auto corruptProfile = savedProfileV5;
        std::memcpy(corruptProfile.data() + corruptProfile.size() - v5TailBytes - sizeof(s.metricOrder) + sizeof(DWORD), &duplicate, sizeof(duplicate));
        RegSetValueExW(testKey, L"DisplaySettingsV5", 0, REG_BINARY, corruptProfile.data(), static_cast<DWORD>(corruptProfile.size()));
        Check(LoadOverlaySettings(key.c_str()).metricOrder == OverlayMetric::DefaultOrder(), "corrupt saved order falls back safely");
        corruptProfile = savedProfileV5;
        const DWORD invalidPump = 2;
        std::memcpy(corruptProfile.data() + corruptProfile.size() - 2 * sizeof(DWORD), &invalidPump, sizeof(invalidPump));
        RegSetValueExW(testKey, L"DisplaySettingsV5", 0, REG_BINARY, corruptProfile.data(), static_cast<DWORD>(corruptProfile.size()));
        Check(!LoadOverlaySettings(key.c_str()).pumpEnabled, "invalid persisted pump flag never enables hardware access");
        const DWORD junk = 0xffffffff;
        RegSetValueExW(testKey, L"DisplaySettingsV5", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&junk), sizeof(junk));
        Check(LoadOverlaySettings(key.c_str()).fontPoints == 33 && LoadOverlaySettings(key.c_str()).visibleMetrics == OverlayMetric::All,
            "damaged V5 profile falls back to intact legacy settings");
        RegDeleteValueW(testKey, L"DisplaySettingsV1");
        RegCloseKey(testKey);
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
    }
    Gdiplus::GdiplusShutdown(token);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
