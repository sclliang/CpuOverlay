#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include "cpu_sampler.h"
#include "overlay_renderer.h"
#include "settings_dialog.h"
#include "startup.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr wchar_t kControllerClass[] = L"sixunited.CpuOverlay.Controller.1";
constexpr wchar_t kOverlayClass[] = L"sixunited.CpuOverlay.Overlay.1";
constexpr wchar_t kSettingsKey[] = L"Software\\sixunited\\CpuOverlay";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr UINT kTray = WM_APP + 1, kSample = WM_APP + 2;
constexpr UINT kShow = WM_APP + 3, kHide = WM_APP + 4, kToggle = WM_APP + 5, kMenu = WM_APP + 6;
constexpr UINT kSettings = WM_APP + 7, kTrayId = 1;
enum Command : UINT { Toggle = 100, Startup, About, Exit, RetryHotkey, Settings, Corner = 200, Size = 300, Layout = 400, Monitor = 1000 };

std::wstring ReadString(const wchar_t* key, const wchar_t* name) {
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes > 131072)
        return {};
    std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) != ERROR_SUCCESS)
        return {};
    return value.data();
}

DWORD ReadDword(const wchar_t* name, DWORD fallback) {
    DWORD value = fallback, bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS
        ? value : fallback;
}

LSTATUS WriteValue(const wchar_t* keyName, const wchar_t* name, DWORD type, const void* data, DWORD bytes) {
    HKEY key = nullptr;
    LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, keyName, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result == ERROR_SUCCESS) {
        result = RegSetValueExW(key, name, 0, type, static_cast<const BYTE*>(data), bytes);
        RegCloseKey(key);
    }
    return result;
}
void WriteDword(const wchar_t* name, DWORD value) { WriteValue(kSettingsKey, name, REG_DWORD, &value, sizeof(value)); }
bool StartupEnabled() { return LogonTaskEnabled() || !ReadString(kRunKey, L"CpuOverlay").empty(); }

LSTATUS SetStartup(bool enabled) {
    const LSTATUS result = static_cast<LSTATUS>(ConfigureLogonTask(enabled));
    if (result != ERROR_SUCCESS) return result;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, L"CpuOverlay");
        RegCloseKey(key);
    }
    if (result == ERROR_SUCCESS) WriteDword(L"Initialized", 1);
    return result;
}

void ShowError(const wchar_t* action, DWORD error) {
    wchar_t* systemText = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&systemText), 0, nullptr);
    const std::wstring message = std::wstring(action) + L"\n\n" + (systemText ? systemText : L"操作失败") +
        L"\n错误代码：" + std::to_wstring(error);
    if (systemText) LocalFree(systemText);
    MessageBoxW(nullptr, message.c_str(), L"CpuOverlay", MB_OK | MB_ICONERROR);
}

