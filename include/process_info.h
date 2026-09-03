#pragma once
#include <Windows.h>

#include <QString>
#include <QVector>

namespace wkg {

// Executable basename (e.g. "Game.exe") of the process that owns `hwnd`.
QString processNameForWindow(HWND hwnd);

// Process id of the window's owning process, 0 on failure.
DWORD processIdForWindow(HWND hwnd);

// True when the process is running with elevated (admin) integrity.
bool isProcessElevated(DWORD pid);

// True when the current process runs elevated.
bool isCurrentProcessElevated();

// A running process, for the "pick from running processes" UI.
struct RunningProcess {
    DWORD pid = 0;
    QString name;  // basename
};

QVector<RunningProcess> listRunningProcesses();

// Restart this application elevated (ShellExecute runas -> UAC prompt).
bool relaunchElevated();

} // namespace wkg
