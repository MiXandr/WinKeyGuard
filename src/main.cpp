#include <QApplication>
#include <QLocale>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QTranslator>

#include <windows.h>

#include "config.h"
#include "guard_controller.h"
#include "main_window.h"

using namespace wkg;

static void enablePerMonitorDpiAwareness() {
    // Accurate fullscreen detection under DPI scaling needs physical pixels.
    using SetCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto* user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto setCtx = (SetCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (setCtx) {
            if (setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
            setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);  // ignore failure
            return;
        }
    }
    SetProcessDPIAware();
}

static QString resolveLanguage(const QString& setting) {
    if (setting == "en") return "en";
    if (setting == "zh") return "zh";
    // "system": follow OS UI language.
    return QLocale::system().language() == QLocale::Chinese ? "zh" : "en";
}

int main(int argc, char** argv) {
    enablePerMonitorDpiAwareness();

    // Single instance guard (avoid double-hooking the keyboard).
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"WinKeyGuard_SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        QApplication app(argc, argv);
        QMessageBox::information(nullptr, "WinKeyGuard",
                                 wgTr("WinKeyGuard is already running."));
        return 0;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName("WinKeyGuard");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setApplicationDisplayName("WinKeyGuard");
    QApplication::setOrganizationName("WinKeyGuard");
    QApplication::setWindowIcon(QIcon(":/icon.ico"));
    QApplication::setQuitOnLastWindowClosed(false);

    ConfigStore store(defaultConfigPath());
    if (!store.load()) {
        QMessageBox::warning(nullptr, "WinKeyGuard",
                             wgTr("Could not read or create config.json."));
    }

    // Install the translation for the resolved language (English is the source
    // language; only Chinese needs a .qm). Must stay alive for the app lifetime.
    QTranslator translator;
    if (resolveLanguage(store.data().language) == "zh" &&
        translator.load(":/i18n/wkg_zh_CN.qm")) {
        app.installTranslator(&translator);
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::warning(nullptr, "WinKeyGuard",
                             wgTr("System tray is not available."));
    }

    GuardController controller(store);
    MainWindow window(store, controller);

    if (!controller.start()) {
        QMessageBox::critical(nullptr, "WinKeyGuard",
                              wgTr("The keyboard hook failed to start."));
        return 1;
    }

    window.show();
    const int rc = app.exec();

    controller.stop();
    store.save();
    if (mutex) CloseHandle(mutex);
    return rc;
}
