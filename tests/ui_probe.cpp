// A disposable integration-test surface. The real keyboard/mouse input comes from desktop testing.
#include <windows.h>
#include <string>
#include <fstream>
#include <filesystem>

namespace {
HWND probe = nullptr;
unsigned transitions = 0, clicks = 0, overlayClicks = 0;
bool seen = false, previousVisible = false;
std::wstring status;
std::filesystem::path reportPath;
bool clickThrough = false, styles = false, foreground = false, visible = false, topCenter = false;

void Refresh() {
    HWND overlay = FindWindowW(L"sixunited.CpuOverlay.Overlay.1", nullptr);
    visible = overlay && IsWindowVisible(overlay);
    if (seen && visible != previousVisible) ++transitions;
    if (overlay) seen = true;
    previousVisible = visible;
    const LONG_PTR extended = overlay ? GetWindowLongPtrW(overlay, GWL_EXSTYLE) : 0;
    constexpr LONG_PTR required = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    styles = (extended & required) == required;
    RECT rect{};
    if (overlay) GetWindowRect(overlay, &rect);
    wchar_t title[1024]{};
    if (overlay) GetWindowTextW(overlay, title, ARRAYSIZE(title));
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(overlay, MONITOR_DEFAULTTOPRIMARY), &monitor);
    topCenter = overlay && abs(rect.left + rect.right - monitor.rcWork.left - monitor.rcWork.right) <= 1 &&
        rect.top >= monitor.rcWork.top && rect.top - monitor.rcWork.top <= 64;
    const POINT testPoint{rect.left + 100, rect.top + 25};
    clickThrough = visible && WindowFromPoint(testPoint) == probe;
    foreground = GetForegroundWindow() == probe;
    status = L"CpuOverlay integration probe\n\n"
        L"Use Alt+F1 to hide/show the overlay.\n"
        L"Click the TOP CENTER text area to verify mouse pass-through.\n"
        L"Close this test window when finished.\n\n";
    status += L"Overlay found: " + std::wstring(overlay ? L"YES" : L"NO") + L"\n";
    status += L"Overlay visible: " + std::wstring(visible ? L"YES" : L"NO") + L"\n";
    status += std::wstring(title) + L"\n";
    status += L"Top-center placement: " + std::wstring(topCenter ? L"PASS" : L"FAIL") + L"\n";
    status += L"Layered / transparent / topmost / noactivate / toolwindow: " + std::wstring(styles ? L"PASS" : L"FAIL") + L"\n";
    status += L"Mouse target beneath overlay: " + std::wstring(clickThrough ? L"PASS" : L"(hidden or not above probe)") + L"\n";
    status += L"Keyboard focus stays in test window: " + std::wstring(foreground ? L"PASS" : L"NO") + L"\n";
    status += L"Observed visibility transitions: " + std::to_wstring(transitions) + L"\n";
    status += L"Clicks received: " + std::to_wstring(clicks) + L"\n";
    status += L"Clicks received UNDER visible overlay: " + std::to_wstring(overlayClicks) + L"\n";
    status += L"Overlay rectangle: " + std::to_wstring(rect.left) + L", " + std::to_wstring(rect.top) + L", " +
        std::to_wstring(rect.right) + L", " + std::to_wstring(rect.bottom);
    InvalidateRect(probe, nullptr, TRUE);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: SetTimer(window, 1, 250, nullptr); return 0;
    case WM_TIMER: Refresh(); return 0;
    case WM_LBUTTONDOWN: {
        ++clicks;
        POINT cursor{};
        GetCursorPos(&cursor);
        const HWND overlay = FindWindowW(L"sixunited.CpuOverlay.Overlay.1", nullptr);
        RECT bounds{};
        if (overlay && IsWindowVisible(overlay) && GetWindowRect(overlay, &bounds) && PtInRect(&bounds, cursor)) ++overlayClicks;
        Refresh(); return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT area{}; GetClientRect(window, &area);
        FillRect(dc, &area, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(20, 35, 50));
        HFONT font = CreateFontW(-24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        auto previous = SelectObject(dc, font);
        area.left += 36; area.top += 100;
        DrawTextW(dc, status.c_str(), -1, &area, DT_LEFT | DT_TOP | DT_NOPREFIX);
        SelectObject(dc, previous); DeleteObject(font); EndPaint(window, &paint); return 0;
    }
    case WM_CLOSE: {
        if (!reportPath.empty()) {
            std::ofstream report(reportPath);
            report << "{\n  \"window_styles_pass\": " << (styles ? "true" : "false")
                << ",\n  \"visibility_transitions\": " << transitions
                << ",\n  \"clicks_under_visible_overlay\": " << overlayClicks
                << ",\n  \"foreground_preserved\": " << (foreground ? "true" : "false")
                << ",\n  \"top_center_pass\": " << (topCenter ? "true" : "false") << "\n}\n";
        }
        DestroyWindow(window); return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int) {
    if (command && *command) reportPath = command;
    SetProcessDPIAware();
    WNDCLASSW wc{};
    wc.hInstance = instance; wc.lpfnWndProc = WindowProc; wc.lpszClassName = L"CpuOverlay.IntegrationProbe";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    RECT desktop{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &desktop, 0);
    probe = CreateWindowExW(0, wc.lpszClassName, L"CpuOverlay Integration Probe", WS_POPUP | WS_VISIBLE,
        desktop.left, desktop.top, desktop.right - desktop.left, desktop.bottom - desktop.top, nullptr, nullptr, instance, nullptr);
    SetForegroundWindow(probe);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
