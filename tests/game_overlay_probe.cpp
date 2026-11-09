#include "game_overlay_host.h"
#include "game_overlay_deployment.h"
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <gl/GL.h>
#include <wrl/client.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <fstream>

using Microsoft::WRL::ComPtr;
namespace {
HWND window = nullptr;
bool expectOverlay = true;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Hr(HRESULT hr, const char* message) { if (FAILED(hr)) { std::cerr << message << " HRESULT=0x" << std::hex << hr << std::dec << '\n'; throw std::runtime_error(message); } }
bool Red(const unsigned char* pixel) { return abs(int(pixel[2]) - 220) <= 5 && abs(int(pixel[1]) - 40) <= 5 && abs(int(pixel[0]) - 60) <= 5; }
POINT SamplePoint() {
    const float scale = static_cast<float>(GetDpiForWindow(window)) / 96;
    return {static_cast<LONG>(20 * scale), static_cast<LONG>(20 * scale)};
}
void RoutingProbe() {
    NativeGameOverlay host(false); host.Configure(true, GameOverlay::All);
    OverlaySettings settings; Gdiplus::Bitmap bitmap(8, 8, PixelFormat32bppPARGB);
    Require(host.Publish(bitmap, settings, 1), "Routing bitmap");
    const DWORD pid = GetCurrentProcessId();
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, GameOverlay::ChannelName(L".Frame").c_str());
    Require(mapping != nullptr, "Routing channel");
    auto* header = static_cast<GameOverlay::Header*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, GameOverlay::MappingBytes));
    Require(header != nullptr, "Routing shared state");
    struct Cleanup { HANDLE mapping; void* view; ~Cleanup() { UnmapViewOfFile(view); CloseHandle(mapping); } } cleanup{mapping, header};
    host.Tick(pid, true, true, false);
    Require(host.Status().targetPid == 0 && header->targetPid == 0 && !header->drawAllowed, "Excluded application must keep the desktop route");
    host.Tick(pid, true, true);
    Require(!host.Status().ready && !host.Status().active && !header->drawAllowed, "Desktop visible before renderer handshake");
    InterlockedExchange(&header->readyPid, static_cast<LONG>(pid)); InterlockedExchange(&header->readyBackend, GameOverlay::D3D11);
    InterlockedIncrement(&header->readyCount);
    Require(host.Status().ready && !host.Status().active && !header->drawAllowed, "Ready renderer cannot draw over visible desktop overlay");
    host.SetDesktopVisible(false);
    InterlockedExchange(&header->drawPid, static_cast<LONG>(pid)); InterlockedExchange(&header->drawBackend, GameOverlay::D3D11);
    InterlockedIncrement(&header->drawCount); InterlockedExchange64(&header->lastDrawTick, 1);
    Require(host.Status().active && header->drawAllowed, "Internal route starts only after desktop hide");
    host.Tick(pid, false, true);
    Require(host.Status().active && header->targetPid == pid, "Idle Present stream must not bring back a duplicate desktop overlay");
    host.Tick(pid, true, true, false); host.SetDesktopVisible(true);
    Require(!host.Status().active && !host.Status().ready && !header->targetPid && !header->drawAllowed, "Exclusion revokes the internal route and restores desktop ownership");
    host.Tick(pid, true, true);
    Require(!host.Status().ready && !host.Status().active, "Old acknowledgements cannot activate a new routing session");
    host.SetVisible(false);
    Require(!host.Status().ready && !host.Status().active, "Hidden monitor owns neither display route");
    std::cout << "Routing handshake, pause retention, exclusions and session reset: PASS\n";
}
void DeploymentProbe(const std::filesystem::path& source) {
    namespace fs = std::filesystem;
    struct Fixture {
        fs::path folder = fs::temp_directory_path() / (L"CpuOverlay.Deployment." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()));
        HMODULE legacy = nullptr, current = nullptr;
        ~Fixture() {
            if (current) FreeLibrary(current); if (legacy) FreeLibrary(legacy);
            DeleteFileW((folder / L"CpuOverlayGame64.dll").c_str());
            DeleteFileW((folder / CPUOVERLAY_GAME_MODULE_WNAME).c_str());
            DeleteFileW((folder / L"CpuOverlayGame64-0.0.0.0.dll").c_str());
            RemoveDirectoryW(folder.c_str());
        }
    } fixture;
    fs::create_directory(fixture.folder);
    const auto legacy = fixture.folder / L"CpuOverlayGame64.dll", current = fixture.folder / CPUOVERLAY_GAME_MODULE_WNAME;
    std::ifstream input(source, std::ios::binary); std::vector<BYTE> bytes((std::istreambuf_iterator<char>(input)), {});
    Require(!bytes.empty(), "Deployment source image");
    fs::copy_file(source, legacy);
    fixture.legacy = LoadLibraryW(legacy.c_str()); Require(fixture.legacy != nullptr, "Load legacy module fixture");
    HANDLE blocked = CreateFileW(legacy.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD reason = GetLastError(); if (blocked != INVALID_HANDLE_VALUE) CloseHandle(blocked);
    Require(blocked == INVALID_HANDLE_VALUE && (reason == ERROR_SHARING_VIOLATION || reason == ERROR_ACCESS_DENIED), "Reproduce the installed DLL write lock");
    std::cout << "loaded_legacy_write_error=" << reason << '\n';
    Require(GameOverlayDeployment::InstallImage(current, bytes.data(), static_cast<DWORD>(bytes.size())) == ERROR_SUCCESS, "Install a new version while legacy is loaded");
    fixture.current = LoadLibraryW(current.c_str());
    Require(fixture.current && fixture.current != fixture.legacy, "New module must have a distinct loader identity");
    Require(GameOverlayDeployment::InstallImage(current, bytes.data(), static_cast<DWORD>(bytes.size())) == ERROR_SUCCESS, "Repair an identical current module while it is loaded");
    const auto unrelated = fixture.folder / L"CpuOverlayGame64-0.0.0.0.dll";
    { std::ofstream file(unrelated); file << "Not a CpuOverlay DLL"; }
    const auto retained = GameOverlayDeployment::RetireModules(fixture.folder, current.filename().wstring());
    Require(retained.size() == 1 && fs::exists(legacy) && fs::exists(current) && fs::exists(unrelated), "Upgrade preserves a loaded old module and unrelated files");
    FreeLibrary(fixture.legacy); fixture.legacy = nullptr;
    Require(GameOverlayDeployment::RetireModules(fixture.folder, current.filename().wstring()).empty() && !fs::exists(legacy), "Remove a retired module after its process releases it");
    FreeLibrary(fixture.current); fixture.current = nullptr;
    Require(GameOverlayDeployment::RetireModules(fixture.folder, L"").empty() && !fs::exists(current) && fs::exists(unrelated), "Retirement only deletes verified product DLLs");
    std::cout << "Locked-file upgrade, loaded-module repair and bounded retirement: PASS\n";
}
void D3D11Probe() {
    DXGI_SWAP_CHAIN_DESC desc{}; desc.BufferDesc.Width = 320; desc.BufferDesc.Height = 180;
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1; desc.BufferCount = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow = window; desc.Windowed = TRUE; desc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; ComPtr<IDXGISwapChain> chain;
    Hr(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        &desc, &chain, &device, nullptr, &context), "D3D11 device");
    ComPtr<ID3D11Texture2D> buffer; Hr(chain->GetBuffer(0, IID_PPV_ARGS(&buffer)), "D3D11 buffer");
    ComPtr<ID3D11RenderTargetView> target; Hr(device->CreateRenderTargetView(buffer.Get(), nullptr, &target), "D3D11 RTV");
    const float background[]{0.9f, 0.93f, 0.96f, 1}; context->ClearRenderTargetView(target.Get(), background);
    Hr(chain->Present(0, 0), "D3D11 present");
    D3D11_TEXTURE2D_DESC td{}; buffer->GetDesc(&td); td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback; Hr(device->CreateTexture2D(&td, nullptr, &readback), "D3D11 staging");
    context->CopyResource(readback.Get(), buffer.Get());
    D3D11_MAPPED_SUBRESOURCE data{}; Hr(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &data), "D3D11 readback");
    const auto point = SamplePoint(); const auto* pixel = static_cast<const unsigned char*>(data.pData) + point.y * data.RowPitch + point.x * 4;
    const bool drawn = Red(pixel);
    std::cout << "D3D11 pixel BGRA=" << int(pixel[0]) << ',' << int(pixel[1]) << ',' << int(pixel[2]) << ',' << int(pixel[3]) << '\n';
    context->Unmap(readback.Get(), 0); Require(drawn == expectOverlay, "D3D11 overlay visibility mismatch");
}
D3D12_RESOURCE_BARRIER Barrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = resource;
    b.Transition.StateBefore = before; b.Transition.StateAfter = after; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; return b;
}
void D3D12Probe() {
    ComPtr<ID3D12Device> device; Hr(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "D3D12 device");
    D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; ComPtr<ID3D12CommandQueue> queue;
    Hr(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)), "D3D12 queue");
    ComPtr<IDXGIFactory4> factory; Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "D3D12 factory");
    DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = 320; desc.Height = 180; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.BufferCount = 2; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    ComPtr<IDXGISwapChain1> initial; Hr(factory->CreateSwapChainForHwnd(queue.Get(), window, &desc, nullptr, nullptr, &initial), "D3D12 swapchain");
    ComPtr<IDXGISwapChain3> chain; Hr(initial.As(&chain), "D3D12 swapchain3");
    ComPtr<ID3D12Resource> buffer; Hr(chain->GetBuffer(chain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&buffer)), "D3D12 buffer");
    ComPtr<ID3D12CommandAllocator> allocator; Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "D3D12 allocator");
    ComPtr<ID3D12GraphicsCommandList> commands; Hr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)), "D3D12 commands");
    ComPtr<ID3D12DescriptorHeap> targets; D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = 1;
    Hr(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&targets)), "D3D12 heap");
    const auto target = targets->GetCPUDescriptorHandleForHeapStart(); device->CreateRenderTargetView(buffer.Get(), nullptr, target);
    auto barrier = Barrier(buffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET); commands->ResourceBarrier(1, &barrier);
    const float color[]{0.9f, 0.93f, 0.96f, 1}; commands->ClearRenderTargetView(target, color, 0, nullptr);
    barrier = Barrier(buffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT); commands->ResourceBarrier(1, &barrier);
    Hr(commands->Close(), "D3D12 close"); ID3D12CommandList* list = commands.Get(); queue->ExecuteCommandLists(1, &list);
    ComPtr<ID3D12Fence> fence; Hr(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "D3D12 fence");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr); Require(event != nullptr, "D3D12 event");
    auto wait = [&](UINT64 value) { Hr(queue->Signal(fence.Get(), value), "D3D12 signal"); Hr(fence->SetEventOnCompletion(value, event), "D3D12 completion"); Require(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0, "D3D12 GPU timeout"); };
    wait(1); Hr(chain->Present(0, 0), "D3D12 present"); wait(2);
    const auto bd = buffer->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 size = 0;
    device->GetCopyableFootprints(&bd, 0, 1, 0, &footprint, nullptr, nullptr, &size);
    D3D12_RESOURCE_DESC rb{}; rb.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rb.Width = size; rb.Height = rb.DepthOrArraySize = rb.MipLevels = 1;
    rb.SampleDesc.Count = 1; rb.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK; heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    ComPtr<ID3D12Resource> readback; Hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rb, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "D3D12 readback buffer");
    Hr(allocator->Reset(), "D3D12 allocator reset"); Hr(commands->Reset(allocator.Get(), nullptr), "D3D12 command reset");
    barrier = Barrier(buffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE); commands->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION from{}; from.pResource = buffer.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION to{}; to.pResource = readback.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint = footprint;
    commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    barrier = Barrier(buffer.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT); commands->ResourceBarrier(1, &barrier);
    Hr(commands->Close(), "D3D12 readback close"); queue->ExecuteCommandLists(1, &list); wait(3);
    void* mapped = nullptr; Hr(readback->Map(0, nullptr, &mapped), "D3D12 readback map");
    const auto point = SamplePoint(); const auto* pixel = static_cast<const unsigned char*>(mapped) + footprint.Offset + point.y * footprint.Footprint.RowPitch + point.x * 4;
    const bool drawn = Red(pixel); std::cout << "D3D12 pixel BGRA=" << int(pixel[0]) << ',' << int(pixel[1]) << ',' << int(pixel[2]) << ',' << int(pixel[3]) << '\n';
    readback->Unmap(0, nullptr); CloseHandle(event); Require(drawn == expectOverlay, "D3D12 overlay visibility mismatch");
}
void OpenGLProbe(int requestedVersion = 0) {
    HDC dc = GetDC(window); PIXELFORMATDESCRIPTOR format{}; format.nSize = sizeof(format); format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER; format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32; format.cAlphaBits = 8;
    const int index = ChoosePixelFormat(dc, &format); Require(index && (GetPixelFormat(dc) || SetPixelFormat(dc, index, &format)), "OpenGL pixel format");
    HGLRC context = wglCreateContext(dc); Require(context && wglMakeCurrent(dc, context), "OpenGL context");
    if (requestedVersion) {
        using CreateContext = HGLRC (WINAPI*)(HDC, HGLRC, const int*);
        const auto create = reinterpret_cast<CreateContext>(wglGetProcAddress("wglCreateContextAttribsARB"));
        Require(create != nullptr, "OpenGL explicit contexts unavailable");
        const int legacy[]{0x2091, 2, 0x2092, 1, 0};
        const int core[]{0x2091, 3, 0x2092, 3, 0x9126, 1, 0};
        HGLRC selected = create(dc, nullptr, requestedVersion == 2 ? legacy : core); Require(selected != nullptr, "OpenGL selected context");
        Require(wglMakeCurrent(dc, selected) != FALSE, "OpenGL selected context current"); wglDeleteContext(context); context = selected;
    }
    std::cout << "OpenGL version=" << glGetString(GL_VERSION) << '\n';
    glViewport(0, 0, 320, 180); glClearColor(0.9f, 0.93f, 0.96f, 1);
    glDrawBuffer(GL_FRONT); glClear(GL_COLOR_BUFFER_BIT); glDrawBuffer(GL_BACK); glClear(GL_COLOR_BUFFER_BIT); glFinish();
    Require(SwapBuffers(dc) != FALSE, "OpenGL swap"); glFinish();
    const auto point = SamplePoint(); unsigned char front[4]{}, back[4]{};
    glReadBuffer(GL_FRONT); glReadPixels(point.x, 180 - point.y - 1, 1, 1, 0x80e1, GL_UNSIGNED_BYTE, front);
    glReadBuffer(GL_BACK); glReadPixels(point.x, 180 - point.y - 1, 1, 1, 0x80e1, GL_UNSIGNED_BYTE, back);
    std::cout << "OpenGL front BGRA=" << int(front[0]) << ',' << int(front[1]) << ',' << int(front[2])
        << " back=" << int(back[0]) << ',' << int(back[1]) << ',' << int(back[2]) << '\n';
    const bool drawn = Red(front) || Red(back);
    wglMakeCurrent(nullptr, nullptr); wglDeleteContext(context); ReleaseDC(window, dc); Require(drawn == expectOverlay, "OpenGL overlay visibility mismatch");
}
}
bool VulkanOverlayProbe(HWND window, const std::filesystem::path& directory, bool expected);
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 3, "Usage: CpuOverlayGameProbe <D3D11|D3D12|OpenGL|Vulkan> <game DLL>");
        const std::wstring api = argv[1]; const std::filesystem::path dll = std::filesystem::absolute(argv[2]);
        const DWORD backend = (api == L"D3D11" || api == L"Remote" || api == L"RemoteChild" || api == L"Routing" || api == L"Deployment") ? GameOverlay::D3D11 : api == L"D3D12" ? GameOverlay::D3D12 : api.rfind(L"OpenGL", 0) == 0 ? GameOverlay::OpenGL : api == L"Vulkan" ? GameOverlay::Vulkan : 0;
        Require(backend != 0, "Unknown API");
        if (api != L"RemoteChild") SetEnvironmentVariableW(L"CPUOVERLAY_CHANNEL_SUFFIX", (L".Probe." + std::to_wstring(GetCurrentProcessId())).c_str());
        Gdiplus::GdiplusStartupInput startup; ULONG_PTR token = 0; Require(Gdiplus::GdiplusStartup(&token, &startup, nullptr) == Gdiplus::Ok, "GDI+ startup");
        if (api == L"Routing") { RoutingProbe(); std::cout << "PASS\n"; return 0; }
        if (api == L"Deployment") { DeploymentProbe(dll); std::cout << "PASS\n"; return 0; }
        WNDCLASSW wc{}; wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = DefWindowProcW; wc.lpszClassName = L"CpuOverlay.NativeProbe"; wc.style = CS_OWNDC;
        RegisterClassW(&wc); window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"CpuOverlay rendering probe", WS_POPUP,
            0, 0, 320, 180, nullptr, nullptr, wc.hInstance, nullptr); Require(window != nullptr, "Probe window");
        if (api == L"RemoteChild") {
            for (unsigned i = 0; i < 50; ++i) {
                if (GetModuleHandleW(dll.filename().c_str())) {
                    try { D3D11Probe(); DestroyWindow(window); window = nullptr; return 0; } catch (...) {}
                }
                Sleep(100);
            }
            throw std::runtime_error("Remote loader did not produce an overlay");
        }
        NativeGameOverlay host(false); host.Configure(true, backend);
        OverlaySettings settings; settings.position = 1; settings.margin = 8;
        Gdiplus::Bitmap bitmap(64, 32, PixelFormat32bppPARGB);
        { Gdiplus::Graphics graphics(&bitmap); graphics.Clear(Gdiplus::Color(255, 220, 40, 60)); }
        Require(host.Publish(bitmap, settings, 1), "Publish bitmap"); host.Tick(GetCurrentProcessId(), true, true);
        if (api == L"Remote") {
            wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
            std::wstring command = std::wstring(L"\"") + executable + L"\" RemoteChild \"" + dll.wstring() + L"\"";
            STARTUPINFOW startupInfo{sizeof(startupInfo)}; startupInfo.dwFlags = STARTF_USESHOWWINDOW; startupInfo.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION child{};
            Require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &child) != FALSE,
                "Start remote rendering fixture");
            CloseHandle(child.hThread); DWORD wait = WAIT_TIMEOUT;
            for (unsigned i = 0; i < 75 && wait == WAIT_TIMEOUT; ++i) {
                host.Tick(child.dwProcessId, true, true);
                if (host.Status().ready) host.SetDesktopVisible(false);
                wait = WaitForSingleObject(child.hProcess, 200);
            }
            if (wait == WAIT_TIMEOUT) TerminateProcess(child.hProcess, 9);
            DWORD code = 0; GetExitCodeProcess(child.hProcess, &code); CloseHandle(child.hProcess);
            const auto status = host.Status();
            std::cout << "remote_exit=" << code << " frames=" << status.frames << " attach_error=" << status.error << '\n';
            Require(wait == WAIT_OBJECT_0 && code == 0 && status.frames > 0 && status.error == 0, "Remote injection failed");
            host.SetVisible(false); DestroyWindow(window); window = nullptr; std::cout << "PASS\n"; return 0;
        }
        HMODULE module = LoadLibraryW(dll.c_str()); Require(module != nullptr, "Load native module");
        auto start = reinterpret_cast<DWORD (WINAPI*)(void*)>(GetProcAddress(module, "CpuOverlayStart"));
        Require(start && start(nullptr), "Install native hooks");
        auto draw = [&] {
            if (backend == GameOverlay::D3D11) D3D11Probe();
            else if (backend == GameOverlay::D3D12) D3D12Probe();
            else if (backend == GameOverlay::OpenGL) OpenGLProbe(api == L"OpenGL2" ? 2 : api == L"OpenGL3" ? 3 : 0);
            else Require(VulkanOverlayProbe(window, dll.parent_path(), expectOverlay), "Vulkan overlay rendering");
        };
        expectOverlay = false; draw();
        Require(host.Status().ready && !host.Status().active && host.Status().frames == 0, "Renderer drew before desktop overlay was hidden");
        host.SetDesktopVisible(false); expectOverlay = true; draw();
        const auto status = host.Status();
        std::cout << "backend=" << backend << " frames=" << status.frames << " active=" << status.active << " extent=" << status.width << 'x' << status.height << '\n';
        Require(status.active && status.backend == backend && status.frames > 0, "Native module did not report drawing");
        host.SetVisible(false); expectOverlay = false; draw();
        Require(host.Status().frames == status.frames && !host.Status().active, "Hidden overlay still draws");
        DestroyWindow(window); window = nullptr;
        std::cout << "PASS\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; if (window) DestroyWindow(window); return 1; }
}
