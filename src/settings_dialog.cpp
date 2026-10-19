#include "settings_dialog.h"
#include "overlay_renderer.h"
#include "../resources/settings_ids.h"
#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>
#include <set>
#include <map>
#include <cmath>

namespace {
struct Dialog {
    OverlaySettings current;
    SettingsCallbacks callbacks;
    std::vector<MonitorEntry> monitors;
    HBRUSH background = CreateSolidBrush(RGB(248, 250, 252));
    std::vector<HFONT> scaledFonts;
    bool filling = false;
    ~Dialog() { for (const auto font : scaledFonts) DeleteObject(font); DeleteObject(background); }
};

std::wstring Text(HWND window, int id) {
    wchar_t buffer[256]{};
    GetDlgItemTextW(window, id, buffer, ARRAYSIZE(buffer));
    return buffer;
}
void ColorText(HWND window, int id, COLORREF color) {
    wchar_t text[8]{};
    swprintf_s(text, L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    SetDlgItemTextW(window, id, text);
}
bool ReadColor(HWND window, int id, COLORREF& color) {
    auto text = Text(window, id);
    if (!text.empty() && text[0] == L'#') text.erase(0, 1);
    if (text.size() != 6 || text.find_first_not_of(L"0123456789abcdefABCDEF") != std::wstring::npos) return false;
    const auto value = wcstoul(text.c_str(), nullptr, 16);
    color = RGB(value >> 16, (value >> 8) & 255, value & 255);
    return true;
}
void PositionControls(HWND window) {
    const bool custom = SendDlgItemMessageW(window, IDC_POSITION, CB_GETCURSEL, 0, 0) == 5;
    EnableWindow(GetDlgItem(window, IDC_X), custom);
    EnableWindow(GetDlgItem(window, IDC_Y), custom);
    EnableWindow(GetDlgItem(window, IDC_MARGIN), !custom);
}

void FitToWorkArea(HWND window, Dialog& d, const RECT& work) {
    RECT outer{}, client{}; GetWindowRect(window, &outer); GetClientRect(window, &client);
    const int frameWidth = outer.right - outer.left - client.right;
    const int frameHeight = outer.bottom - outer.top - client.bottom;
    const double scale = std::min({1.0, static_cast<double>(work.right - work.left - frameWidth - 24) / client.right,
        static_cast<double>(work.bottom - work.top - frameHeight - 24) / client.bottom});
    if (scale <= 0 || scale >= 1) return;
    auto scaled = [scale](int value) { return static_cast<int>(std::lround(value * scale)); };
    std::map<HFONT, HFONT> fonts;
    for (HWND control = GetWindow(window, GW_CHILD); control; control = GetWindow(control, GW_HWNDNEXT)) {
        RECT rect{}; GetWindowRect(control, &rect); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rect), 2);
        int height = rect.bottom - rect.top;
        wchar_t type[32]{}; GetClassNameW(control, type, ARRAYSIZE(type));
        if (_wcsicmp(type, L"ComboBox") == 0) {
            RECT dropped{}; SendMessageW(control, CB_GETDROPPEDCONTROLRECT, 0, reinterpret_cast<LPARAM>(&dropped));
            height = std::max(height, static_cast<int>(dropped.bottom - dropped.top));
        }
        auto original = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
        if (original && !fonts.count(original)) {
            LOGFONTW font{};
            if (GetObjectW(original, sizeof(font), &font)) {
                font.lfHeight = scaled(font.lfHeight); font.lfWidth = 0;
                if (auto resized = CreateFontIndirectW(&font)) { fonts[original] = resized; d.scaledFonts.push_back(resized); }
            }
        }
        if (fonts.count(original)) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(fonts[original]), FALSE);
        SetWindowPos(control, nullptr, scaled(rect.left), scaled(rect.top), scaled(rect.right - rect.left), scaled(height), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    SetWindowPos(window, nullptr, 0, 0, scaled(client.right) + frameWidth, scaled(client.bottom) + frameHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Fill(HWND window, Dialog& d) {
    d.filling = true;
    const auto& s = d.current;
    WORD modifiers = 0;
    if (s.hotkeyModifiers & MOD_ALT) modifiers |= HOTKEYF_ALT;
    if (s.hotkeyModifiers & MOD_CONTROL) modifiers |= HOTKEYF_CONTROL;
    if (s.hotkeyModifiers & MOD_SHIFT) modifiers |= HOTKEYF_SHIFT;
    if ((s.hotkeyKey >= VK_PRIOR && s.hotkeyKey <= VK_DOWN) || s.hotkeyKey == VK_INSERT || s.hotkeyKey == VK_DELETE) modifiers |= HOTKEYF_EXT;
    SendDlgItemMessageW(window, IDC_HOTKEY, HKM_SETHOTKEY, MAKEWORD(s.hotkeyKey, modifiers), 0);
    CheckDlgButton(window, IDC_WINKEY, s.hotkeyModifiers & MOD_WIN ? BST_CHECKED : BST_UNCHECKED);
    for (unsigned i = 0; i < OverlayMetric::Count; ++i)
        CheckDlgButton(window, IDC_METRIC_CPU + i, s.visibleMetrics & (1u << i) ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(window, IDC_LAYOUT, CB_SETCURSEL, s.vertical, 0);
    SendDlgItemMessageW(window, IDC_POSITION, CB_SETCURSEL, s.position, 0);
    int display = 0;
    for (size_t i = 0; i < d.monitors.size(); ++i) if (s.monitor == d.monitors[i].info.szDevice) display = static_cast<int>(i + 1);
    SendDlgItemMessageW(window, IDC_MONITOR, CB_SETCURSEL, display, 0);
    SetDlgItemInt(window, IDC_X, s.x, FALSE); SetDlgItemInt(window, IDC_Y, s.y, FALSE); SetDlgItemInt(window, IDC_MARGIN, s.margin, FALSE);
    SetDlgItemTextW(window, IDC_FONT, s.font.c_str()); SetDlgItemInt(window, IDC_POINTS, s.fontPoints, FALSE);
    CheckDlgButton(window, IDC_BOLD, s.bold ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, IDC_ITALIC, s.italic ? BST_CHECKED : BST_UNCHECKED);
    ColorText(window, IDC_COLOR, s.color); ColorText(window, IDC_OUTLINE_COLOR, s.outlineColor);
    SetDlgItemInt(window, IDC_OPACITY, s.opacity, FALSE); SetDlgItemInt(window, IDC_GAP, s.gap, FALSE);
    SetDlgItemInt(window, IDC_OUTLINE, s.outlineWidth, FALSE);
    PositionControls(window);
    d.filling = false;
    InvalidateRect(GetDlgItem(window, IDC_PREVIEW), nullptr, TRUE);
}

bool Read(HWND window, Dialog& d, OverlaySettings& s, bool report) {
    s = d.current;
    auto fail = [window, report](const std::wstring& text) {
        if (report) MessageBoxW(window, text.c_str(), L"请检查设置", MB_OK | MB_ICONWARNING);
        return false;
    };
    const WORD hotkey = static_cast<WORD>(SendDlgItemMessageW(window, IDC_HOTKEY, HKM_GETHOTKEY, 0, 0));
    s.hotkeyKey = LOBYTE(hotkey); s.hotkeyModifiers = 0;
    if (HIBYTE(hotkey) & HOTKEYF_ALT) s.hotkeyModifiers |= MOD_ALT;
    if (HIBYTE(hotkey) & HOTKEYF_CONTROL) s.hotkeyModifiers |= MOD_CONTROL;
    if (HIBYTE(hotkey) & HOTKEYF_SHIFT) s.hotkeyModifiers |= MOD_SHIFT;
    if (IsDlgButtonChecked(window, IDC_WINKEY) == BST_CHECKED) s.hotkeyModifiers |= MOD_WIN;
    s.position = static_cast<DWORD>(SendDlgItemMessageW(window, IDC_POSITION, CB_GETCURSEL, 0, 0));
    s.vertical = static_cast<DWORD>(SendDlgItemMessageW(window, IDC_LAYOUT, CB_GETCURSEL, 0, 0));
    const auto monitor = SendDlgItemMessageW(window, IDC_MONITOR, CB_GETCURSEL, 0, 0);
    if (monitor < 0 || monitor > static_cast<LRESULT>(d.monitors.size())) return fail(L"请选择显示器。");
    s.monitor = monitor == 0 ? L"" : d.monitors[static_cast<size_t>(monitor - 1)].info.szDevice;
    auto number = [window](int id, int& value) {
        const auto text = Text(window, id);
        if (text.empty() || text.size() > 5 || text.find_first_not_of(L"0123456789") != std::wstring::npos) return false;
        BOOL valid = FALSE; value = static_cast<int>(GetDlgItemInt(window, id, &valid, FALSE)); return valid != FALSE;
    };
    if (!number(IDC_X, s.x) || !number(IDC_Y, s.y) || !number(IDC_MARGIN, s.margin) || !number(IDC_POINTS, s.fontPoints) ||
        !number(IDC_OPACITY, s.opacity) || !number(IDC_GAP, s.gap) || !number(IDC_OUTLINE, s.outlineWidth)) return fail(L"请在数值输入框中填写完整的非负整数。");
    if (!ReadColor(window, IDC_COLOR, s.color) || !ReadColor(window, IDC_OUTLINE_COLOR, s.outlineColor)) return fail(L"颜色格式应为 #RRGGBB，例如 #FFFF00。");
    s.font = Text(window, IDC_FONT);
    s.bold = IsDlgButtonChecked(window, IDC_BOLD) == BST_CHECKED;
    s.italic = IsDlgButtonChecked(window, IDC_ITALIC) == BST_CHECKED;
    s.visibleMetrics = 0;
    for (unsigned i = 0; i < OverlayMetric::Count; ++i)
        if (IsDlgButtonChecked(window, IDC_METRIC_CPU + i) == BST_CHECKED) s.visibleMetrics |= 1u << i;
    std::wstring error;
    if (!ValidateOverlaySettings(s, error)) return fail(error);
    Gdiplus::FontFamily font(s.font.c_str());
    if (!font.IsAvailable()) return fail(L"此字体不可用，请从字体列表中选择已安装的字体。");
    const int style = (s.bold ? Gdiplus::FontStyleBold : 0) | (s.italic ? Gdiplus::FontStyleItalic : 0);
    if (!font.IsStyleAvailable(style)) return fail(L"此字体不提供所选粗细或斜体，请修改字形或更换字体。");
    return true;
}

void Choose(HWND window, int id) {
    COLORREF color = 0; ReadColor(window, id, color);
    static COLORREF custom[16]{};
    CHOOSECOLORW picker{sizeof(picker)};
    picker.hwndOwner = window; picker.rgbResult = color; picker.lpCustColors = custom;
    picker.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&picker)) { ColorText(window, id, picker.rgbResult); InvalidateRect(GetDlgItem(window, IDC_PREVIEW), nullptr, TRUE); }
}

INT_PTR CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        d = reinterpret_cast<Dialog*>(lParam); SetWindowLongPtrW(window, DWLP_USER, lParam);
        SendDlgItemMessageW(window, IDC_HOTKEY, HKM_SETRULES, 0, 0);
        for (int id : {IDC_COLOR, IDC_OUTLINE_COLOR}) SendDlgItemMessageW(window, id, EM_SETLIMITTEXT, 7, 0);
        for (int id : {IDC_X, IDC_Y, IDC_MARGIN, IDC_POINTS, IDC_GAP, IDC_OUTLINE, IDC_OPACITY}) SendDlgItemMessageW(window, id, EM_SETLIMITTEXT, 5, 0);
        SendDlgItemMessageW(window, IDC_FONT, CB_LIMITTEXT, LF_FACESIZE - 1, 0);
        auto add = [window](int id, const wchar_t* text) { SendDlgItemMessageW(window, id, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text)); };
        add(IDC_LAYOUT, L"横向显示"); add(IDC_LAYOUT, L"纵向显示");
        for (const auto text : {L"右上角", L"左上角", L"右下角", L"左下角", L"顶部居中", L"自定义坐标"}) add(IDC_POSITION, text);
        add(IDC_MONITOR, L"跟随主显示器");
        d->monitors = ListMonitors();
        for (const auto& monitor : d->monitors) {
            const auto& info = monitor.info;
            const std::wstring label = std::wstring(info.szDevice) + L"  " + std::to_wstring(info.rcMonitor.right - info.rcMonitor.left) + L" × " + std::to_wstring(info.rcMonitor.bottom - info.rcMonitor.top);
            add(IDC_MONITOR, label.c_str());
        }
        std::set<std::wstring> fonts;
        LOGFONTW query{}; query.lfCharSet = DEFAULT_CHARSET;
        HDC dc = GetDC(window);
        EnumFontFamiliesExW(dc, &query, [](const LOGFONTW* font, const TEXTMETRICW*, DWORD type, LPARAM context) -> int {
            if ((type & TRUETYPE_FONTTYPE) && font->lfFaceName[0] != L'@') reinterpret_cast<std::set<std::wstring>*>(context)->insert(font->lfFaceName);
            return 1;
        }, reinterpret_cast<LPARAM>(&fonts), 0);
        ReleaseDC(window, dc);
        for (const auto& font : fonts) add(IDC_FONT, font.c_str());
        Fill(window, *d);
        POINT cursor{}; GetCursorPos(&cursor);
        MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
        FitToWorkArea(window, *d, monitor.rcWork);
        RECT rect{}; GetWindowRect(window, &rect);
        SetWindowPos(window, nullptr, monitor.rcWork.left + std::max(0L, (monitor.rcWork.right - monitor.rcWork.left - (rect.right - rect.left)) / 2),
            monitor.rcWork.top + std::max(0L, (monitor.rcWork.bottom - monitor.rcWork.top - (rect.bottom - rect.top)) / 2), 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        if (d->callbacks.opened) d->callbacks.opened(window);
        return TRUE;
    }
    if (!d) return FALSE;
    switch (message) {
    case WM_CTLCOLORDLG: return reinterpret_cast<INT_PTR>(d->background);
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), RGB(30, 41, 59));
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(248, 250, 252));
        return reinterpret_cast<INT_PTR>(d->background);
    case WM_DRAWITEM: {
        const auto item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item->CtlID != IDC_PREVIEW) break;
        FillRect(item->hDC, &item->rcItem, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        OverlaySettings draft;
        if (!Read(window, *d, draft, false)) draft = d->current;
        const auto bitmap = RenderOverlay(PreviewSample(), static_cast<float>(GetDpiForWindow(window)) / 96.0f,
            item->rcItem.right - 8, item->rcItem.bottom - 8, draft);
        if (bitmap) {
            Gdiplus::Graphics graphics(item->hDC);
            graphics.DrawImage(bitmap.get(), static_cast<int>((item->rcItem.right - bitmap->GetWidth()) / 2),
                static_cast<int>((item->rcItem.bottom - bitmap->GetHeight()) / 2));
        }
        return TRUE;
    }
    case kPositionPicked: {
        const auto picked = reinterpret_cast<const OverlaySettings*>(lParam);
        if (picked) {
            d->current.position = 5; d->current.x = picked->x; d->current.y = picked->y; d->current.monitor = picked->monitor;
            SendDlgItemMessageW(window, IDC_POSITION, CB_SETCURSEL, 5, 0);
            SetDlgItemInt(window, IDC_X, picked->x, FALSE); SetDlgItemInt(window, IDC_Y, picked->y, FALSE);
            for (size_t i = 0; i < d->monitors.size(); ++i)
                if (picked->monitor == d->monitors[i].info.szDevice) SendDlgItemMessageW(window, IDC_MONITOR, CB_SETCURSEL, i + 1, 0);
            PositionControls(window);
            SetDlgItemTextW(window, IDC_SETTINGS_STATUS, L"位置已填入；点击“应用”或“确定”保存。");
        }
        SetForegroundWindow(window); return TRUE;
    }
    case WM_CLOSE: EndDialog(window, IDCANCEL); return TRUE;
    case WM_COMMAND: {
        const int id = LOWORD(wParam), notification = HIWORD(wParam);
        if (id == IDCANCEL) { EndDialog(window, IDCANCEL); return TRUE; }
        if (id == IDC_CHOOSE_COLOR || id == IDC_CHOOSE_OUTLINE) { Choose(window, id == IDC_CHOOSE_COLOR ? IDC_COLOR : IDC_OUTLINE_COLOR); return TRUE; }
        if (id == IDC_RESET) {
            d->current = {}; Fill(window, *d); SetDlgItemTextW(window, IDC_SETTINGS_STATUS, L"已恢复默认值；点击“应用”或“确定”保存。"); return TRUE;
        }
        if (id == IDOK || id == IDC_APPLY || id == IDC_DESKTOP_PREVIEW || id == IDC_PICK_POSITION) {
            OverlaySettings candidate;
            if (!Read(window, *d, candidate, true)) return TRUE;
            if (id == IDC_PICK_POSITION) {
                d->callbacks.pickPosition(window, candidate);
                SetDlgItemTextW(window, IDC_SETTINGS_STATUS, L"拖动桌面上的监控文字，松开后填入坐标；取消可撤销。"); return TRUE;
            }
            if (id == IDC_DESKTOP_PREVIEW) {
                d->callbacks.preview(candidate);
                SetDlgItemTextW(window, IDC_SETTINGS_STATUS, L"正在预览到桌面；预览不保存快捷键和外观设置。"); return TRUE;
            }
            std::wstring error;
            if (!d->callbacks.apply(candidate, error)) { MessageBoxW(window, error.c_str(), L"设置未保存", MB_OK | MB_ICONWARNING); return TRUE; }
            d->current = candidate;
            SetDlgItemTextW(window, IDC_SETTINGS_STATUS, L"设置已保存，重启后仍然有效。");
            if (id == IDOK) EndDialog(window, IDOK);
            return TRUE;
        }
        if (!d->filling && (notification == EN_CHANGE || notification == CBN_SELCHANGE || notification == CBN_EDITCHANGE || notification == BN_CLICKED)) {
            PositionControls(window);
            InvalidateRect(GetDlgItem(window, IDC_PREVIEW), nullptr, TRUE);
        }
        break;
    }
    }
    return FALSE;
}
}

