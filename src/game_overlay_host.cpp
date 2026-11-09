#include "game_overlay_host.h"
#include "game_overlay_registration.h"
#include <tlhelp32.h>
#include <sddl.h>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace {
namespace fs = std::filesystem;
using GameOverlay::Header;
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete; Handle& operator=(const Handle&) = delete;
};
fs::path ExecutableFolder() {
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, ARRAYSIZE(path)); return fs::path(path).parent_path();
}
uintptr_t RemoteModule(DWORD pid, const wchar_t* module) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (snapshot.value == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot.value, &entry)) do {
        if (_wcsicmp(entry.szModule, module) == 0) return reinterpret_cast<uintptr_t>(entry.modBaseAddr);
    } while (Module32NextW(snapshot.value, &entry));
    return 0;
}
bool Candidate(DWORD pid) {
    if (!pid || pid == GetCurrentProcessId() || pid <= 4) return false;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)); if (!process.value) return false;
    wchar_t path[32768]{}; DWORD size = ARRAYSIZE(path);
    if (!QueryFullProcessImageNameW(process.value, 0, path, &size)) return false;
    const auto filename = fs::path(path).filename().wstring();
    const wchar_t* excluded[]{L"dwm.exe", L"explorer.exe", L"chrome.exe", L"msedge.exe", L"firefox.exe", L"Codex.exe",
        L"Code.exe", L"devenv.exe", L"obs64.exe", L"DesktopOverlayHost.exe", L"NVIDIA Overlay.exe", L"ApplicationFrameHost.exe"};
    for (const auto* name : excluded) if (_wcsicmp(filename.c_str(), name) == 0) return false;
    return true;
}
DWORD Attach(DWORD pid, const fs::path& dll, const std::atomic<bool>& stop) {
    if (!fs::is_regular_file(dll)) return ERROR_FILE_NOT_FOUND;
    Handle process(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid));
    if (!process.value) return GetLastError();
    USHORT machine = 0, native = 0;
    if (!IsWow64Process2(process.value, &machine, &native)) return GetLastError();
    if (machine != IMAGE_FILE_MACHINE_UNKNOWN || native != IMAGE_FILE_MACHINE_AMD64) return ERROR_BAD_EXE_FORMAT;
    uintptr_t remoteDll = RemoteModule(pid, dll.filename().c_str());
    if (!remoteDll) {
        // Resolve the actual module owning LoadLibraryW, including KernelBase forwarding.
        auto loader = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"); HMODULE owner = nullptr;
        if (!loader || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(loader), &owner)) return GetLastError();
        wchar_t ownerPath[MAX_PATH]{}; GetModuleFileNameW(owner, ownerPath, ARRAYSIZE(ownerPath));
        const auto remoteOwner = RemoteModule(pid, fs::path(ownerPath).filename().c_str()); if (!remoteOwner) return ERROR_MOD_NOT_FOUND;
        const auto address = remoteOwner + reinterpret_cast<uintptr_t>(loader) - reinterpret_cast<uintptr_t>(owner);
        const auto path = dll.wstring(); const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
        auto* memory = VirtualAllocEx(process.value, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!memory) return GetLastError();
        if (!WriteProcessMemory(process.value, memory, path.c_str(), bytes, nullptr)) {
            const auto error = GetLastError(); VirtualFreeEx(process.value, memory, 0, MEM_RELEASE); return error;
        }
        Handle thread(CreateRemoteThread(process.value, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(address), memory, 0, nullptr));
        if (!thread.value) { const auto error = GetLastError(); VirtualFreeEx(process.value, memory, 0, MEM_RELEASE); return error; }
        while (WaitForSingleObject(thread.value, 100) == WAIT_TIMEOUT) {
            if (stop.load()) return ERROR_CANCELLED; // Keep the argument allocation alive until the target exits.
            if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) return ERROR_PROCESS_ABORTED;
        }
        VirtualFreeEx(process.value, memory, 0, MEM_RELEASE);
        remoteDll = RemoteModule(pid, dll.filename().c_str()); if (!remoteDll) return ERROR_DLL_INIT_FAILED;
    }
    HMODULE local = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!local) return GetLastError();
    auto start = GetProcAddress(local, "CpuOverlayStart");
    const uintptr_t rva = start ? reinterpret_cast<uintptr_t>(start) - reinterpret_cast<uintptr_t>(local) : 0;
    FreeLibrary(local); if (!rva) return ERROR_PROC_NOT_FOUND;
    Handle thread(CreateRemoteThread(process.value, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteDll + rva), nullptr, 0, nullptr));
    if (!thread.value) return GetLastError();
    while (WaitForSingleObject(thread.value, 100) == WAIT_TIMEOUT) {
        if (stop.load()) return ERROR_CANCELLED;
        if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) return ERROR_PROCESS_ABORTED;
    }
    DWORD result = 0; if (!GetExitCodeThread(thread.value, &result)) return GetLastError();
    return result ? ERROR_SUCCESS : ERROR_DLL_INIT_FAILED;
}
}
struct NativeGameOverlay::Impl {
    fs::path folder = ExecutableFolder() / L"bin" / L"game";
    HANDLE mapping = nullptr, mutex = nullptr;
    Header* shared = nullptr;
    std::atomic<bool> stop{false}, enabled{false}, visible{false};
    std::atomic<DWORD> target{0}, attachError{0}, layerError{0};
    std::thread worker;
    DWORD backendMask = GameOverlay::All;
    bool layerRegistered = false;
    bool registerLayer = true;
    uint64_t generation = 0;
    std::mutex configMutex;
    explicit Impl(bool registerVulkanLayer) : registerLayer(registerVulkanLayer) { Open(); worker = std::thread([this] { Work(); }); }
    ~Impl() {
        stop = true; if (worker.joinable()) worker.join();
        if (registerLayer) SetVulkanLayer(false);
        if (Lock()) { shared->visible = shared->enabled = 0; shared->heartbeat = 0; ReleaseMutex(mutex); }
        if (shared) UnmapViewOfFile(shared);
        if (mapping) CloseHandle(mapping); if (mutex) CloseHandle(mutex);
    }
    bool Lock() const {
        if (!shared || !mutex) return false;
        const auto result = WaitForSingleObject(mutex, 30); return result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    }
    void Open() {
        Handle token; // Open objects only to this user and SYSTEM, at medium integrity.
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) {
            DWORD bytes = 0; GetTokenInformation(token.value, TokenUser, nullptr, 0, &bytes);
            std::vector<BYTE> data(bytes);
            if (GetTokenInformation(token.value, TokenUser, data.data(), bytes, &bytes)) {
                LPWSTR sid = nullptr;
                if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid)) {
                    const auto sddl = std::wstring(L"D:P(A;;GA;;;SY)(A;;GA;;;") + sid + L")S:(ML;;NW;;;ME)";
                    ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr); LocalFree(sid);
                }
            }
        }
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
        mutex = CreateMutexW(descriptor ? &attributes : nullptr, FALSE, GameOverlay::ChannelName(L".Mutex").c_str());
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, descriptor ? &attributes : nullptr, PAGE_READWRITE, 0,
            static_cast<DWORD>(GameOverlay::MappingBytes), GameOverlay::ChannelName(L".Frame").c_str());
        if (descriptor) LocalFree(descriptor);
        if (mapping) shared = static_cast<Header*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, GameOverlay::MappingBytes));
        if (!Lock()) { attachError = GetLastError(); return; }
        generation = shared->magic == GameOverlay::Magic ? shared->generation : 0;
        memset(shared, 0, sizeof(Header)); shared->magic = GameOverlay::Magic; shared->version = GameOverlay::Version;
        shared->bytes = sizeof(Header); shared->ownerPid = GetCurrentProcessId(); shared->backendMask = backendMask; shared->rasterScale = 1;
        ReleaseMutex(mutex);
    }
    void SetVulkanLayer(bool active) {
        const auto manifest = folder / L"CpuOverlayGame64.json";
        if (active && !fs::is_regular_file(manifest)) { layerError = ERROR_FILE_NOT_FOUND; return; }
        const auto result = ConfigureGameOverlayLayer(manifest, active);
        layerError = result;
        layerRegistered = active && result == ERROR_SUCCESS;
    }
    void Work() {
        std::unordered_map<DWORD, ULONGLONG> attempts;
        while (!stop.load()) {
            const DWORD pid = enabled.load() ? target.load() : 0;
            if (pid && Candidate(pid)) {
                const auto now = GetTickCount64();
                const auto drawTick = shared ? static_cast<ULONGLONG>(InterlockedCompareExchange64(&shared->lastDrawTick, 0, 0)) : 0;
                const bool drawing = shared && static_cast<DWORD>(InterlockedCompareExchange(&shared->drawPid, 0, 0)) == pid &&
                    drawTick && now >= drawTick && now - drawTick < 3000;
                if (!drawing && (!attempts.count(pid) || now - attempts[pid] >= 15000)) {
                    attempts[pid] = now; attachError = Attach(pid, folder / L"CpuOverlayGame64.dll", stop);
                }
            }
            for (int i = 0; i < 5 && !stop.load(); ++i) Sleep(100);
        }
    }
};
std::wstring GameOverlayStatus::Text() const {
    if (!enabled) return L"游戏内显示已关闭";
    if (active) return std::wstring(L"游戏内显示：") + GameOverlay::BackendName(backend);
    if (error == ERROR_ACCESS_DENIED) return L"目标程序不允许接入，桌面显示仍可使用";
    if (error == ERROR_BAD_EXE_FORMAT) return L"当前游戏内显示支持 64 位程序";
    if (renderError) return L"游戏内绘制尚未就绪，桌面显示仍可使用";
    if (layerError) return L"Vulkan 尚未就绪，请检查程序文件是否完整";
    return L"等待前台游戏；Vulkan 游戏请在本程序之后启动";
}
NativeGameOverlay::NativeGameOverlay(bool registerVulkanLayer) : impl_(std::make_unique<Impl>(registerVulkanLayer)) {}
NativeGameOverlay::~NativeGameOverlay() = default;
void NativeGameOverlay::Configure(bool enabled, DWORD backendMask) {
    auto& p = *impl_; std::lock_guard<std::mutex> lock(p.configMutex);
    const bool vulkan = enabled && (backendMask & GameOverlay::Vulkan);
    if (p.registerLayer) p.SetVulkanLayer(vulkan);
    p.backendMask = backendMask & GameOverlay::All; p.enabled = enabled;
    if (p.Lock()) { p.shared->enabled = enabled; p.shared->backendMask = p.backendMask; ReleaseMutex(p.mutex); }
}
bool NativeGameOverlay::Publish(Gdiplus::Bitmap& bitmap, const OverlaySettings& settings, float rasterScale) {
    auto& p = *impl_; const UINT width = bitmap.GetWidth(), height = bitmap.GetHeight();
    if (!width || !height || width > GameOverlay::MaxWidth || height > GameOverlay::MaxHeight) return false;
    Gdiplus::Rect rect(0, 0, width, height); Gdiplus::BitmapData pixels{};
    if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &pixels) != Gdiplus::Ok) return false;
    if (!p.Lock()) { bitmap.UnlockBits(&pixels); return false; }
    auto* destination = reinterpret_cast<BYTE*>(p.shared) + sizeof(Header);
    for (UINT y = 0; y < height; ++y) memcpy(destination + static_cast<size_t>(y) * width * 4,
        static_cast<const BYTE*>(pixels.Scan0) + static_cast<ptrdiff_t>(y) * pixels.Stride, width * 4);
    auto& h = *p.shared; h.width = width; h.height = height; h.stride = width * 4; h.pixelBytes = width * height * 4;
    h.position = settings.position; h.x = settings.x; h.y = settings.y; h.margin = settings.margin; h.rasterScale = rasterScale;
    h.generation = ++p.generation; h.heartbeat = GetTickCount64(); ReleaseMutex(p.mutex); bitmap.UnlockBits(&pixels); return true;
}
void NativeGameOverlay::Tick(DWORD foregroundPid, bool hasFrames, bool visible) {
    auto& p = *impl_; const DWORD pid = hasFrames && p.enabled.load() ? foregroundPid : 0; p.target = pid; p.visible = visible;
    if (p.Lock()) { p.shared->targetPid = pid; p.shared->heartbeat = GetTickCount64(); p.shared->visible = visible; ReleaseMutex(p.mutex); }
}
void NativeGameOverlay::SetVisible(bool visible) {
    auto& p = *impl_; p.visible = visible; if (p.Lock()) { p.shared->visible = visible; p.shared->heartbeat = GetTickCount64(); ReleaseMutex(p.mutex); }
}
GameOverlayStatus NativeGameOverlay::Status() const {
    const auto& p = *impl_; GameOverlayStatus status; status.enabled = p.enabled.load(); status.targetPid = p.target.load();
    status.error = p.attachError.load(); status.layerError = p.layerError.load();
    if (p.shared) {
        status.backend = static_cast<DWORD>(InterlockedCompareExchange(&p.shared->drawBackend, 0, 0));
        status.frames = InterlockedCompareExchange(&p.shared->drawCount, 0, 0);
        status.renderError = InterlockedCompareExchange(&p.shared->drawError, 0, 0);
        status.width = InterlockedCompareExchange(&p.shared->viewportWidth, 0, 0); status.height = InterlockedCompareExchange(&p.shared->viewportHeight, 0, 0);
        const auto tick = static_cast<ULONGLONG>(InterlockedCompareExchange64(&p.shared->lastDrawTick, 0, 0));
        const auto pid = static_cast<DWORD>(InterlockedCompareExchange(&p.shared->drawPid, 0, 0));
        const auto now = GetTickCount64(); status.active = status.enabled && p.visible.load() && status.targetPid && pid == status.targetPid && tick && now >= tick && now - tick < 1500;
    }
    return status;
}
