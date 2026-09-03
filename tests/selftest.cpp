// WinKeyGuard console self-test. Exercises the pure core logic, IME
// enumeration and fullscreen classification without launching the GUI.
#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <cstdio>

#include "common.h"
#include "config.h"
#include "fullscreen.h"
#include "ime_manager.h"
#include "process_info.h"

using namespace wkg;

static int g_fail = 0;

#define CHECK(cond, name)                                        \
    do {                                                         \
        if (cond) {                                              \
            std::printf("[PASS] %s\n", name);                    \
        } else {                                                 \
            std::printf("[FAIL] %s\n", name);                    \
            ++g_fail;                                            \
        }                                                        \
    } while (0)

static void testComboNames() {
    CHECK(comboName(MOD_WIN, 'D', 0) == "Win + D", "comboName Win+D");
    CHECK(comboName(MOD_ALT, VK_TAB, 0) == "Alt + Tab", "comboName Alt+Tab");
    CHECK(comboName(MOD_CTRL | MOD_ALT, VK_F12, 0) == "Ctrl + Alt + F12",
          "comboName Ctrl+Alt+F12");
    CHECK(vkName('A') == "A", "vkName A");
    CHECK(vkName(VK_F4) == "F4", "vkName F4");
    CHECK(scanName(SCAN_TICK) == "`", "scanName tick");
    CHECK(modsName(MOD_WIN | MOD_SHIFT) == "Win + Shift", "modsName Win+Shift");
}

static void testRuleMatching() {
    RuleSnapshot snap;
    snap.winMaster = true;
    snap.winTick = true;
    snap.emergencyMods = MOD_CTRL | MOD_ALT;
    snap.emergencyVk = VK_F12;

    snap.altCtrlBlocks.push_back(HotkeyRule{"alt_tab", "Alt + Tab", MOD_ALT, VK_TAB, 0, true});
    snap.altCtrlBlocks.push_back(HotkeyRule{"ctrl_esc", "Ctrl + Esc", MOD_CTRL, VK_ESCAPE, 0, true});
    snap.altCtrlBlocks.push_back(HotkeyRule{"alt_f4", "Alt + F4", MOD_ALT, VK_F4, 0, true});

    CHECK(matchesGameRule(snap, MOD_ALT, VK_TAB, 0), "match Alt+Tab");
    CHECK(!matchesGameRule(snap, MOD_ALT, 'W', 0), "no match Alt+W (game key)");
    CHECK(!matchesGameRule(snap, MOD_CTRL, 'C', 0), "no match Ctrl+C (game key)");
    CHECK(matchesGameRule(snap, MOD_CTRL, VK_ESCAPE, 0), "match Ctrl+Esc");
    CHECK(matchesGameRule(snap, MOD_ALT, VK_F4, 0), "match Alt+F4");
    CHECK(!matchesGameRule(snap, MOD_SHIFT, 'S', 0), "no match Shift+S");

    // allowed Win replay set
    snap.allowedWinVks.insert(VK_TAB);
    CHECK(isAllowedWinRule(snap, VK_TAB), "allowed Win+Tab replay");
    CHECK(!isAllowedWinRule(snap, 'D'), "Win+D not in allowed set");
}

static void testImeSwitchCombo() {
    CHECK(isImeSwitchCombo(MOD_WIN, VK_SPACE, 0), "IME combo Win+Space");
    CHECK(isImeSwitchCombo(MOD_ALT, VK_LSHIFT, 0), "IME combo Alt+Shift");
    CHECK(isImeSwitchCombo(MOD_SHIFT, VK_LMENU, 0), "IME combo Shift+Alt");
    CHECK(isImeSwitchCombo(MOD_CTRL, VK_SHIFT, 0), "IME combo Ctrl+Shift");
    CHECK(!isImeSwitchCombo(MOD_CTRL, 'C', 0), "not IME combo Ctrl+C");
    CHECK(!isImeSwitchCombo(MOD_ALT, 'W', 0), "not IME combo Alt+W");
}

static void testDefaultRules() {
    const auto rules = defaultRules();
    CHECK(rules.size() >= 25, "default rules count >= 25");
    bool hasWinAlone = false, hasWinD = false, hasAltTab = false, hasWinDigit = false;
    for (const auto& r : rules) {
        if (r.id == "win") hasWinAlone = true;
        if (r.id == "win_d") hasWinD = true;
        if (r.id == "alt_tab") hasAltTab = true;
        if (r.id == "win_1") hasWinDigit = true;
    }
    CHECK(hasWinAlone, "default has Win alone");
    CHECK(hasWinD, "default has Win+D");
    CHECK(hasAltTab, "default has Alt+Tab");
    CHECK(hasWinDigit, "default has Win+1");
}

