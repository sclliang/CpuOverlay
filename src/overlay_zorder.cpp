#include "overlay_zorder.h"
#include <dwmapi.h>

void MaintainOverlayTopmost(HWND overlay, bool interactive) {
    if (!overlay || interactive || !IsWindowVisible(overlay) || IsIconic(overlay)) return;
    DWORD foreground = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
    if (foreground == GetCurrentProcessId()) return;
    RECT panel{};
    if (!GetWindowRect(overlay, &panel)) return;
    bool obscured = (GetWindowLongPtrW(overlay, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0;
    for (HWND above = GetWindow(overlay, GW_HWNDPREV); above; above = GetWindow(above, GW_HWNDPREV)) {
        if (!IsWindowVisible(above) || IsIconic(above)) continue;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(above, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) continue;
        RECT bounds{}, overlap{};
        if (!GetWindowRect(above, &bounds) || !IntersectRect(&overlap, &panel, &bounds)) continue;
        DWORD process = 0;
        GetWindowThreadProcessId(above, &process);
        // Keep this application's settings and menus above its overlay.
        if (process == GetCurrentProcessId()) return;
        obscured = true;
    }
    if (obscured) SetWindowPos(overlay, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}
