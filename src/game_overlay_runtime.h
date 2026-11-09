#pragma once
#include "game_overlay_protocol.h"
#include <mutex>

namespace GameOverlay {
// All rendering callbacks serialize on this mutex and preserve the game's API state.
std::recursive_mutex& RenderMutex();
const Frame* ReadFrame(uint32_t backend);
void ReportDraw(uint32_t backend, uint32_t width, uint32_t height);
void ReportError(uint32_t backend, LONG error);
Rect WindowPlacement(const Frame& frame, HWND window, uint32_t width, uint32_t height);
bool InstallGraphicsHooks();
void RegisterSwapChainQueue(void* chain, void* queue);
void TrackCommandQueue(void* queue);
void* FindCommandQueue(void* chain, void* device);
bool DrawD3D11(void* chain);
bool DrawD3D12(void* chain);
void ResetD3D11(void* chain);
void ResetD3D12(void* chain);
bool DrawOpenGL(HDC dc);
void ResetOpenGL(HGLRC context);
}
