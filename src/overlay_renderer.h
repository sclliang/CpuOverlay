#pragma once
#include "cpu_sampler.h"
#include <objidl.h>
#include <gdiplus.h>
#include <memory>

std::unique_ptr<Gdiplus::Bitmap> RenderOverlay(const CpuSample& sample, float scale);
bool PresentOverlay(HWND window, Gdiplus::Bitmap& bitmap, POINT position);
bool SavePreview(const wchar_t* path);