void ShowSettingsDialog(HWND owner, HINSTANCE instance, const OverlaySettings& settings, const SettingsCallbacks& callbacks) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_HOTKEY_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    Dialog dialog; dialog.current = settings; dialog.callbacks = callbacks;
    if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, Procedure, reinterpret_cast<LPARAM>(&dialog)) == -1)
        MessageBoxW(owner, L"无法打开设置窗口。", L"CpuOverlay", MB_OK | MB_ICONERROR);
}

bool CaptureRegisteredShortcut(HWND dialog, const OverlaySettings& settings) {
    const HWND input = GetDlgItem(dialog, IDC_HOTKEY);
    if (!input || GetFocus() != input) return false;
    // Windows sends an already registered combination to WM_HOTKEY instead of
    // the focused editor. Put it back into the editor rather than toggling.
    WORD modifiers = 0;
    if (settings.hotkeyModifiers & MOD_ALT) modifiers |= HOTKEYF_ALT;
    if (settings.hotkeyModifiers & MOD_CONTROL) modifiers |= HOTKEYF_CONTROL;
    if (settings.hotkeyModifiers & MOD_SHIFT) modifiers |= HOTKEYF_SHIFT;
    SendMessageW(input, HKM_SETHOTKEY, MAKEWORD(settings.hotkeyKey, modifiers), 0);
    CheckDlgButton(dialog, IDC_WINKEY, settings.hotkeyModifiers & MOD_WIN ? BST_CHECKED : BST_UNCHECKED);
    return true;
}
