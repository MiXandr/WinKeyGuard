#pragma once
#include <Windows.h>

namespace wkg {

// Detects full-screen / borderless-fullscreen windows without relying solely on
// "maximized". Combines window rect vs monitor rect, window styles and DWM.
enum class WindowMode {
    Unknown = 0,
    Windowed,
    Fullscreen
};

WindowMode classifyWindowMode(HWND hwnd);

// Convenience: true when the window covers its monitor and is borderless/popup.
bool isFullscreen(HWND hwnd);

} // namespace wkg
