#include "overlay_renderer.h"
#include <cmath>
#include <cwchar>
#include <vector>

namespace {
void DrawTextPath(Gdiplus::Graphics& graphics, const wchar_t* text, float x, float y,
                  float emSize, const Gdiplus::Color& color, bool mono = false) {
    Gdiplus::FontFamily family(mono ? L"Consolas" : L"Segoe UI");
    const auto* chosen = family.IsAvailable() ? &family : Gdiplus::FontFamily::GenericSansSerif();
    Gdiplus::GraphicsPath path;
    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
    path.AddString(text, -1, chosen, Gdiplus::FontStyleBold, emSize, Gdiplus::PointF(x, y), &format);
    Gdiplus::Pen edge(Gdiplus::Color(230, 15, 23, 35), 2.5f);
    edge.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawPath(&edge, &path);
    Gdiplus::SolidBrush fill(color);
    graphics.FillPath(&fill, &path);
}
}

std::unique_ptr<Gdiplus::Bitmap> RenderOverlay(const CpuSample& sample, float scale) {
    const int width = static_cast<int>(std::ceil(446.0f * scale));
    const int height = static_cast<int>(std::ceil(44.0f * scale));
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppPARGB);
    if (bitmap->GetLastStatus() != Gdiplus::Ok) return nullptr;
    Gdiplus::Graphics graphics(bitmap.get());
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.ScaleTransform(scale, scale);
    wchar_t usage[32] = L"   --";
    wchar_t frequency[32] = L"   --";
    if (sample.usagePercent) swprintf_s(usage, L"%5.1f", *sample.usagePercent);
    if (sample.frequencyGhz) swprintf_s(frequency, L"%5.2f", *sample.frequencyGhz);
    wchar_t line[96]{};
    swprintf_s(line, L"CPU %s%% | FREQ %s GHz", usage, frequency);
    DrawTextPath(graphics, line, 8, 7, 24, Gdiplus::Color(255, 255, 255, 0), true);
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
