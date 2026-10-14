#include "overlay_renderer.h"
#include <cmath>
#include <cwchar>
#include <vector>
#include <algorithm>
#include <string>

namespace {
std::wstring Value(const std::optional<double>& value, int decimals, const wchar_t* unit) {
    if (!value) return std::wstring(L"--") + unit;
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%.*f%s", decimals, *value, unit);
    return buffer;
}
}

std::wstring FormatOverlayText(const CpuSample& sample) {
    return L"处理器:" + Value(sample.usagePercent, 1, L"%") +
        L" | 频率:" + Value(sample.frequencyGhz, 2, L"GHz") +
        L" | 温度:" + Value(sample.hardware.temperatureC, 0, L"℃") +
        L" | 功耗:" + Value(sample.hardware.packageWatts, 1, L"W") +
        L" | 内存:" + Value(sample.memoryPercent, 1, L"%") +
        L" | 显卡:" + Value(sample.gpuPercent, 1, L"%");
}

std::unique_ptr<Gdiplus::Bitmap> RenderOverlay(const CpuSample& sample, float scale, int maxWidth) {
    if (!std::isfinite(scale) || scale <= 0) return nullptr;
    Gdiplus::FontFamily family(L"Microsoft YaHei UI");
    Gdiplus::FontFamily fallback(L"Microsoft YaHei");
    const auto* chosen = family.IsAvailable() ? &family : (fallback.IsAvailable() ? &fallback : Gdiplus::FontFamily::GenericSansSerif());
    Gdiplus::GraphicsPath path;
    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
    const auto line = FormatOverlayText(sample);
    if (path.AddString(line.c_str(), -1, chosen, Gdiplus::FontStyleBold, 24, Gdiplus::PointF(8, 7), &format) != Gdiplus::Ok)
        return nullptr;
    Gdiplus::RectF bounds;
    path.GetBounds(&bounds);
    const float logicalWidth = bounds.GetRight() + 8;
    const float logicalHeight = std::max(44.0f, bounds.GetBottom() + 8);
    if (maxWidth > 0) scale = std::min(scale, static_cast<float>(maxWidth) / logicalWidth);
    const int width = static_cast<int>(std::ceil(logicalWidth * scale));
    const int height = static_cast<int>(std::ceil(logicalHeight * scale));
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppPARGB);
    if (bitmap->GetLastStatus() != Gdiplus::Ok) return nullptr;
    Gdiplus::Graphics graphics(bitmap.get());
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.ScaleTransform(scale, scale);
    Gdiplus::Pen edge(Gdiplus::Color(230, 15, 23, 35), 2.0f);
    edge.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawPath(&edge, &path);
    Gdiplus::SolidBrush fill(Gdiplus::Color(255, 255, 255, 0));
    graphics.FillPath(&fill, &path);
    return bitmap;
}

bool PresentOverlay(HWND window, Gdiplus::Bitmap& bitmap, POINT position) {
    const int width = static_cast<int>(bitmap.GetWidth());
    const int height = static_cast<int>(bitmap.GetHeight());
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    void* pixels = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    bool success = false;
    if (memory && dib && pixels) {
        Gdiplus::Rect rect(0, 0, width, height);
        Gdiplus::BitmapData data{};
        if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) == Gdiplus::Ok) {
            for (int y = 0; y < height; ++y)
                memcpy(static_cast<BYTE*>(pixels) + static_cast<size_t>(y) * width * 4,
                    static_cast<BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride, static_cast<size_t>(width) * 4);
            bitmap.UnlockBits(&data);
            HGDIOBJ previous = SelectObject(memory, dib);
            SIZE size{width, height};
            POINT origin{};
            BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            success = UpdateLayeredWindow(window, screen, &position, &size, memory, &origin, 0, &blend, ULW_ALPHA) != FALSE;
            SelectObject(memory, previous);
        }
    }
    if (dib) DeleteObject(dib);
    if (memory) DeleteDC(memory);
    if (screen) ReleaseDC(nullptr, screen);
    return success;
}

bool SavePreview(const wchar_t* path) {
    CpuSample sample;
    sample.usagePercent = 12.3;
    sample.frequencyGhz = 4.25;
    sample.hardware.temperatureC = 55;
    sample.hardware.packageWatts = 42.1;
    sample.memoryPercent = 37.5;
    sample.gpuPercent = 18.0;
    auto bitmap = RenderOverlay(sample, 2.0f);
    if (!bitmap) return false;
    UINT count = 0, bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    if (!bytes) return false;
    std::vector<BYTE> buffer(bytes);
    auto encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < count; ++i)
        if (wcscmp(encoders[i].MimeType, L"image/png") == 0)
            return bitmap->Save(path, &encoders[i].Clsid, nullptr) == Gdiplus::Ok;
    return false;
}