static void testConfigRoundTrip() {
    const QString tmp = QDir::temp().filePath("wkg_test_config.json");
    QFile::remove(tmp);

    ConfigStore store(tmp);
    store.load();  // creates defaults
    store.data().targets.push_back(TargetEntry{"TestGame.exe", true, false});
    store.data().rules[1].enabled = false;  // disable Win+D
    store.data().ime.capsOffAction = CapsOffAction::SwitchToSpecific;
    store.data().ime.capsOffTarget = "layout:00000409";
    const bool saved = store.save();
    CHECK(saved, "config save");

    ConfigStore store2(tmp);
    const bool loaded = store2.load();
    CHECK(loaded, "config load");
    CHECK(store2.data().targets.size() == 1 && store2.data().targets[0].name == "TestGame.exe",
          "config targets roundtrip");
    CHECK(!store2.data().rules[1].enabled, "config rule toggle roundtrip");
    CHECK(store2.data().ime.capsOffAction == CapsOffAction::SwitchToSpecific,
          "config ime action roundtrip");
    CHECK(store2.data().ime.capsOffTarget == "layout:00000409", "config ime target roundtrip");

    auto snap = store2.data().toSnapshot();
    CHECK(snap->winMaster == true, "snapshot winMaster");
    // Disabling a pure Win+key rule marks it as "allowed" -> replayed via
    // SendInput while the Win master is on.
    CHECK(snap->allowedWinVks.count('D') != 0, "snapshot: disabled Win+D is replayed (allowed)");

    QFile::remove(tmp);
}

static void testFullscreen() {
    const wchar_t* cls = L"WkgSelftestWnd";
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    RegisterClassW(&wc);

    // popup covering the primary monitor -> fullscreen
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    GetMonitorInfoW(mon, &mi);

    HWND popup = CreateWindowExW(0, cls, L"t", WS_POPUP | WS_VISIBLE,
                                 mi.rcMonitor.left, mi.rcMonitor.top,
                                 mi.rcMonitor.right - mi.rcMonitor.left,
                                 mi.rcMonitor.bottom - mi.rcMonitor.top,
                                 nullptr, nullptr, wc.hInstance, nullptr);
    CHECK(popup != nullptr, "create popup fullscreen window");
    if (popup) {
        CHECK(classifyWindowMode(popup) == WindowMode::Fullscreen, "popup spanning monitor = fullscreen");
        DestroyWindow(popup);
    }

    // normal captioned window -> windowed
    HWND normal = CreateWindowExW(0, cls, L"t", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  100, 100, 400, 300,
                                  nullptr, nullptr, wc.hInstance, nullptr);
    CHECK(normal != nullptr, "create normal window");
    if (normal) {
        CHECK(classifyWindowMode(normal) == WindowMode::Windowed, "captioned window = windowed");
        DestroyWindow(normal);
    }
    UnregisterClassW(cls, wc.hInstance);
}

static void testIme() {
    ImeManager ime;
    const auto entries = ime.enumerate();
    std::printf("[INFO] detected %d input methods:\n", (int)entries.size());
    for (const auto& e : entries) {
        std::printf("        - [%s] %s%s\n",
                    e.kind == ImeEntry::Kind::TextService ? "TSF" : "Layout",
                    e.name.toUtf8().constData(), e.isEnglish ? " (EN)" : "");
    }
    CHECK(entries.size() > 0, "IME enumeration non-empty");
    CHECK(ime.hasEnglish(), "English input method available");
    std::printf("[INFO] English id = %s\n", ime.englishId().toUtf8().constData());

    const uint32_t mask = detectImeSwitchMask();
    std::printf("[INFO] IME switch mask = 0x%X (WinSpace=%d AltShift=%d CtrlShift=%d)\n",
                mask, (mask & IME_WINSPACE) ? 1 : 0,
                (mask & IME_ALTSHIFT) ? 1 : 0, (mask & IME_CTRLSHIFT) ? 1 : 0);
    CHECK((mask & IME_WINSPACE) != 0, "Win+Space IME switch active");
}

static void testProcessInfo() {
    const DWORD pid = GetCurrentProcessId();
    CHECK(isCurrentProcessElevated() == isProcessElevated(pid), "elevation self-consistent");
    const auto procs = listRunningProcesses();
    CHECK(procs.size() > 0, "running process enumeration non-empty");
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);  // unbuffered so crash point is visible
    QCoreApplication app(argc, argv);
    std::printf("=== WinKeyGuard self-test ===\n");
    testComboNames();
    testRuleMatching();
    testImeSwitchCombo();
    testDefaultRules();
    testConfigRoundTrip();
    testFullscreen();
    testProcessInfo();
    testIme();

    if (g_fail == 0) {
        std::printf("=== ALL TESTS PASSED ===\n");
        return 0;
    }
    std::printf("=== %d TEST(S) FAILED ===\n", g_fail);
    return 1;
}