int WriteDiagnostics(const wchar_t* path) {
    std::ofstream file{std::filesystem::path(path)};
    if (!file) return 2;
    CpuSampler sampler;
    sampler.Prime();
    file << "{\n  \"version\": \"1.4.0.0\",\n  \"elevated\": " << (IsElevated() ? "true" : "false")
         << ",\n  \"sample_interval_ms\": 1000,\n  \"samples\": [\n";
    bool valid = true;
    for (int i = 0; i < 4; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto sample = sampler.Collect();
        if (i) file << ",\n";
        file << "    {\"cpu_percent\": ";
        if (sample.usagePercent) file << std::fixed << std::setprecision(3) << *sample.usagePercent;
        else { file << "null"; valid = false; }
        file << ", \"frequency_ghz\": ";
        if (sample.frequencyGhz) file << *sample.frequencyGhz;
        else { file << "null"; valid = false; }
        auto number = [&file, &valid](const char* key, const std::optional<double>& value) {
            file << ", \"" << key << "\": ";
            if (value) file << *value;
            else { file << "null"; valid = false; }
        };
        number("cpu_temperature_c", sample.hardware.temperatureC);
        number("cpu_package_watts", sample.hardware.packageWatts);
        file << ", \"energy_meter_milliwatts\": ";
        if (sample.energyMeterMilliwatts) file << *sample.energyMeterMilliwatts; else file << "null";
        number("memory_percent", sample.memoryPercent);
        number("gpu_percent", sample.gpuPercent);
        file << ", \"memory_used_bytes\": " << sample.memoryUsedBytes << ", \"memory_total_bytes\": " << sample.memoryTotalBytes
             << ", \"gpu_engine_count\": " << sample.gpuEngineCount
             << ", \"usage_source\": \"" << sample.usageSource << "\", \"frequency_source\": \"" << sample.frequencySource
             << "\", \"cpu_sensor_source\": \"" << sample.hardware.sensorSource
             << "\", \"cpu_vendor\": \"" << sample.hardware.cpuVendor << "\", \"cpu_family\": " << sample.hardware.cpuFamily
             << ", \"cpu_model\": " << sample.hardware.cpuModel
             << ", \"temperature_source\": \"" << sample.hardware.temperatureSource << "\", \"power_source\": \"" << sample.hardware.powerSource
             << "\", \"memory_source\": \"GlobalMemoryStatusEx\", \"gpu_source\": \"windows_gpu_engine\""
             << ", \"pawnio_status\": \"" << sample.hardware.status << "\", \"pawnio_hresult\": \"0x" << std::hex
             // Module origin helps verify both portable and logon startup contexts.
             << static_cast<DWORD>(sample.hardware.error) << std::dec << "\", \"pawnio_module_source\": \"" << sample.hardware.moduleSource
             << "\", \"thermal_msr_raw\": " << sample.hardware.thermalRaw
             << ", \"thermal_smn_raw\": " << sample.hardware.thermalSmnRaw << ", \"amd_tctl_offset_c\": " << sample.hardware.amdTctlOffsetC
             << ", \"thermal_legacy_raw\": " << sample.hardware.thermalLegacyRaw << ", \"thermal_core_index\": " << sample.hardware.thermalCoreIndex
             << ", \"tj_max_c\": " << sample.hardware.tjMaxC << ", \"energy_counter_raw\": " << sample.hardware.energyRaw
             << ", \"energy_unit_raw\": " << sample.hardware.energyUnitRaw
             << std::setprecision(15) << ", \"energy_unit_j\": " << sample.hardware.energyUnitJoules
             << ", \"power_interval_s\": " << sample.hardware.intervalSeconds << "}";
    }
    file << "\n  ]\n}\n";
    return file.good() ? (valid ? 0 : 3) : 2;
}

class Application {
public:
    explicit Application(HINSTANCE instance) : instance_(instance) {}
    ~Application() {
        StopWorker();
        shortcut_.reset();
        if (controller_) { DeleteTray(); DestroyWindow(controller_); }
        if (overlay_) DestroyWindow(overlay_);
        if (icon_) DestroyIcon(icon_);
    }

