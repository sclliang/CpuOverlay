#pragma once
#include <windows.h>
#include <array>
#include <string>
#include <vector>

namespace OverlayMetric {
constexpr DWORD CpuUsage = 1u << 0, Frequency = 1u << 1, Temperature = 1u << 2, Power = 1u << 3;
constexpr DWORD Memory = 1u << 4, Gpu = 1u << 5, Keyboard = 1u << 6, Mouse = 1u << 7;
constexpr DWORD GpuName = 1u << 8, GpuTemperature = 1u << 9, GpuFrequency = 1u << 10, GpuPower = 1u << 11;
constexpr DWORD GpuMemory = 1u << 12, Fps = 1u << 13;
constexpr DWORD LowFps = 1u << 14;
constexpr unsigned Count = 15;
constexpr size_t MaxLabelLength = 32;
constexpr DWORD All = (1u << Count) - 1;
constexpr DWORD AddedMetrics = All & ~0xffu;
constexpr std::array<DWORD, Count> DefaultOrder() {
    std::array<DWORD, Count> order{};
    for (unsigned i = 0; i < Count; ++i) order[i] = i;
    return order;
}
inline constexpr const wchar_t* Names[Count] = {L"CPU 占用", L"CPU 频率", L"CPU 温度", L"CPU 功耗",
    L"内存占用", L"显卡占用", L"键盘电量", L"鼠标电量", L"显卡型号", L"显卡温度", L"显卡频率", L"显卡功耗", L"显存用量", L"前台帧率 FPS", L"1% Low 帧率"};
inline std::array<std::wstring, Count> DefaultLabels() {
    return {L"处理器", L"频率", L"温度", L"功耗", L"内存", L"显卡", L"键盘", L"鼠标",
        L"型号", L"显卡温度", L"显卡频率", L"显卡功耗", L"显存", L"帧率", L"1% Low"};
}
bool ValidLabel(const std::wstring& label);
}

struct OverlaySettings {
    DWORD position = 4; // Existing corner IDs: TR, TL, BR, BL, top center; 5 = custom.
    DWORD vertical = 0;
    int x = 16, y = 16, margin = 16; // Logical pixels, relative to monitor work area.
    int fontPoints = 18;
    int bold = 1, italic = 0;
    COLORREF color = RGB(255, 255, 0), outlineColor = RGB(15, 23, 35);
    int outlineWidth = 2, opacity = 100, gap = 8;
    int valueGap = 0; // Extra logical pixels between the label (including ':') and its value.
    bool pumpEnabled = false;
    bool gameOverlayEnabled = true;
    DWORD gameOverlayBackends = 15; // D3D11, D3D12, OpenGL, Vulkan.
    std::vector<std::wstring> gameOverlayExcludedApps;
    int refreshIntervalMs = 1000;
    UINT hotkeyModifiers = MOD_ALT, hotkeyKey = VK_F1;
    DWORD visibleMetrics = OverlayMetric::All;
    std::array<DWORD, OverlayMetric::Count> metricOrder = OverlayMetric::DefaultOrder();
    std::array<std::wstring, OverlayMetric::Count> metricLabels = OverlayMetric::DefaultLabels();
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
