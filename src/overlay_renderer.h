#pragma once
#include "cpu_sampler.h"
#include "overlay_settings.h"
#include <objidl.h>
#include <gdiplus.h>
#include <memory>
#include <string>

std::wstring FormatOverlayText(const CpuSample& sample);
std::unique_ptr<Gdiplus::Bitmap> RenderOverlay(const CpuSample& sample, float scale, int maxWidth = 0,
    int maxHeight = 0, const OverlaySettings& settings = {}, bool positioning = false);
bool PresentOverlay(HWND window, Gdiplus::Bitmap& bitmap, POINT position);
bool SavePreview(const wchar_t* path);
CpuSample PreviewSample();
