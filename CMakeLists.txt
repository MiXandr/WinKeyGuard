cmake_minimum_required(VERSION 3.21)
project(WinKeyGuard VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# --- main GUI application ---------------------------------------------------
add_executable(WinKeyGuard WIN32
    src/main.cpp
    src/common.cpp
    src/config.cpp
    src/fullscreen.cpp
    src/process_info.cpp
    src/ime_manager.cpp
    src/keyboard_hook.cpp
    src/guard_controller.cpp
    src/main_window.cpp
    include/common.h
    include/config.h
    include/fullscreen.h
    include/process_info.h
    include/ime_manager.h
    include/keyboard_hook.h
    include/guard_controller.h
    include/main_window.h
    resources/resources.qrc
    resources/app.rc
    resources/app.manifest
)

target_include_directories(WinKeyGuard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_definitions(WinKeyGuard PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(WinKeyGuard PRIVATE
    Qt6::Widgets
    user32
    gdi32
    shell32
    dwmapi
    ole32
    advapi32
)

# --- console self-test (core logic + IME enumeration + fullscreen) ----------
add_executable(wkg_selftest
    tests/selftest.cpp
    src/common.cpp
    src/config.cpp
    src/fullscreen.cpp
    src/process_info.cpp
    src/ime_manager.cpp
)

target_include_directories(wkg_selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_definitions(wkg_selftest PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(wkg_selftest PRIVATE
    Qt6::Core
    user32
    gdi32
    shell32
    dwmapi
    ole32
    advapi32
)
