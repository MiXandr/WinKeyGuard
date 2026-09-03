#include "fullscreen.h"

#include <dwmapi.h>

namespace wkg {

WindowMode classifyWindowMode(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return WindowMode::Unknown;
    if (!IsWindowVisible(hwnd)) return WindowMode::Windowed;

    // Skip cloaked (minimized to other virtual desktop, UWP suspended, etc.).
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return WindowMode::Windowed;

    RECT wr{};
    if (!GetWindowRect(hwnd, &wr)) return WindowMode::Unknown;

    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return WindowMode::Unknown;

    const RECT& mr = mi.rcMonitor;  // full monitor (includes taskbar area)
    // Tolerance for DPI rounding at the edges.
    const LONG tol = 2;
    const bool coversMonitor =
        wr.left <= mr.left + tol && wr.top <= mr.top + tol &&
        wr.right >= mr.right - tol && wr.bottom >= mr.bottom - tol;

    if (!coversMonitor) return WindowMode::Windowed;

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const bool popup = (style & WS_POPUP) != 0;
    const bool hasFrame = (style & (WS_CAPTION | WS_THICKFRAME | WS_SYSMENU)) != 0;

    // Borderless / exclusive fullscreen: a popup with no window frame that
    // spans the whole monitor. A plain maximized window still has a caption
    // (and spans only the work area), so it is correctly treated as windowed.
    if (popup && !hasFrame) return WindowMode::Fullscreen;

    // Some engines remove the frame but keep WS_OVERLAPPED styles; any frame-
    // less window covering the monitor is effectively fullscreen.
    if (!hasFrame) return WindowMode::Fullscreen;

    return WindowMode::Windowed;
}

bool isFullscreen(HWND hwnd) {
    return classifyWindowMode(hwnd) == WindowMode::Fullscreen;
}

} // namespace wkg
