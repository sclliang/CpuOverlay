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
std::vector<std::wstring> Metrics(const CpuSample& sample, DWORD visibleMetrics) {
    auto battery = [](const BatterySample& s) {
        auto text = Value(s.percent, 0, L"%");
        if (s.state == BatteryState::Disconnected) text += L"(断开)";
        else if (s.state == BatteryState::Stale) text += L"(未更新)";
        else if (s.state == BatteryState::Fresh && s.charging) text += L"(充电)";
        return text;
    };
    const std::vector<std::wstring> all{L"处理器:" + Value(sample.usagePercent, 1, L"%"), L"频率:" + Value(sample.frequencyGhz, 2, L"GHz"),
        L"温度:" + Value(sample.hardware.temperatureC, 0, L"℃"), L"功耗:" + Value(sample.hardware.packageWatts, 1, L"W"),
        L"内存:" + Value(sample.memoryPercent, 1, L"%"), L"显卡:" + Value(sample.gpuPercent, 1, L"%"),
        L"键盘:" + battery(sample.peripherals.keyboard), L"鼠标:" + battery(sample.peripherals.mouse)};
    std::vector<std::wstring> selected;
    for (unsigned i = 0; i < OverlayMetric::Count; ++i) if (visibleMetrics & (1u << i)) selected.push_back(all[i]);
    return selected;
}
}

std::wstring FormatOverlayText(const CpuSample& sample, const OverlaySettings& settings) {
    std::wstring text;
    for (const auto& item : Metrics(sample, settings.visibleMetrics)) { if (!text.empty()) text += L" | "; text += item; }
    return text;
}

