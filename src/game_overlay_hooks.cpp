#include "game_overlay_runtime.h"
#include <MinHook.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace GameOverlay {
namespace {
using Microsoft::WRL::ComPtr;
using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using Resize1Fn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);
using CreateFn = HRESULT (STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateHwndFn = HRESULT (STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
using CreateCompositionFn = HRESULT (STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**);
using ExecuteFn = void (STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using SwapFn = BOOL (WINAPI*)(HDC);
using DeleteContextFn = BOOL (WINAPI*)(HGLRC);
PresentFn present = nullptr; Present1Fn present1 = nullptr;
ResizeFn resize = nullptr; Resize1Fn resize1 = nullptr;
CreateFn create = nullptr; CreateHwndFn createHwnd = nullptr; CreateCompositionFn createComposition = nullptr;
ExecuteFn execute = nullptr; SwapFn swap = nullptr;
DeleteContextFn deleteContext = nullptr;
thread_local unsigned nesting = 0;
struct Entry { Entry() { ++nesting; } ~Entry() { --nesting; } };
void Draw(IDXGISwapChain* chain, UINT flags) noexcept {
    if (flags & DXGI_PRESENT_TEST) return;
    try { if (!DrawD3D11(chain)) DrawD3D12(chain); } catch (...) { ReportError(0, ERROR_UNHANDLED_EXCEPTION); }
}
HRESULT STDMETHODCALLTYPE OnPresent(IDXGISwapChain* chain, UINT sync, UINT flags) {
    Entry entry;
    if (nesting == 1) { std::lock_guard<std::recursive_mutex> lock(RenderMutex()); Draw(chain, flags); }
    return present(chain, sync, flags);
}
HRESULT STDMETHODCALLTYPE OnPresent1(IDXGISwapChain1* chain, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters) {
    Entry entry;
    if (nesting == 1) { std::lock_guard<std::recursive_mutex> lock(RenderMutex()); Draw(chain, flags); }
    return present1(chain, sync, flags, parameters);
}
HRESULT STDMETHODCALLTYPE OnResize(IDXGISwapChain* chain, UINT count, UINT w, UINT h, DXGI_FORMAT format, UINT flags) {
    std::lock_guard<std::recursive_mutex> lock(RenderMutex());
    ResetD3D11(chain); ResetD3D12(chain); return resize(chain, count, w, h, format, flags);
}
HRESULT STDMETHODCALLTYPE OnResize1(IDXGISwapChain3* chain, UINT count, UINT w, UINT h, DXGI_FORMAT format, UINT flags,
    const UINT* masks, IUnknown* const* queues) {
    std::lock_guard<std::recursive_mutex> lock(RenderMutex());
    ResetD3D11(chain); ResetD3D12(chain);
    const auto hr = resize1(chain, count, w, h, format, flags, masks, queues);
    if (SUCCEEDED(hr) && count && queues) RegisterSwapChainQueue(chain, queues[0]);
    return hr;
}
HRESULT STDMETHODCALLTYPE OnCreate(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** chain) {
    const auto hr = create(factory, device, desc, chain);
    if (SUCCEEDED(hr) && chain && *chain) { std::lock_guard<std::recursive_mutex> lock(RenderMutex()); RegisterSwapChainQueue(*chain, device); }
    return hr;
}
HRESULT STDMETHODCALLTYPE OnCreateHwnd(IDXGIFactory2* factory, IUnknown* device, HWND window, const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, IDXGIOutput* output, IDXGISwapChain1** chain) {
    const auto hr = createHwnd(factory, device, window, desc, fullscreen, output, chain);
    if (SUCCEEDED(hr) && chain && *chain) { std::lock_guard<std::recursive_mutex> lock(RenderMutex()); RegisterSwapChainQueue(*chain, device); }
    return hr;
}
HRESULT STDMETHODCALLTYPE OnCreateComposition(IDXGIFactory2* factory, IUnknown* device, const DXGI_SWAP_CHAIN_DESC1* desc,
    IDXGIOutput* output, IDXGISwapChain1** chain) {
    const auto hr = createComposition(factory, device, desc, output, chain);
    if (SUCCEEDED(hr) && chain && *chain) { std::lock_guard<std::recursive_mutex> lock(RenderMutex()); RegisterSwapChainQueue(*chain, device); }
    return hr;
}
void STDMETHODCALLTYPE OnExecute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) {
    try { std::lock_guard<std::recursive_mutex> lock(RenderMutex()); TrackCommandQueue(queue); } catch (...) {}
    execute(queue, count, lists);
}
BOOL WINAPI OnSwap(HDC dc) {
    Entry entry;
    if (nesting == 1) {
        try { std::lock_guard<std::recursive_mutex> lock(RenderMutex()); DrawOpenGL(dc); }
        catch (...) { ReportError(OpenGL, ERROR_UNHANDLED_EXCEPTION); }
    }
    return swap(dc);
}
BOOL WINAPI OnDeleteContext(HGLRC context) {
    std::lock_guard<std::recursive_mutex> lock(RenderMutex()); ResetOpenGL(context); return deleteContext(context);
}
template<class T> bool Hook(void* target, void* replacement, T& original) {
    if (!target) return false;
    if (MH_CreateHook(target, replacement, reinterpret_cast<void**>(&original)) != MH_OK) return false;
    return MH_QueueEnableHook(target) == MH_OK;
}
void** Table(void* object) { return *reinterpret_cast<void***>(object); }
}
bool InstallGraphicsHooks() {
    std::lock_guard<std::recursive_mutex> lock(RenderMutex());
    const auto init = MH_Initialize(); if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
    const wchar_t* name = L"sixunited.CpuOverlay.HookBootstrap";
    WNDCLASSW wc{}; wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = DefWindowProcW; wc.lpszClassName = name;
    const ATOM atom = RegisterClassW(&wc);
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, name, L"", WS_POPUP, 0, 0, 32, 32,
        nullptr, nullptr, wc.hInstance, nullptr);
    bool installed = false;
    if (window) {
        DXGI_SWAP_CHAIN_DESC desc{}; desc.BufferDesc.Width = desc.BufferDesc.Height = 32;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2; desc.OutputWindow = window; desc.Windowed = TRUE;
        ComPtr<IDXGISwapChain> chain; ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        auto hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context);
        if (FAILED(hr)) hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context);
        if (SUCCEEDED(hr)) {
            installed |= Hook(Table(chain.Get())[8], reinterpret_cast<void*>(&OnPresent), present);
            Hook(Table(chain.Get())[13], reinterpret_cast<void*>(&OnResize), resize);
            ComPtr<IDXGISwapChain1> sc1;
            if (SUCCEEDED(chain.As(&sc1))) Hook(Table(sc1.Get())[22], reinterpret_cast<void*>(&OnPresent1), present1);
            ComPtr<IDXGISwapChain3> sc3;
            if (SUCCEEDED(chain.As(&sc3))) Hook(Table(sc3.Get())[39], reinterpret_cast<void*>(&OnResize1), resize1);
        }
        ComPtr<IDXGIFactory2> factory;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            Hook(Table(factory.Get())[10], reinterpret_cast<void*>(&OnCreate), create);
            Hook(Table(factory.Get())[15], reinterpret_cast<void*>(&OnCreateHwnd), createHwnd);
            Hook(Table(factory.Get())[24], reinterpret_cast<void*>(&OnCreateComposition), createComposition);
        }
        ComPtr<ID3D12Device> d12; ComPtr<ID3D12CommandQueue> queue;
        if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d12)))) {
            D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            if (SUCCEEDED(d12->CreateCommandQueue(&q, IID_PPV_ARGS(&queue))))
                Hook(Table(queue.Get())[10], reinterpret_cast<void*>(&OnExecute), execute);
        }
        DestroyWindow(window);
    }
    if (atom) UnregisterClassW(name, wc.hInstance);
    HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
    installed |= Hook(reinterpret_cast<void*>(GetProcAddress(gdi, "SwapBuffers")), reinterpret_cast<void*>(&OnSwap), swap);
    Hook(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"opengl32.dll"), "wglDeleteContext")), reinterpret_cast<void*>(&OnDeleteContext), deleteContext);
    return MH_ApplyQueued() == MH_OK && installed;
}
}
