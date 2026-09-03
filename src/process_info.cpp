#include "process_info.h"

#include <psapi.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <QFileInfo>

namespace wkg {

static QString baseNameFromPath(const QString& full) {
    const int idx = full.lastIndexOf('\\');
    return (idx >= 0) ? full.mid(idx + 1) : full;
}

DWORD processIdForWindow(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid;
}

QString processNameForWindow(HWND hwnd) {
    DWORD pid = processIdForWindow(hwnd);
    if (!pid) return QString();

    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hp) return QString();

    wchar_t path[MAX_PATH] = {0};
    DWORD len = MAX_PATH;
    QString name;
    if (QueryFullProcessImageNameW(hp, 0, path, &len)) {
        name = baseNameFromPath(QString::fromWCharArray(path));
    }
    CloseHandle(hp);
    return name;
}

bool isProcessElevated(DWORD pid) {
    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hp) return false;
    HANDLE tok = nullptr;
    bool elevated = false;
    if (OpenProcessToken(hp, TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION elev{};
        DWORD size = 0;
        if (GetTokenInformation(tok, TokenElevation, &elev, sizeof(elev), &size))
            elevated = elev.TokenIsElevated != 0;
        CloseHandle(tok);
    }
    CloseHandle(hp);
    return elevated;
}

bool isCurrentProcessElevated() {
    return isProcessElevated(GetCurrentProcessId());
}

QVector<RunningProcess> listRunningProcesses() {
    QVector<RunningProcess> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            RunningProcess rp;
            rp.pid = pe.th32ProcessID;
            rp.name = QString::fromWCharArray(pe.szExeFile);
            out.push_back(rp);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

bool relaunchElevated() {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) == TRUE;
}

} // namespace wkg