std::unique_ptr<Gdiplus::Bitmap> RenderOverlay(const CpuSample& sample, float scale, int maxWidth, int maxHeight,
    const OverlaySettings& settings, bool positioning) {
    if (!std::isfinite(scale) || scale <= 0) return nullptr;
    auto metrics = Metrics(sample, settings.visibleMetrics);
    if (metrics.empty()) {
        if (positioning) metrics.push_back(L"未选择显示项（拖动定位）");
        else {
            auto empty = std::make_unique<Gdiplus::Bitmap>(1, 1, PixelFormat32bppPARGB);
            if (empty->GetLastStatus() != Gdiplus::Ok) return nullptr;
            Gdiplus::Graphics clear(empty.get()); clear.Clear(Gdiplus::Color(0, 0, 0, 0));
            return empty;
        }
    }
    Gdiplus::FontFamily family(settings.font.c_str());
    Gdiplus::FontFamily fallback(L"Microsoft YaHei");
    const auto* chosen = family.IsAvailable() ? &family : (fallback.IsAvailable() ? &fallback : Gdiplus::FontFamily::GenericSansSerif());
    int style = (settings.bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular) | (settings.italic ? Gdiplus::FontStyleItalic : 0);
    if (!chosen->IsStyleAvailable(style)) style = chosen->IsStyleAvailable(Gdiplus::FontStyleRegular) ? Gdiplus::FontStyleRegular : Gdiplus::FontStyleBold;
    const float em = static_cast<float>(std::clamp(settings.fontPoints, 8, 96)) * 96.0f / 72.0f;
    const float lineHeight = chosen->GetEmHeight(style) ? em * chosen->GetLineSpacing(style) / chosen->GetEmHeight(style) : em * 1.5f;
    Gdiplus::GraphicsPath path;
    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
    float x = 0, y = 0;
    for (size_t i = 0; i < metrics.size(); ++i) {
        Gdiplus::GraphicsPath item;
        if (item.AddString(metrics[i].c_str(), -1, chosen, style, em, Gdiplus::PointF(x, y), &format) != Gdiplus::Ok) return nullptr;
        Gdiplus::RectF itemBounds; item.GetBounds(&itemBounds); path.AddPath(&item, FALSE);
        if (settings.vertical) y += lineHeight + static_cast<float>(settings.gap);
        else if (i + 1 < metrics.size()) {
            x = itemBounds.GetRight() + static_cast<float>(settings.gap) + 2;
            Gdiplus::GraphicsPath separator;
            separator.AddString(L"|", 1, chosen, style, em, Gdiplus::PointF(x, y), &format);
            separator.GetBounds(&itemBounds); path.AddPath(&separator, FALSE);
            x = itemBounds.GetRight() + static_cast<float>(settings.gap) + 2;
        }
    }
    Gdiplus::RectF bounds;
    path.GetBounds(&bounds);
    const float padding = 6.0f + static_cast<float>(settings.outlineWidth);
    Gdiplus::Matrix translate;
    translate.Translate(padding - bounds.X, padding - bounds.Y); path.Transform(&translate);
    const float logicalWidth = bounds.Width + padding * 2;
    const float logicalHeight = std::max(44.0f, bounds.Height + padding * 2);
    if (maxWidth > 0) scale = std::min(scale, static_cast<float>(maxWidth) / logicalWidth);
    if (maxHeight > 0) scale = std::min(scale, static_cast<float>(maxHeight) / logicalHeight);
    const int width = static_cast<int>(std::ceil(logicalWidth * scale));
    const int height = static_cast<int>(std::ceil(logicalHeight * scale));
    if (width < 1 || height < 1 || static_cast<uint64_t>(width) * height > 32 * 1024 * 1024) return nullptr;
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppPARGB);
    if (bitmap->GetLastStatus() != Gdiplus::Ok) return nullptr;
    {
    Gdiplus::Graphics graphics(bitmap.get());
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.ScaleTransform(scale, scale);
    if (positioning) {
        Gdiplus::SolidBrush background(Gdiplus::Color(245, 246, 248, 251));
        graphics.FillRectangle(&background, 0.0f, 0.0f, logicalWidth, logicalHeight);
    }
    const BYTE alpha = 255;
    Gdiplus::Pen edge(Gdiplus::Color(alpha, GetRValue(settings.outlineColor), GetGValue(settings.outlineColor), GetBValue(settings.outlineColor)), static_cast<float>(settings.outlineWidth));
    edge.SetLineJoin(Gdiplus::LineJoinRound);
    if (settings.outlineWidth > 0) graphics.DrawPath(&edge, &path);
    Gdiplus::SolidBrush fill(Gdiplus::Color(alpha, GetRValue(settings.color), GetGValue(settings.color), GetBValue(settings.color)));
    graphics.FillPath(&fill, &path);
    }
    const int opacity = std::clamp(settings.opacity, 10, 100);
    if (opacity < 100) {
        Gdiplus::Rect region(0, 0, width, height);
        Gdiplus::BitmapData data{};
        if (bitmap->LockBits(&region, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &data) != Gdiplus::Ok) return nullptr;
        // Apply opacity once to the completed, premultiplied image. Applying it
        // separately to fill and outline makes their overlapping pixels darker.
        for (int rowIndex = 0; rowIndex < height; ++rowIndex) {
            auto row = static_cast<BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(rowIndex) * data.Stride;
            for (int channel = 0; channel < width * 4; ++channel) row[channel] = static_cast<BYTE>(row[channel] * opacity / 100);
        }
        bitmap->UnlockBits(&data);
    }
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

CpuSample PreviewSample() {
    CpuSample sample;
    sample.usagePercent = 12.3;
    sample.frequencyGhz = 4.25;
    sample.hardware.temperatureC = 55;
    sample.hardware.packageWatts = 42.1;
    sample.memoryPercent = 37.5;
    sample.gpuPercent = 18.0;
    UpdateBattery(sample.peripherals.keyboard, BatteryProtocol::Reading{14, false}, true, GetTickCount64(), 0);
    UpdateBattery(sample.peripherals.mouse, BatteryProtocol::Reading{48.6, false}, true, GetTickCount64(), 0);
    return sample;
}

bool SavePreview(const wchar_t* path) {
    auto bitmap = RenderOverlay(PreviewSample(), 2.0f);
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
