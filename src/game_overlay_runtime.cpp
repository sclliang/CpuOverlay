#include "game_overlay_runtime.h"
#include <atomic>

namespace GameOverlay {
namespace {
std::recursive_mutex renderMutex;
struct Reader {
    HANDLE mapping = nullptr, mutex = nullptr;
    Header* shared = nullptr;
    Frame frame;
    ~Reader() { if (shared) UnmapViewOfFile(shared); if (mapping) CloseHandle(mapping); if (mutex) CloseHandle(mutex); }
    bool Open() {
        if (shared) return true;
        mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, ChannelName(L".Frame").c_str());
        if (!mapping) return false;
        shared = static_cast<Header*>(MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, MappingBytes));
        mutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, ChannelName(L".Mutex").c_str());
        if (shared && mutex) return true;
        if (shared) UnmapViewOfFile(shared); shared = nullptr;
        CloseHandle(mapping); mapping = nullptr;
        if (mutex) CloseHandle(mutex); mutex = nullptr;
        return false;
    }
};
Reader& State() { static auto* reader = new Reader; return *reader; }
}
std::recursive_mutex& RenderMutex() { return renderMutex; }
const Frame* ReadFrame(uint32_t backend) {
    auto& r = State();
    if (!r.Open()) return nullptr;
    const DWORD locked = WaitForSingleObject(r.mutex, 0);
    if (locked != WAIT_OBJECT_0 && locked != WAIT_ABANDONED) return nullptr;
    Header h{}; memcpy(&h, r.shared, sizeof(h));
    const auto now = GetTickCount64();
    const bool active = ValidImage(h) && h.enabled && h.visible && h.targetPid == GetCurrentProcessId() &&
        (h.backendMask & backend) && now >= h.heartbeat && now - h.heartbeat < 5000;
    if (active) {
        if (h.ownerPid != r.frame.header.ownerPid || h.generation != r.frame.header.generation || r.frame.pixels.size() != h.pixelBytes) {
            try { r.frame.pixels.resize(h.pixelBytes); }
            catch (...) { ReleaseMutex(r.mutex); return nullptr; }
            memcpy(r.frame.pixels.data(), reinterpret_cast<const uint8_t*>(r.shared) + sizeof(Header), h.pixelBytes);
        }
        memcpy(&r.frame.header, &h, sizeof(h));
    }
    ReleaseMutex(r.mutex);
    return active ? &r.frame : nullptr;
}
bool BeginDraw(const Frame& frame, uint32_t backend) {
    auto& r = State(); if (!r.shared) return false;
    const DWORD locked = WaitForSingleObject(r.mutex, 0);
    if (locked != WAIT_OBJECT_0 && locked != WAIT_ABANDONED) return false;
    const bool current = r.shared->ownerPid == frame.header.ownerPid && r.shared->targetPid == GetCurrentProcessId() &&
        r.shared->enabled && r.shared->visible && (r.shared->backendMask & backend);
    if (current) {
        // Called only after this backend has a valid device and renderer.
        InterlockedExchange(&r.shared->readyPid, static_cast<LONG>(GetCurrentProcessId()));
        InterlockedExchange(&r.shared->readyBackend, static_cast<LONG>(backend));
        InterlockedExchange(&r.shared->drawError, 0);
        InterlockedIncrement(&r.shared->readyCount);
    }
    const bool allowed = current && frame.header.drawAllowed && r.shared->drawAllowed;
    ReleaseMutex(r.mutex); return allowed;
}
void ReportDraw(uint32_t backend, uint32_t width, uint32_t height) {
    auto& r = State(); if (!r.shared) return;
    InterlockedExchange(&r.shared->drawPid, static_cast<LONG>(GetCurrentProcessId()));
    InterlockedExchange(&r.shared->drawBackend, static_cast<LONG>(backend));
    InterlockedExchange(&r.shared->drawError, 0);
    InterlockedExchange(&r.shared->viewportWidth, static_cast<LONG>(width));
    InterlockedExchange(&r.shared->viewportHeight, static_cast<LONG>(height));
    InterlockedIncrement(&r.shared->drawCount);
    InterlockedExchange64(&r.shared->lastDrawTick, static_cast<LONG64>(GetTickCount64()));
}
void ReportError(uint32_t backend, LONG error) {
    auto& r = State(); if (!r.Open()) return;
    InterlockedExchange(&r.shared->drawBackend, static_cast<LONG>(backend));
    InterlockedExchange(&r.shared->drawError, error);
}
Rect WindowPlacement(const Frame& frame, HWND window, uint32_t width, uint32_t height) {
    const float dpi = window ? static_cast<float>(GetDpiForWindow(window)) / 96 : 1;
    return Placement(frame.header, width, height, dpi);
}
}

extern "C" __declspec(dllexport) DWORD WINAPI CpuOverlayStart(void*) {
    static std::atomic<bool> started{false};
    if (started.exchange(true)) return 1;
    // Pin only after an explicit host request. Vulkan's loader otherwise owns the DLL lifetime.
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&CpuOverlayStart), &module);
    return GameOverlay::InstallGraphicsHooks() ? 1 : 0;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
