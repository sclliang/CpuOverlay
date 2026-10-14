#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include "cpu_sampler.h"
#include "overlay_renderer.h"
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
constexpr UINT kTrayId = 1, kHotkeyId = 1;
enum Command : UINT { Toggle = 100, Startup, About, Exit, RetryHotkey, Corner = 200, Size = 300, Monitor = 1000 };

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

struct MonitorEntry { HMONITOR handle; MONITORINFOEXW info; };
std::vector<MonitorEntry> ListMonitors() {
    std::vector<MonitorEntry> result;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR handle, HDC, LPRECT, LPARAM param) -> BOOL {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(handle, &info)) reinterpret_cast<std::vector<MonitorEntry>*>(param)->push_back({handle, info});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

int WriteDiagnostics(const wchar_t* path) {
    std::ofstream file{std::filesystem::path(path)};
    if (!file) return 2;
    CpuSampler sampler;
    sampler.Prime();
    file << "{\n  \"version\": \"1.1.0.0\",\n  \"elevated\": " << (IsElevated() ? "true" : "false")
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
        number("memory_percent", sample.memoryPercent);
        number("gpu_percent", sample.gpuPercent);
        file << ", \"memory_used_bytes\": " << sample.memoryUsedBytes << ", \"memory_total_bytes\": " << sample.memoryTotalBytes
             << ", \"gpu_engine_count\": " << sample.gpuEngineCount
             << ", \"usage_source\": \"" << sample.usageSource << "\", \"frequency_source\": \"" << sample.frequencySource
             << "\", \"cpu_sensor_source\": \"pawnio_intel_msr\", \"memory_source\": \"GlobalMemoryStatusEx\", \"gpu_source\": \"windows_gpu_engine\""
             << ", \"pawnio_status\": \"" << sample.hardware.status << "\", \"pawnio_hresult\": \"0x" << std::hex
             // Module origin helps verify both portable and logon startup contexts.
             << static_cast<DWORD>(sample.hardware.error) << std::dec << "\", \"pawnio_module_source\": \"" << sample.hardware.moduleSource
             << "\", \"thermal_msr_raw\": " << sample.hardware.thermalRaw
             << ", \"tj_max_c\": " << sample.hardware.tjMaxC << ", \"energy_counter_raw\": " << sample.hardware.energyRaw
             << std::setprecision(9) << ", \"energy_unit_j\": " << sample.hardware.energyUnitJoules
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
        if (controller_) { UnregisterHotKey(controller_, kHotkeyId); DeleteTray(); DestroyWindow(controller_); }
        if (overlay_) DestroyWindow(overlay_);
        if (icon_) DestroyIcon(icon_);
    }

    int Run(bool show, bool noAutostart) {
        // Upgrade the previous four-corner layout to the requested top-center default once.
        if (ReadDword(L"PositionVersion", 0) < 2) {
            WriteDword(L"Corner", 4);
            WriteDword(L"PositionVersion", 2);
        }
        corner_ = std::min<DWORD>(ReadDword(L"Corner", 4), 4);
        size_ = std::min<DWORD>(ReadDword(L"Size", 1), 2);
        monitorName_ = ReadString(kSettingsKey, L"Monitor");
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
        // Permit only harmless display/exit requests from a second, unelevated launch.
        for (UINT message : {kShow, kHide, kToggle, kMenu, static_cast<UINT>(WM_CLOSE)})
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
            Notify(L"CpuOverlay 已启动", L"按 Alt+F1 显示 / 隐藏 CPU 监控。\n右键托盘图标可调整位置、字号和开机自启。");
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
        const auto monitors = ListMonitors();
        if (monitors.empty()) return false;
        auto chosen = std::find_if(monitors.begin(), monitors.end(), [this](const auto& m) { return monitorName_ == m.info.szDevice; });
        if (chosen == monitors.end()) chosen = std::find_if(monitors.begin(), monitors.end(), [](const auto& m) {
            return (m.info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        });
        if (chosen == monitors.end()) chosen = monitors.begin();
        UINT dpiX = 96, dpiY = 96;
        if (FAILED(GetDpiForMonitor(chosen->handle, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) dpiX = 96;
        const float sizeScale[] = {0.8f, 1.0f, 1.3f};
        const float scale = static_cast<float>(dpiX) / 96.0f * sizeScale[size_];
        const auto& work = chosen->info.rcWork;
        const int margin = MulDiv(16, static_cast<int>(dpiX), 96);
        const auto sample = Latest();
        // Expose the same readings to accessibility and desktop integration checks.
        std::wstring title = L"CpuOverlay — " + FormatOverlayText(sample);
        if (!sample.hardware.temperatureC || !sample.hardware.packageWatts) {
            const std::string status(sample.hardware.status);
            title += L" — PawnIO: " + std::wstring(status.begin(), status.end()) +
                L" (" + std::to_wstring(static_cast<DWORD>(sample.hardware.error)) + L")";
        }
        SetWindowTextW(overlay_, title.c_str());
        auto bitmap = RenderOverlay(sample, scale, std::max(1L, work.right - work.left - 2 * margin));
        if (!bitmap) return false;
        const int width = static_cast<int>(bitmap->GetWidth()), height = static_cast<int>(bitmap->GetHeight());
        const bool left = corner_ == 1 || corner_ == 3, bottom = corner_ == 2 || corner_ == 3;
        POINT position{corner_ == 4 ? work.left + (work.right - work.left - width) / 2 :
                (left ? work.left + margin : work.right - width - margin),
            bottom ? work.bottom - height - margin : work.top + margin};
        position.x = std::max(work.left, position.x);
        position.y = std::max(work.top, position.y);
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
        wcscpy_s(data.szTip, L"CpuOverlay — Alt+F1 显示 / 隐藏");
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
            wcsncpy_s(data.szTip, (FormatOverlayText(sample) + L"\nAlt+F1 显示 / 隐藏").c_str(), _TRUNCATE);
        else wcscpy_s(data.szTip, visible_ ? L"CpuOverlay — 正在采样 | Alt+F1 隐藏" : L"CpuOverlay — 已隐藏 | Alt+F1 显示");
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
        if (hotkeyRegistered_) return;
        hotkeyRegistered_ = RegisterHotKey(controller_, kHotkeyId, MOD_ALT | MOD_NOREPEAT, VK_F1) != FALSE;
        if (!hotkeyRegistered_)
            MessageBoxW(nullptr, L"Alt+F1 注册失败，可能已被其他程序占用。\n\n仍可通过托盘菜单显示 / 隐藏监控。\n释放快捷键后，在托盘菜单点击“重新注册 Alt+F1”。",
                L"CpuOverlay — 快捷键不可用", MB_OK | MB_ICONWARNING);
    }

    void ShowMenu() {
        HMENU menu = CreatePopupMenu(), positions = CreatePopupMenu(), sizes = CreatePopupMenu(), displays = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, Toggle, visible_ ? L"隐藏监控\tAlt+F1" : L"显示监控\tAlt+F1");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        const wchar_t* corners[] = {L"右上角", L"左上角", L"右下角", L"左下角", L"顶部居中"};
        for (UINT i = 0; i < 5; ++i) AppendMenuW(positions, MF_STRING | (corner_ == i ? MF_CHECKED : 0), Corner + i, corners[i]);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(positions), L"显示位置");
        const wchar_t* sizeNames[] = {L"小", L"标准", L"大"};
        for (UINT i = 0; i < 3; ++i) AppendMenuW(sizes, MF_STRING | (size_ == i ? MF_CHECKED : 0), Size + i, sizeNames[i]);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sizes), L"文字大小");
        const auto monitors = ListMonitors();
        AppendMenuW(displays, MF_STRING | (monitorName_.empty() ? MF_CHECKED : 0), Monitor, L"跟随主显示器");
        for (UINT i = 0; i < monitors.size(); ++i) {
            const auto& info = monitors[i].info;
            const std::wstring label = std::wstring(info.szDevice) + L"  (" + std::to_wstring(info.rcMonitor.right - info.rcMonitor.left) +
                L" × " + std::to_wstring(info.rcMonitor.bottom - info.rcMonitor.top) + L")";
            AppendMenuW(displays, MF_STRING | (monitorName_ == info.szDevice ? MF_CHECKED : 0), Monitor + 1 + i, label.c_str());
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(displays), L"显示器");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (StartupEnabled() ? MF_CHECKED : 0), Startup, L"开机自启（当前用户登录后）");
        if (!hotkeyRegistered_) AppendMenuW(menu, MF_STRING, RetryHotkey, L"重新注册 Alt+F1");
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
        else if (selected == Exit) PostMessageW(controller_, WM_CLOSE, 0, 0);
        else if (selected == Startup) {
            const auto status = SetStartup(!StartupEnabled());
            if (status != ERROR_SUCCESS) ShowError(L"无法修改开机自启设置。", status);
        } else if (selected == RetryHotkey) RegisterShortcut();
        else if (selected == About) {
            MessageBoxW(nullptr,
                L"CpuOverlay 1.1.0.0\n\n"
                L"Alt+F1：显示 / 隐藏；单击托盘图标也可切换。\n"
                L"右键托盘图标：位置、字号、显示器、自启与退出。\n"
                L"背景透明、鼠标穿透、置顶，不抢键盘焦点。\n"
                L"每秒更新；隐藏后暂停采样。\n\n"
                L"处理器：所有逻辑处理器的平均忙碌时间占比。\n"
                L"频率：Windows 性能计数器估算频率。\n"
                L"温度 / 功耗：PawnIO 读取 Intel CPU 温度及封装功耗。\n"
                L"内存：物理内存使用百分比。\n"
                L"显卡：Windows GPU 引擎统计中最繁忙的引擎占用。\n"
                L"各核心频率可能不同，读数可能与任务管理器不同。\n"
                L"普通置顶窗口不能保证覆盖独占全屏游戏或安全桌面。\n\n"
                L"硬件传感器需要已安装 PawnIO 及管理员权限。\n"
                L"登录自启使用当前用户的最高权限计划任务；可在托盘中取消。\n"
                L"删除程序前请先取消自启并退出。\n\n"
                L"Copyright (c) 2026 sixunited. Author: sclliang",
                L"关于 CpuOverlay", MB_OK | MB_ICONINFORMATION);
        } else if (selected >= Corner && selected < Corner + 5) {
            corner_ = selected - Corner; WriteDword(L"Corner", corner_); if (visible_) Render();
        } else if (selected >= Size && selected < Size + 3) {
            size_ = selected - Size; WriteDword(L"Size", size_); if (visible_) Render();
        } else if (selected >= Monitor && selected <= Monitor + monitors.size()) {
            monitorName_ = selected == Monitor ? L"" : monitors[selected - Monitor - 1].info.szDevice;
            WriteValue(kSettingsKey, L"Monitor", REG_SZ, monitorName_.c_str(), static_cast<DWORD>((monitorName_.size() + 1) * sizeof(wchar_t)));
            if (visible_) Render();
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
        case WM_HOTKEY: if (wParam == kHotkeyId) app->SetVisible(!app->visible_); return 0;
        case kToggle: app->SetVisible(!app->visible_); return 0;
        case kShow: app->SetVisible(true); return 0;
        case kHide: app->SetVisible(false); return 0;
        case kMenu: app->ShowMenu(); return 0;
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
        case WM_CLOSE: app->SetVisible(false); app->DeleteTray(); PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    static LRESULT CALLBACK OverlayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE)
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (message == WM_NCHITTEST) return HTTRANSPARENT;
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_DPICHANGED) {
            auto app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (app && app->controller_) PostMessageW(app->controller_, kSample, 0, 0);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    HINSTANCE instance_;
    HWND controller_ = nullptr, overlay_ = nullptr;
    HICON icon_ = nullptr;
    UINT taskbarCreated_ = 0;
    bool visible_ = false, hotkeyRegistered_ = false, trayAdded_ = false;
    DWORD corner_ = 4, size_ = 1;
    std::wstring monitorName_;
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
            else if (!has(L"--startup")) PostMessageW(controller, kShow, 0, 0);
        } else result = 1;
    } else if (!has(L"--exit")) {
        Application application(instance);
        result = application.Run(has(L"--show") || has(L"--toggle"), has(L"--no-autostart"));
    }
    if (instanceMutex) CloseHandle(instanceMutex);
    Gdiplus::GdiplusShutdown(gdiplus);
    return result;
}
