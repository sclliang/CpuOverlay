#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace GameOverlay {
constexpr uint32_t Magic = 0x434f4731, Version = 1;
constexpr uint32_t D3D11 = 1, D3D12 = 2, OpenGL = 4, Vulkan = 8, All = 15;
constexpr uint32_t MaxWidth = 4096, MaxHeight = 2048;
constexpr uint32_t MaxPixels = MaxWidth * MaxHeight * 4;
// Fixed-width, versioned wire layout. No pointers or STL objects cross processes.
struct alignas(8) Header {
    uint32_t magic, version, bytes, ownerPid;
    uint64_t heartbeat, generation;
    uint32_t enabled, visible, targetPid, backendMask;
    uint32_t width, height, stride, pixelBytes;
    uint32_t position;
    int32_t x, y, margin;
    float rasterScale;
    uint32_t reserved;
    alignas(8) volatile LONG64 lastDrawTick;
    volatile LONG drawPid, drawBackend, drawCount, drawError;
    volatile LONG viewportWidth, viewportHeight;
};
static_assert(sizeof(Header) % 8 == 0);
constexpr size_t MappingBytes = sizeof(Header) + MaxPixels;
inline std::wstring ChannelName(const wchar_t* suffix) {
    DWORD session = 0; ProcessIdToSessionId(GetCurrentProcessId(), &session);
    wchar_t probe[64]{};
    GetEnvironmentVariableW(L"CPUOVERLAY_CHANNEL_SUFFIX", probe, ARRAYSIZE(probe));
    const std::wstring extra = std::wstring(probe).rfind(L".Probe.", 0) == 0 ? probe : L"";
    return L"Local\\sixunited.CpuOverlay.GameOverlay." + std::to_wstring(session) + extra + suffix;
}
inline const wchar_t* BackendName(uint32_t backend) {
    switch (backend) { case D3D11: return L"D3D11"; case D3D12: return L"D3D12";
    case OpenGL: return L"OpenGL"; case Vulkan: return L"Vulkan"; default: return L"等待游戏"; }
}
inline bool ValidImage(const Header& h) {
    return h.magic == Magic && h.version == Version && h.bytes == sizeof(Header) &&
        h.width && h.width <= MaxWidth && h.height && h.height <= MaxHeight &&
        h.stride == h.width * 4 && h.pixelBytes == h.stride * h.height && h.pixelBytes <= MaxPixels &&
        h.position <= 5 && std::isfinite(h.rasterScale) && h.rasterScale >= 0.25f && h.rasterScale <= 8;
}
struct Rect { float x, y, width, height; };
inline Rect Placement(const Header& h, uint32_t width, uint32_t height, float dpiScale = 1) {
    if (!width || !height || !ValidImage(h)) return {};
    if (!std::isfinite(dpiScale) || dpiScale < 0.25f || dpiScale > 8) dpiScale = 1;
    const float margin = std::clamp(h.margin, 0, 256) * dpiScale;
    const float availableW = std::max(1.0f, width - 2 * margin), availableH = std::max(1.0f, height - 2 * margin);
    const float scale = std::min({dpiScale / h.rasterScale, availableW / h.width, availableH / h.height});
    const float w = h.width * scale, t = h.height * scale;
    float x = margin, y = margin;
    if (h.position == 0 || h.position == 2) x = width - w - margin;
    if (h.position == 2 || h.position == 3) y = height - t - margin;
    if (h.position == 4) x = (width - w) * 0.5f;
    if (h.position == 5) { x = std::max(0, h.x) * dpiScale; y = std::max(0, h.y) * dpiScale; }
    return {std::clamp(x, 0.0f, std::max(0.0f, width - w)), std::clamp(y, 0.0f, std::max(0.0f, height - t)), w, t};
}
struct Frame { Header header{}; std::vector<uint8_t> pixels; };
}
