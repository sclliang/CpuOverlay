// Elevated integration fixture: real D3D11 presents, not fabricated ETW samples.
#include "frame_rate_monitor.h"
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <iostream>

using Microsoft::WRL::ComPtr;
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto foreground = GetForegroundWindow();
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC",
        L"CpuOverlay graphics test", WS_POPUP | WS_DISABLED, work.right - 112, work.bottom - 88, 96, 64,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return 2;
    ShowWindow(window, SW_SHOWNOACTIVATE);
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferDesc.Width = 96; description.BufferDesc.Height = 64;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1; description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2; description.OutputWindow = window;
    description.Windowed = TRUE; description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    auto error = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &description, &swap, &device, nullptr, &context);
    ComPtr<ID3D11Texture2D> backBuffer;
    ComPtr<ID3D11RenderTargetView> target;
    if (SUCCEEDED(error)) error = swap->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (SUCCEEDED(error)) error = device->CreateRenderTargetView(backBuffer.Get(), nullptr, &target);
    if (FAILED(error)) { DestroyWindow(window); std::cerr << "D3D11 setup failed: " << std::hex << error << '\n'; return 2; }
    FrameRateMonitor monitor;
    auto sample = monitor.Collect(GetCurrentProcessId());
    if (!sample.running) { DestroyWindow(window); std::cerr << "ETW start failed: " << sample.error << '\n'; return 2; }
    LARGE_INTEGER frequency{}, now{}; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&now);
    auto seconds = [&]() { LARGE_INTEGER qpc{}; QueryPerformanceCounter(&qpc); return static_cast<double>(qpc.QuadPart) / frequency.QuadPart; };
    const double start = seconds();
    double nextSample = start, measured = 0, expected = 0, onePercentLow = 0;
    unsigned estimates = 0, slowEstimates = 0, presents = 0;
    bool slowHistoryValid = false;
    std::deque<double> times, history;
    while (seconds() - start < 14) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        const float color[] = {0.05f, 0.13f, 0.20f, 1};
        context->ClearRenderTargetView(target.Get(), color);
        const double submitted = seconds();
        if (swap->Present(1, 0) != S_OK) { Sleep(10); continue; }
        ++presents; times.push_back(submitted);
        while (!times.empty() && submitted - times.front() > 2) times.pop_front();
        history.push_back(submitted);
        while (!history.empty() && submitted - history.front() > 10) history.pop_front();
        if (seconds() >= nextSample) {
            sample = monitor.Collect(GetCurrentProcessId());
            // Exercise the maximum user-configurable display interval while the
            // renderer continues presenting. ETW ingestion must continue between reads.
            const bool slowRefresh = seconds() - start >= 3;
            nextSample = seconds() + (slowRefresh ? 5 : .25);
            if (sample.fps && times.size() > 2 && times.back() > times.front()) {
                ++estimates; measured = *sample.fps;
                expected = (times.size() - 1) / (times.back() - times.front());
                if (sample.onePercentLow) onePercentLow = *sample.onePercentLow;
                if (slowRefresh && seconds() - start >= 7) {
                    ++slowEstimates;
                    slowHistoryValid = sample.onePercentLow &&
                        sample.intervalCount >= static_cast<unsigned>(history.size() * .7);
                }
            }
        }
    }
    monitor.Pause();
    DestroyWindow(window);
    const bool focused = GetForegroundWindow() == foreground;
    const bool valid = estimates >= 3 && slowEstimates >= 2 && slowHistoryValid && expected > 1 && onePercentLow > 0 &&
        std::abs(measured - expected) < std::max(5.0, expected * .25);
    std::cout << "presents=" << presents << " estimates=" << estimates << " presentmon_fps=" << measured
              << " reference_fps=" << expected << " one_percent_low=" << onePercentLow
              << " slow_refresh_estimates=" << slowEstimates << " slow_history_valid=" << slowHistoryValid
              << " foreground_preserved=" << focused << '\n';
    return valid && focused ? 0 : 1;
}
