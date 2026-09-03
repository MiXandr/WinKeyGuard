#pragma once
// WinKeyGuard - shared core types, constants and lock-free shared state.
// This header is Qt-free so it can be used by the console self-test as well.

#include <Windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace wkg {

// Windows headers define MOD_ALT / MOD_CONTROL / MOD_SHIFT / MOD_WIN as
// RegisterHotKey flags; undefine them so we can define our own bitmask values.
#ifdef MOD_ALT
#undef MOD_ALT
#endif
#ifdef MOD_CONTROL
#undef MOD_CONTROL
#endif
#ifdef MOD_SHIFT
#undef MOD_SHIFT
#endif
#ifdef MOD_WIN
#undef MOD_WIN
#endif

// ---- modifier bitmask -----------------------------------------------------
constexpr uint32_t MOD_WIN   = 1u << 0;
constexpr uint32_t MOD_CTRL  = 1u << 1;
constexpr uint32_t MOD_ALT   = 1u << 2;
constexpr uint32_t MOD_SHIFT = 1u << 3;

// ---- IME switch hotkey mask bits ------------------------------------------
constexpr uint32_t IME_WINSPACE  = 1u << 0;
constexpr uint32_t IME_ALTSHIFT  = 1u << 1;
constexpr uint32_t IME_CTRLSHIFT = 1u << 2;
// Backtick-alone toggle exists on some layouts; we detect but do not block it
// (blocking a lone ` would break games using the console key).
constexpr uint32_t IME_GRAVE     = 1u << 3;

// VK_TAB / VK_SPACE / VK_F1 / VK_F4 / VK_F12 / VK_ESCAPE etc. are already
// provided as Windows macros; here we only alias the "Esc" name we use.
constexpr uint32_t VK_ESC = VK_ESCAPE;

// Physical scan code of the "`/~" key (below Esc, left of "1").
constexpr uint32_t SCAN_TICK = 0x29;

// ---- rule model -----------------------------------------------------------
// A single "Modifier + Key" interception rule. mods is the exact required
// modifier bitmask. vk==0 && scan==0 means "modifier alone" (only used for Win).
struct HotkeyRule {
    std::string id;       // stable id, e.g. "win_d"
    std::string display;  // "Win + D"
    uint32_t mods = 0;
    uint32_t vk = 0;      // main key virtual-key code (0 = none)
    uint32_t scan = 0;    // if != 0, match by physical scan code instead of vk
    bool enabled = true;
};

// Immutable snapshot handed to the keyboard hook thread. Rebuilt (on the GUI
// thread) whenever configuration changes, then published atomically.
struct RuleSnapshot {
    bool winMaster = true;   // "Win alone" rule enabled -> swallow Win keydown
    bool winTick = true;     // Win + ` passthrough to native Start menu

    uint32_t emergencyMods = MOD_CTRL | MOD_ALT;
    uint32_t emergencyVk = VK_F12;

    // Non-Win combos (Alt+Tab, Ctrl+Esc, custom ...) -> block the chord key.
    std::vector<HotkeyRule> altCtrlBlocks;
    // Pure "Win + key" rules that are DISABLED (user wants them to reach
    // Windows). These are replayed through SendInput while the Win master is on.
    std::unordered_set<uint32_t> allowedWinVks;

    bool winAloneEnabled() const { return winMaster; }
};

// Result of the "record shortcut" dialog.
struct RecordedCombo {
    uint32_t mods = 0;
    uint32_t vk = 0;
    uint32_t scan = 0;
    bool cancelled = false;
};

// ---- cross-thread shared state --------------------------------------------
// The hook thread only reads atomics (cheap). The controller (GUI thread)
// writes them. The rules pointer is swapped with std::atomic_load/store.
struct SharedState {
    std::atomic<bool> protectionActive{false};  // game protection engaged
    std::atomic<bool> imeLockActive{false};     // Caps Lock ON + IME link enabled
    std::atomic<bool> emergencySuspend{false};  // toggled by Ctrl+Alt+F12
    std::atomic<bool> passthrough{false};       // Win+` native UI active
    std::atomic<bool> recordActive{false};
    std::atomic<bool> recordDone{false};
    std::atomic<bool> targetElevated{false};    // foreground target runs elevated
    // Which Windows input-language/IME switch hotkeys are actually configured.
    std::atomic<uint32_t> imeSwitchMask{IME_WINSPACE | IME_ALTSHIFT | IME_CTRLSHIFT};

    std::mutex recordMutex;
    RecordedCombo recorded{};

    std::shared_ptr<const RuleSnapshot> rules;  // use std::atomic_load/store

    SharedState() {
        rules = std::make_shared<RuleSnapshot>();
    }
};

// ---- pure helper functions (unit-testable) --------------------------------
bool isWinVk(uint32_t vk);
bool isCtrlVk(uint32_t vk);
bool isAltVk(uint32_t vk);
bool isShiftVk(uint32_t vk);
bool isModifierVk(uint32_t vk);  // any modifier key

// True if the given non-modifier key completes the standard Windows
// input-language / IME switch chord while Caps Lock is held.
bool isImeSwitchCombo(uint32_t mods, uint32_t vk, uint32_t scan);

// True if a game-protection rule matches "mods + key". Only considers
// non-Win block rules and the "allowed Win" replay set.
bool matchesGameRule(const RuleSnapshot& snap, uint32_t mods, uint32_t vk, uint32_t scan);

// True if the key is an allowed (disabled) pure-Win rule -> should be replayed.
bool isAllowedWinRule(const RuleSnapshot& snap, uint32_t vk);

// Human readable names.
std::string vkName(uint32_t vk);
std::string scanName(uint32_t scan);
std::string modsName(uint32_t mods);
std::string comboName(uint32_t mods, uint32_t vk, uint32_t scan);

// Default game-protection rules.
std::vector<HotkeyRule> defaultRules();

// Detect which Windows input-language/IME switch hotkeys are actually active.
uint32_t detectImeSwitchMask();

} // namespace wkg