    int Run(bool show, bool noAutostart, bool openSettings) {
        settings_ = LoadOverlaySettings();
        WNDCLASSEXW controllerClass{sizeof(WNDCLASSEXW)};
        controllerClass.lpfnWndProc = ControllerProc;
        controllerClass.hInstance = instance_;
        controllerClass.lpszClassName = kControllerClass;
        if (!RegisterClassExW(&controllerClass)) { ShowError(L"无法创建程序窗口类。", GetLastError()); return 1; }
        WNDCLASSEXW overlayClass{sizeof(WNDCLASSEXW)};
        overlayClass.lpfnWndProc = OverlayProc;
        overlayClass.hInstance = instance_;
        overlayClass.lpszClassName = kOverlayClass;
        if (!RegisterClassExW(&overlayClass)) { ShowError(L"无法创建悬浮窗口类。", GetLastError()); return 1; }
        controller_ = CreateWindowExW(WS_EX_TOOLWINDOW, kControllerClass, L"CpuOverlay Controller", WS_OVERLAPPED,
            0, 0, 0, 0, nullptr, nullptr, instance_, this);
        if (!controller_) { ShowError(L"无法创建程序窗口。", GetLastError()); return 1; }
        shortcut_ = std::make_unique<OverlayShortcut>(controller_);
        // Permit only harmless display/exit requests from a second, unelevated launch.
        for (UINT message : {kShow, kHide, kToggle, kMenu, kSettings, static_cast<UINT>(WM_CLOSE)})
            ChangeWindowMessageFilterEx(controller_, message, MSGFLT_ALLOW, nullptr);
        overlay_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kOverlayClass, L"CpuOverlay", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance_, this);
        if (!overlay_) { ShowError(L"无法创建悬浮窗口。", GetLastError()); return 1; }
        taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
        icon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        AddTray();
        SetTimer(controller_, 1, 10000, nullptr);
        RegisterShortcut();
        bool firstRun = ReadDword(L"Initialized", 0) == 0;
        if (!noAutostart && (firstRun || StartupEnabled())) {
            const auto status = SetStartup(true);
            if (status != ERROR_SUCCESS) ShowError(L"开机自启设置失败，程序仍可正常使用。", status);
        }
        try { worker_ = std::thread(&Application::Worker, this); }
        catch (...) { MessageBoxW(nullptr, L"无法启动 CPU 采样线程。", L"CpuOverlay", MB_OK | MB_ICONERROR); return 1; }
        if (show) SetVisible(true);
        if (firstRun && !noAutostart)
            Notify(L"CpuOverlay 已启动", (L"按 " + ShortcutText(settings_) + L" 显示 / 隐藏 CPU 监控。\n右键托盘图标进入设置，可调整快捷键和显示外观。").c_str());
        if (openSettings) PostMessageW(controller_, kSettings, 0, 0);
        MSG message{};
        int result;
        while ((result = static_cast<int>(GetMessageW(&message, nullptr, 0, 0))) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        StopWorker();
        return result < 0 ? 1 : 0;
    }

private:
    void StopWorker() {
        { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; }
        condition_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    void Worker() {
        // This is the only thread that touches PDH. No potentially slow counter query runs on the UI thread.
        try {
            CpuSampler sampler;
            std::unique_lock<std::mutex> lock(mutex_);
            while (!stop_) {
                condition_.wait(lock, [this] { return stop_ || sampling_; });
                if (stop_) break;
                const auto generation = generation_;
                lock.unlock();
                sampler.Prime();
                lock.lock();
                while (!stop_ && sampling_ && generation == generation_) {
                    if (condition_.wait_for(lock, std::chrono::seconds(1), [this, generation] {
                        return stop_ || !sampling_ || generation_ != generation;
                    })) break;
                    lock.unlock();
                    const auto sample = sampler.Collect();
                    lock.lock();
                    if (!stop_ && sampling_ && generation == generation_) {
                        latest_ = sample;
                        PostMessageW(controller_, kSample, 0, 0);
                    }
                }
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            latest_ = {};
            PostMessageW(controller_, kSample, 0, 0);
        }
    }

    CpuSample Latest() { std::lock_guard<std::mutex> lock(mutex_); return latest_; }

    void SetSampling(bool active) {
        { std::lock_guard<std::mutex> lock(mutex_); sampling_ = active; ++generation_; latest_ = {}; }
        condition_.notify_all();
    }

    void SetVisible(bool visible) {
        if (visible_ == visible) return;
        visible_ = visible;
        SetSampling(visible);
        if (visible) {
            if (!Render()) {
                visible_ = false;
                SetSampling(false);
                Notify(L"悬浮窗显示失败", L"无法更新透明窗口，请退出后重新启动程序。");
                return;
            }
            SetWindowPos(overlay_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        } else ShowWindow(overlay_, SW_HIDE);
        UpdateTooltip();
    }

    bool Render() {
        if (dragging_) return true;
        const auto& settings = preview_ ? *preview_ : settings_;
        const auto monitors = ListMonitors();
        if (monitors.empty()) return false;
        auto chosen = std::find_if(monitors.begin(), monitors.end(), [&settings](const auto& m) { return settings.monitor == m.info.szDevice; });
        if (chosen == monitors.end()) chosen = std::find_if(monitors.begin(), monitors.end(), [](const auto& m) {
            return (m.info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        });
        if (chosen == monitors.end()) chosen = monitors.begin();
        UINT dpiX = 96, dpiY = 96;
        if (FAILED(GetDpiForMonitor(chosen->handle, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) dpiX = 96;
        const float scale = static_cast<float>(dpiX) / 96.0f;
        const auto& work = chosen->info.rcWork;
        const int margin = settings.position == 5 ? 0 : MulDiv(settings.margin, static_cast<int>(dpiX), 96);
        const auto sample = Latest();
        // Expose the same readings to accessibility and desktop integration checks.
        std::wstring title = L"CpuOverlay — " + FormatOverlayText(sample);
        if (!sample.hardware.temperatureC || !sample.hardware.packageWatts) {
            const std::string status(sample.hardware.status);
            title += L" — PawnIO: " + std::wstring(status.begin(), status.end()) +
                L" (" + std::to_wstring(static_cast<DWORD>(sample.hardware.error)) + L")";
        }
        SetWindowTextW(overlay_, title.c_str());
        auto bitmap = RenderOverlay(sample, scale, std::max(1L, work.right - work.left - 2 * margin),
            std::max(1L, work.bottom - work.top - 2 * margin), settings, positioning_);
        if (!bitmap) return false;
        const int width = static_cast<int>(bitmap->GetWidth()), height = static_cast<int>(bitmap->GetHeight());
        const POINT position = OverlayPosition(settings, work, {width, height}, dpiX);
        // Moving first ensures WM_DPICHANGED is delivered; rendering always uses the selected monitor's DPI.
        SetWindowPos(overlay_, HWND_TOPMOST, position.x, position.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        return PresentOverlay(overlay_, *bitmap, position);
    }

    void AddTray() {
        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = controller_;
        data.uID = kTrayId;
        data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = kTray;
        data.hIcon = icon_ ? icon_ : LoadIconW(nullptr, IDI_APPLICATION);
        wcsncpy_s(data.szTip, (L"CpuOverlay — " + ShortcutText(settings_) + L" 显示 / 隐藏").c_str(), _TRUNCATE);
        trayAdded_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
        if (trayAdded_) { data.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &data); }
    }
    void DeleteTray() {
        if (!trayAdded_) return;
        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = controller_; data.uID = kTrayId;
        Shell_NotifyIconW(NIM_DELETE, &data);
        trayAdded_ = false;
    }
    void UpdateTooltip() {
        if (!trayAdded_) return;
        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = controller_; data.uID = kTrayId; data.uFlags = NIF_TIP;
        const auto sample = Latest();
        if (visible_ && sample.usagePercent && sample.frequencyGhz)
            wcsncpy_s(data.szTip, (FormatOverlayText(sample) + L"\n" + ShortcutText(settings_) + L" 显示 / 隐藏").c_str(), _TRUNCATE);
        else wcsncpy_s(data.szTip, (std::wstring(visible_ ? L"CpuOverlay — 正在采样 | " : L"CpuOverlay — 已隐藏 | ") + ShortcutText(settings_) + L" 显示 / 隐藏").c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }
    void Notify(const wchar_t* title, const wchar_t* text) {
        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = controller_; data.uID = kTrayId; data.uFlags = NIF_INFO;
        data.dwInfoFlags = NIIF_INFO;
        wcsncpy_s(data.szInfoTitle, title, _TRUNCATE);
        wcsncpy_s(data.szInfo, text, _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }
    void RegisterShortcut() {
        DWORD error = 0;
        if (shortcut_->Prepare(settings_.hotkeyModifiers, settings_.hotkeyKey, error)) shortcut_->Commit();
        else MessageBoxW(nullptr, (ShortcutText(settings_) + L" 注册失败，可能已被其他程序占用。\n\n可通过托盘菜单显示 / 隐藏，并在“设置”中选择其他快捷键。").c_str(),
            L"CpuOverlay — 快捷键不可用", MB_OK | MB_ICONWARNING);
    }

    bool ApplySettings(const OverlaySettings& candidate, std::wstring& error) {
        if (!ValidateOverlaySettings(candidate, error)) return false;
        DWORD status = 0;
        if (!shortcut_->Prepare(candidate.hotkeyModifiers, candidate.hotkeyKey, status)) {
            error = ShortcutText(candidate) + L" 无法注册，可能已被其他程序占用。\n原快捷键和设置已保留，请更换组合键。"; return false;
        }
        status = SaveOverlaySettings(candidate);
        if (status != ERROR_SUCCESS) {
            shortcut_->Cancel(); error = L"保存配置失败，原设置已保留。错误代码：" + std::to_wstring(status); return false;
        }
        shortcut_->Commit(); settings_ = candidate; preview_.reset(); StopPositioning();
        if (visible_) Render(); UpdateTooltip();
        return true;
    }

    void StopPositioning() {
        positioning_ = dragging_ = false;
        SetWindowLongPtrW(overlay_, GWL_EXSTYLE, GetWindowLongPtrW(overlay_, GWL_EXSTYLE) | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
        SetWindowPos(overlay_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    void OpenSettings() {
        if (settingsWindow_) { SetForegroundWindow(settingsWindow_); return; }
        const bool wasVisible = visible_;
        SettingsCallbacks callbacks;
        callbacks.opened = [this](HWND window) { settingsWindow_ = window; };
        callbacks.apply = [this](const OverlaySettings& candidate, std::wstring& error) { return ApplySettings(candidate, error); };
        callbacks.preview = [this](const OverlaySettings& candidate) {
            StopPositioning(); preview_ = candidate; SetVisible(true); Render();
        };
        callbacks.pickPosition = [this](HWND, const OverlaySettings& candidate) {
            preview_ = candidate; positioning_ = true;
            SetWindowLongPtrW(overlay_, GWL_EXSTYLE, GetWindowLongPtrW(overlay_, GWL_EXSTYLE) & ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE));
            SetVisible(true); Render();
        };
        ShowSettingsDialog(controller_, instance_, settings_, callbacks);
        settingsWindow_ = nullptr;
        StopPositioning(); preview_.reset();
        SetVisible(wasVisible); if (visible_) Render();
    }

    void PositionPicked() {
        dragging_ = false;
        if (!positioning_ || !preview_) return;
        RECT rectangle{}; GetWindowRect(overlay_, &rectangle);
        const auto monitor = MonitorFromWindow(overlay_, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) {
            UINT dpi = 96, unused = 96;
            if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi, &unused))) dpi = 96;
            preview_->position = 5; preview_->monitor = info.szDevice;
            preview_->x = std::clamp(MulDiv(rectangle.left - info.rcWork.left, 96, static_cast<int>(dpi)), 0, 32767);
            preview_->y = std::clamp(MulDiv(rectangle.top - info.rcWork.top, 96, static_cast<int>(dpi)), 0, 32767);
            if (settingsWindow_) SendMessageW(settingsWindow_, kPositionPicked, 0, reinterpret_cast<LPARAM>(&*preview_));
        }
        StopPositioning(); Render();
    }

    void ShowMenu() {
        HMENU menu = CreatePopupMenu(), positions = CreatePopupMenu(), sizes = CreatePopupMenu(), displays = CreatePopupMenu(), layouts = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, Toggle, (std::wstring(visible_ ? L"隐藏监控\t" : L"显示监控\t") + ShortcutText(settings_)).c_str());
        AppendMenuW(menu, MF_STRING, Settings, L"设置…（快捷键 / 外观 / 自定义位置）");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        const wchar_t* corners[] = {L"右上角", L"左上角", L"右下角", L"左下角", L"顶部居中"};
        for (UINT i = 0; i < 5; ++i) AppendMenuW(positions, MF_STRING | (settings_.position == i ? MF_CHECKED : 0), Corner + i, corners[i]);
        AppendMenuW(positions, MF_STRING | (settings_.position == 5 ? MF_CHECKED : 0), Settings, L"自定义位置…");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(positions), L"显示位置");
        const wchar_t* sizeNames[] = {L"小", L"标准", L"大"};
        const int points[]{14, 18, 23};
        for (UINT i = 0; i < 3; ++i) AppendMenuW(sizes, MF_STRING | (settings_.fontPoints == points[i] ? MF_CHECKED : 0), Size + i, sizeNames[i]);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sizes), L"文字大小");
        AppendMenuW(layouts, MF_STRING | (!settings_.vertical ? MF_CHECKED : 0), Layout, L"横向显示");
        AppendMenuW(layouts, MF_STRING | (settings_.vertical ? MF_CHECKED : 0), Layout + 1, L"纵向显示");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(layouts), L"排列方向");
        const auto monitors = ListMonitors();
        AppendMenuW(displays, MF_STRING | (settings_.monitor.empty() ? MF_CHECKED : 0), Monitor, L"跟随主显示器");
        for (UINT i = 0; i < monitors.size(); ++i) {
            const auto& info = monitors[i].info;
            const std::wstring label = std::wstring(info.szDevice) + L"  (" + std::to_wstring(info.rcMonitor.right - info.rcMonitor.left) +
                L" × " + std::to_wstring(info.rcMonitor.bottom - info.rcMonitor.top) + L")";
            AppendMenuW(displays, MF_STRING | (settings_.monitor == info.szDevice ? MF_CHECKED : 0), Monitor + 1 + i, label.c_str());
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(displays), L"显示器");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (StartupEnabled() ? MF_CHECKED : 0), Startup, L"开机自启（当前用户登录后）");
        if (!shortcut_->Registered()) AppendMenuW(menu, MF_STRING, RetryHotkey, L"重新注册快捷键");
        AppendMenuW(menu, MF_STRING, About, L"关于 / 使用说明");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, Exit, L"退出");
        POINT cursor{};
        GetCursorPos(&cursor);
        SetForegroundWindow(controller_);
        const UINT selected = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, controller_, nullptr);
        PostMessageW(controller_, WM_NULL, 0, 0);
        DestroyMenu(menu);
        if (selected == Toggle) SetVisible(!visible_);
        else if (selected == Settings) OpenSettings();
        else if (selected == Exit) PostMessageW(controller_, WM_CLOSE, 0, 0);
        else if (selected == Startup) {
            const auto status = SetStartup(!StartupEnabled());
            if (status != ERROR_SUCCESS) ShowError(L"无法修改开机自启设置。", status);
        } else if (selected == RetryHotkey) RegisterShortcut();
        else if (selected == About) {
            const std::wstring help = L"CpuOverlay 1.4.0.0\n\n" + ShortcutText(settings_) +
                L"：显示 / 隐藏；单击托盘图标也可切换。\n"
                L"右键托盘 → 设置：快捷键、横/纵排列、自定义坐标及拖动定位。\n"
                L"可调整字体、字号、粗细、颜色、不透明度、间距和描边。\n"
                L"背景透明、鼠标穿透、置顶，不抢键盘焦点。\n"
                L"每秒更新；隐藏后暂停采样。\n\n"
                L"处理器：所有逻辑处理器的平均忙碌时间占比。\n"
                L"频率：Windows 性能计数器估算频率。\n"
                L"温度 / 功耗：Intel / AMD，对应硬件未提供读数时显示 --。\n"
                L"内存：物理内存使用百分比。\n"
                L"显卡：Windows GPU 引擎统计中最繁忙的引擎占用。\n"
                L"各核心频率可能不同，读数可能与任务管理器不同。\n"
                L"普通置顶窗口不能保证覆盖独占全屏游戏或安全桌面。\n\n"
                L"硬件传感器需要已安装 PawnIO 及管理员权限。\n"
                L"登录自启使用当前用户的最高权限计划任务；可在托盘中取消。\n"
                L"删除程序前请先取消自启并退出。";
            MessageBoxW(nullptr, help.c_str(), L"关于 CpuOverlay", MB_OK | MB_ICONINFORMATION);
        } else {
            auto candidate = settings_;
            bool changed = true;
            if (selected >= Corner && selected < Corner + 5) candidate.position = selected - Corner;
            else if (selected >= Size && selected < Size + 3) candidate.fontPoints = points[selected - Size];
            else if (selected == Layout || selected == Layout + 1) candidate.vertical = selected - Layout;
            else if (selected >= Monitor && selected <= Monitor + monitors.size())
                candidate.monitor = selected == Monitor ? L"" : monitors[selected - Monitor - 1].info.szDevice;
            else changed = false;
            std::wstring error;
            if (changed && !ApplySettings(candidate, error)) MessageBoxW(controller_, error.c_str(), L"设置未保存", MB_OK | MB_ICONWARNING);
        }
    }

    static LRESULT CALLBACK ControllerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            app = static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (!app) return DefWindowProcW(window, message, wParam, lParam);
        if (app->taskbarCreated_ && message == app->taskbarCreated_) { app->trayAdded_ = false; app->AddTray(); return 0; }
        switch (message) {
        case WM_HOTKEY:
            if (app->shortcut_ && app->shortcut_->Matches(wParam) &&
                !(app->settingsWindow_ && CaptureRegisteredShortcut(app->settingsWindow_, app->settings_))) app->SetVisible(!app->visible_);
            return 0;
        case kToggle: app->SetVisible(!app->visible_); return 0;
        case kShow: app->SetVisible(true); return 0;
        case kHide: app->SetVisible(false); return 0;
        case kMenu: app->ShowMenu(); return 0;
        case kSettings: app->OpenSettings(); return 0;
        case kSample: if (app->visible_) { app->Render(); app->UpdateTooltip(); } return 0;
        case kTray:
            if (LOWORD(lParam) == WM_CONTEXTMENU) app->ShowMenu();
            else if (LOWORD(lParam) == NIN_SELECT || LOWORD(lParam) == NIN_KEYSELECT) app->SetVisible(!app->visible_);
            return 0;
        case WM_TIMER: if (!app->trayAdded_) app->AddTray(); return 0;
        case WM_DISPLAYCHANGE: case WM_SETTINGCHANGE:
            if (app->visible_) PostMessageW(window, kSample, 0, 0);
            return 0;
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMSUSPEND) app->SetSampling(false);
            else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) app->SetSampling(app->visible_);
            return TRUE;
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION: if (wParam) PostQuitMessage(0); return 0;
        case WM_CLOSE:
            if (app->settingsWindow_) SendMessageW(app->settingsWindow_, WM_CLOSE, 0, 0);
            app->SetVisible(false); app->DeleteTray(); PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    static LRESULT CALLBACK OverlayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE)
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        auto app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_MOUSEACTIVATE) return app && app->positioning_ ? MA_ACTIVATE : MA_NOACTIVATE;
        if (message == WM_NCHITTEST) return app && app->positioning_ ? HTCAPTION : HTTRANSPARENT;
        if (message == WM_ENTERSIZEMOVE && app) { app->dragging_ = true; return 0; }
        if (message == WM_EXITSIZEMOVE && app) { app->PositionPicked(); return 0; }
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_DPICHANGED) {
            if (app && app->controller_) PostMessageW(app->controller_, kSample, 0, 0);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    HINSTANCE instance_;
    HWND controller_ = nullptr, overlay_ = nullptr, settingsWindow_ = nullptr;
    HICON icon_ = nullptr;
    UINT taskbarCreated_ = 0;
    bool visible_ = false, trayAdded_ = false, positioning_ = false, dragging_ = false;
    OverlaySettings settings_;
    std::optional<OverlaySettings> preview_;
    std::unique_ptr<OverlayShortcut> shortcut_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool stop_ = false, sampling_ = false;
    unsigned long long generation_ = 0;
    CpuSample latest_;
};
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    LocalFree(argv);
    auto has = [&args](const wchar_t* option) { return std::find(args.begin(), args.end(), option) != args.end(); };
    auto output = [&args](const wchar_t* option) -> std::wstring {
        const auto it = std::find(args.begin(), args.end(), option);
        return it != args.end() && it + 1 != args.end() ? *(it + 1) : std::wstring();
    };
    if (has(L"--diagnostics")) { const auto path = output(L"--diagnostics"); return path.empty() ? 2 : WriteDiagnostics(path.c_str()); }
    ULONG_PTR gdiplus = 0;
    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&gdiplus, &gdiplusInput, nullptr) != Gdiplus::Ok) return 1;
    if (has(L"--preview")) {
        const auto path = output(L"--preview");
        const bool saved = !path.empty() && SavePreview(path.c_str());
        Gdiplus::GdiplusShutdown(gdiplus);
        return saved ? 0 : 2;
    }
    if (!has(L"--enable-startup") && !has(L"--disable-startup")) {
        const HWND existing = FindWindowW(kControllerClass, nullptr);
        if (existing) {
            UINT request = kShow;
            if (has(L"--exit")) request = WM_CLOSE;
            else if (has(L"--hide")) request = kHide;
            else if (has(L"--toggle")) request = kToggle;
            else if (has(L"--menu")) { AllowSetForegroundWindow(ASFW_ANY); request = kMenu; }
            else if (has(L"--settings")) { AllowSetForegroundWindow(ASFW_ANY); request = kSettings; }
            const bool sent = has(L"--startup") || PostMessageW(existing, request, 0, 0);
            Gdiplus::GdiplusShutdown(gdiplus);
            return sent ? 0 : 1;
        }
        if (has(L"--exit")) { Gdiplus::GdiplusShutdown(gdiplus); return 0; }
    }
    if (!IsElevated()) {
        const auto error = RelaunchElevated(commandLine);
        if (error != ERROR_SUCCESS && error != ERROR_CANCELLED) ShowError(L"无法获得 PawnIO 所需的管理员权限。", error);
        Gdiplus::GdiplusShutdown(gdiplus);
        return error == ERROR_SUCCESS ? 0 : 1;
    }
    if (has(L"--enable-startup") || has(L"--disable-startup")) {
        const auto result = SetStartup(has(L"--enable-startup"));
        if (result != ERROR_SUCCESS) ShowError(L"无法修改登录自启任务。", result);
        Gdiplus::GdiplusShutdown(gdiplus);
        return result == ERROR_SUCCESS ? 0 : 1;
    }
    HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\sixunited.CpuOverlay.Instance.1");
    const DWORD mutexStatus = GetLastError();
    int result = 0;
    if (!instanceMutex) { ShowError(L"无法建立程序单实例锁。", mutexStatus); result = 1; }
    else if (mutexStatus == ERROR_ALREADY_EXISTS) {
        HWND controller = nullptr;
        for (int i = 0; i < 20 && !controller; ++i) {
            controller = FindWindowW(kControllerClass, nullptr);
            if (!controller) Sleep(100);
        }
        if (controller) {
            if (has(L"--exit")) PostMessageW(controller, WM_CLOSE, 0, 0);
            else if (has(L"--hide")) PostMessageW(controller, kHide, 0, 0);
            else if (has(L"--toggle")) PostMessageW(controller, kToggle, 0, 0);
            else if (has(L"--menu")) { AllowSetForegroundWindow(ASFW_ANY); PostMessageW(controller, kMenu, 0, 0); }
            else if (has(L"--settings")) { AllowSetForegroundWindow(ASFW_ANY); PostMessageW(controller, kSettings, 0, 0); }
            else if (!has(L"--startup")) PostMessageW(controller, kShow, 0, 0);
        } else result = 1;
    } else if (!has(L"--exit")) {
        Application application(instance);
        result = application.Run(has(L"--show") || has(L"--toggle"), has(L"--no-autostart"), has(L"--settings"));
    }
    if (instanceMutex) CloseHandle(instanceMutex);
    Gdiplus::GdiplusShutdown(gdiplus);
    return result;
}
