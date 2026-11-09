#pragma once
#include "overlay_renderer.h"
#include "game_overlay_protocol.h"
#include <memory>

struct GameOverlayStatus {
    bool enabled = false, active = false;
    DWORD targetPid = 0, backend = 0, error = 0, layerError = 0;
    LONG frames = 0;
    LONG renderError = 0;
    uint32_t width = 0, height = 0;
    std::wstring Text() const;
};
class NativeGameOverlay {
public:
    explicit NativeGameOverlay(bool registerVulkanLayer = true);
    ~NativeGameOverlay();
    NativeGameOverlay(const NativeGameOverlay&) = delete;
    NativeGameOverlay& operator=(const NativeGameOverlay&) = delete;
    void Configure(bool enabled, DWORD backendMask);
    bool Publish(Gdiplus::Bitmap& bitmap, const OverlaySettings& settings, float rasterScale);
    void Tick(DWORD foregroundPid, bool hasFrames, bool visible);
    void SetVisible(bool visible);
    GameOverlayStatus Status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
