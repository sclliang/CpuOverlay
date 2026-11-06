// Manual integration probe: run against a packaged instance with its floating
// panel visible and Always on top enabled. The probe only moves its own window.
#include <windows.h>
#include "overlay_settings.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
struct Target { DWORD processId; HWND window{}; unsigned count{}; };

BOOL CALLBACK FindPanel(HWND window, LPARAM parameter) {
    auto& target = *reinterpret_cast<Target*>(parameter);
    DWORD processId{};
    GetWindowThreadProcessId(window, &processId);
    wchar_t className[128]{};
    GetClassNameW(window, className, 128);
    if (processId == target.processId && IsWindowVisible(window) &&
        std::wstring_view(className) == L"sixunited.CpuOverlay.Overlay.1" &&
        (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) == 0) {
        target.window = window;
        ++target.count;
    }
    return TRUE;
}

bool Above(HWND upper, HWND lower) {
    for (HWND window = GetWindow(lower, GW_HWNDPREV); window != nullptr;
        window = GetWindow(window, GW_HWNDPREV)) {
        if (window == upper) return true;
    }
    return false;
}

void PumpFor(DWORD milliseconds) {
    const auto until = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    } while (GetTickCount64() < until);
}
} // namespace

int main(int argc, char** argv) {
    const bool checkBar = argc == 3 && std::string_view(argv[2]) == "--bar";
    const bool checkState = argc == 3 && std::string_view(argv[2]) == "--state";
    if (argc != 2 && !checkBar && !checkState) {
        std::cerr << "Usage: CpuOverlayTopmostProbe <packaged CpuOverlay PID> [--bar|--state]\n";
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Target target{static_cast<DWORD>(std::strtoul(argv[1], nullptr, 10))};
    if (checkState) {
        const auto controller = FindWindowW(L"sixunited.CpuOverlay.Controller.1", nullptr);
        DWORD process{}; GetWindowThreadProcessId(controller, &process);
        if (!controller || process != target.processId) return 2;
        wchar_t state[2048]{}; GetWindowTextW(controller, state, 2048);
        std::wcout << state << L'\n';
        return 0;
    }
    EnumWindows(FindPanel, reinterpret_cast<LPARAM>(&target));
    if (target.count != 1 ||
        (GetWindowLongPtrW(target.window, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0) {
        std::cerr << "Expected exactly one visible topmost floating panel.\n";
        return 2;
    }
    RECT panel{};
    GetWindowRect(target.window, &panel);
    const auto settings = LoadOverlaySettings();
    if (checkBar) {
        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoW(MonitorFromWindow(target.window, MONITOR_DEFAULTTONEAREST),
                &monitor) || panel.left != monitor.rcMonitor.left ||
            panel.top != monitor.rcMonitor.top || panel.right != monitor.rcMonitor.right ||
            panel.bottom <= panel.top || panel.bottom > monitor.rcMonitor.bottom) {
            std::cerr << "Bar does not span the screen's top edge.\n";
            return 1;
        }
        std::cout << "Bar geometry: origin=(" << panel.left << ',' << panel.top
            << ") size=" << panel.right - panel.left << 'x'
            << panel.bottom - panel.top << " full_screen_width=1\n";
    }
    const HWND foreground = GetForegroundWindow();
    DWORD foregroundPid{};
    GetWindowThreadProcessId(foreground, &foregroundPid);
    if (foregroundPid == target.processId) {
        std::cout << "SKIP: the application's own UI has foreground focus; no occlusion was attempted.\n";
        return 77;
    }
    const HWND cover = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        L"STATIC", L"CpuOverlay topmost regression fixture", WS_POPUP | SS_CENTER,
        panel.left, panel.top, panel.right - panel.left, panel.bottom - panel.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (cover == nullptr) return 2;

    bool passed = true;
    for (int iteration = 0; iteration < 3; ++iteration) {
        SetWindowPos(cover, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        if (!Above(cover, target.window)) {
            std::cerr << "Could not establish occlusion.\n";
            passed = false;
            break;
        }
        PumpFor(1200);
        RECT after{};
        GetWindowRect(target.window, &after);
        const bool recovered = Above(target.window, cover);
        const bool noActivation = GetForegroundWindow() == foreground;
        MONITORINFO display{sizeof(display)};
        GetMonitorInfoW(MonitorFromWindow(target.window, MONITOR_DEFAULTTONEAREST), &display);
        const auto expected = OverlayPosition(settings, display.rcWork,
            {after.right - after.left, after.bottom - after.top}, GetDpiForWindow(target.window));
        // Live values change text width. Verify the configured anchor rather
        // than requiring a fixed-size panel like the original WinMonitor test.
        const bool noMovement = after.left == expected.x && after.top == expected.y;
        const auto styles = GetWindowLongPtrW(target.window, GWL_EXSTYLE);
        const bool passthrough = (styles & (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED)) ==
            (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED);
        std::cout << "Cycle " << iteration + 1 << ": recovered=" << recovered
            << " foreground_preserved=" << noActivation
            << " placement_preserved=" << noMovement << " click_through_styles=" << passthrough << '\n';
        passed = passed && recovered && noActivation && noMovement && passthrough;
    }
    DestroyWindow(cover);
    return passed ? 0 : 1;
}
