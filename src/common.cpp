#include "common.h"

namespace wkg {

bool isWinVk(uint32_t vk) {
    return vk == VK_LWIN || vk == VK_RWIN;
}

bool isCtrlVk(uint32_t vk) {
    return vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL;
}

bool isAltVk(uint32_t vk) {
    return vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU;
}

bool isShiftVk(uint32_t vk) {
    return vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT;
}

bool isModifierVk(uint32_t vk) {
    return isWinVk(vk) || isCtrlVk(vk) || isAltVk(vk) || isShiftVk(vk);
}

// Win+Space, Alt+Shift and Ctrl+Shift are the standard Windows input-language /
// IME switch chords. We also accept the Ctrl+Alt variant used by some layouts.
bool isImeSwitchCombo(uint32_t mods, uint32_t vk, uint32_t scan) {
    (void)scan;
    // Win + Space
    if ((mods == MOD_WIN) && vk == VK_SPACE) return true;
    // Alt + Shift (either direction is detected at the point the second
    // modifier goes down, see the hook; here the chord is "Alt held, Shift down"
    // or "Shift held, Alt down", normalised by the hook into mods + vk)
    if ((mods == MOD_ALT) && isShiftVk(vk)) return true;
    if ((mods == MOD_SHIFT) && isAltVk(vk)) return true;
    // Ctrl + Shift
    if ((mods == MOD_CTRL) && isShiftVk(vk)) return true;
    if ((mods == MOD_SHIFT) && isCtrlVk(vk)) return true;
    return false;
}

bool isAllowedWinRule(const RuleSnapshot& snap, uint32_t vk) {
    return snap.allowedWinVks.count(vk) != 0;
}

bool matchesGameRule(const RuleSnapshot& snap, uint32_t mods, uint32_t vk, uint32_t scan) {
    for (const auto& r : snap.altCtrlBlocks) {
        if (!r.enabled) continue;
        if (r.mods != mods) continue;
        if (r.scan != 0) {
            if (r.scan == scan) return true;
        } else if (r.vk != 0 && r.vk == vk) {
            return true;
        }
    }
    return false;
}

std::string vkName(uint32_t vk) {
    if (vk >= 'A' && vk <= 'Z') return std::string(1, (char)vk);
    if (vk >= '0' && vk <= '9') return std::string(1, (char)vk);
    switch (vk) {
        case VK_BACK: return "Backspace";
        case VK_TAB: return "Tab";
        case VK_RETURN: return "Enter";
        case VK_ESCAPE: return "Esc";
        case VK_SPACE: return "Space";
        case VK_PRIOR: return "PgUp";
        case VK_NEXT: return "PgDn";
        case VK_END: return "End";
        case VK_HOME: return "Home";
        case VK_LEFT: return "Left";
        case VK_UP: return "Up";
        case VK_RIGHT: return "Right";
        case VK_DOWN: return "Down";
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_F1: return "F1"; case VK_F2: return "F2"; case VK_F3: return "F3";
        case VK_F4: return "F4"; case VK_F5: return "F5"; case VK_F6: return "F6";
        case VK_F7: return "F7"; case VK_F8: return "F8"; case VK_F9: return "F9";
        case VK_F10: return "F10"; case VK_F11: return "F11"; case VK_F12: return "F12";
        case VK_OEM_3: return "`";
        case VK_OEM_MINUS: return "-";
        case VK_OEM_PLUS: return "=";
        case VK_OEM_4: return "[";
        case VK_OEM_6: return "]";
        case VK_OEM_5: return "\\";
        case VK_OEM_1: return ";";
        case VK_OEM_7: return "'";
        case VK_OEM_COMMA: return ",";
        case VK_OEM_PERIOD: return ".";
        case VK_OEM_2: return "/";
        default: break;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "VK_0x%02X", vk);
    return buf;
}

std::string scanName(uint32_t scan) {
    if (scan == SCAN_TICK) return "`";
    char buf[16];
    snprintf(buf, sizeof(buf), "Scan0x%02X", scan);
    return buf;
}

std::string modsName(uint32_t mods) {
    std::string s;
    if (mods & MOD_WIN) s += "Win + ";
    if (mods & MOD_CTRL) s += "Ctrl + ";
    if (mods & MOD_ALT) s += "Alt + ";
    if (mods & MOD_SHIFT) s += "Shift + ";
    if (!s.empty()) s = s.substr(0, s.size() - 3);
    return s;
}

std::string comboName(uint32_t mods, uint32_t vk, uint32_t scan) {
    std::string m = modsName(mods);
    std::string k = (scan != 0) ? scanName(scan) : vkName(vk);
    if (m.empty()) return k;
    if (k.empty()) return m;
    return m + " + " + k;
}

uint32_t detectImeSwitchMask() {
    // Win+Space is the modern (Win8+) language/IME switcher and is always active.
    uint32_t mask = IME_WINSPACE;

    // The legacy language/layout hotkeys are configured under this key.
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Keyboard Layout\\Toggle", 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        // No legacy config: assume Alt+Shift and Ctrl+Shift are not in use.
        return mask;
    }

    auto readDword = [&](const wchar_t* name, DWORD& out) {
        DWORD type = 0, size = sizeof(DWORD);
        out = 0;
        return RegQueryValueExW(key, name, nullptr, &type, (LPBYTE)&out, &size) == ERROR_SUCCESS &&
               (type == REG_DWORD || type == REG_SZ);
    };

    // "Hotkey": 1 = key sequence, 2 = ALT+SHIFT, 3 = CTRL+SHIFT
    DWORD hotkey = 1;
    if (!readDword(L"Hotkey", hotkey)) hotkey = 1;

    if (hotkey == 2) {
        mask |= IME_ALTSHIFT;
    } else if (hotkey == 3) {
        mask |= IME_CTRLSHIFT;
    } else {
        // key-sequence mode: read the specific language/layout sequence.
        // Values: 0=none, 1=Left ALT+SHIFT, 2=CTRL+SHIFT, 3=grave accent.
        DWORD seq = 0;
        if (readDword(L"Language Hotkey", seq)) {
            if (seq == 1) mask |= IME_ALTSHIFT;
            else if (seq == 2) mask |= IME_CTRLSHIFT;
            else if (seq == 3) mask |= IME_GRAVE;
        }
        if (readDword(L"Layout Hotkey", seq)) {
            if (seq == 1) mask |= IME_ALTSHIFT;
            else if (seq == 2) mask |= IME_CTRLSHIFT;
            else if (seq == 3) mask |= IME_GRAVE;
        }
    }
    RegCloseKey(key);
    return mask;
}

std::vector<HotkeyRule> defaultRules() {
    auto R = [](const char* id, uint32_t mods, uint32_t vk, uint32_t scan, bool on = true) {
        HotkeyRule r;
        r.id = id;
        r.mods = mods;
        r.vk = vk;
        r.scan = scan;
        r.enabled = on;
        r.display = comboName(mods, vk, scan);
        return r;
    };

    std::vector<HotkeyRule> rules;
    // "Win alone" is special-cased (swallow Win keydown); vk/scan both 0.
    rules.push_back(R("win", MOD_WIN, 0, 0, true));
    rules.back().display = "Win";

    rules.push_back(R("win_d", MOD_WIN, 'D', 0));
    rules.push_back(R("win_e", MOD_WIN, 'E', 0));
    rules.push_back(R("win_r", MOD_WIN, 'R', 0));
    rules.push_back(R("win_s", MOD_WIN, 'S', 0));
    rules.push_back(R("win_a", MOD_WIN, 'A', 0));
    rules.push_back(R("win_x", MOD_WIN, 'X', 0));
    rules.push_back(R("win_i", MOD_WIN, 'I', 0));
    rules.push_back(R("win_l", MOD_WIN, 'L', 0));
    rules.push_back(R("win_tab", MOD_WIN, VK_TAB, 0));
    rules.push_back(R("win_space", MOD_WIN, VK_SPACE, 0));
    rules.push_back(R("win_shift_s", MOD_WIN | MOD_SHIFT, 'S', 0));
    rules.push_back(R("win_ctrl_d", MOD_WIN | MOD_CTRL, 'D', 0));
    rules.push_back(R("win_ctrl_f4", MOD_WIN | MOD_CTRL, VK_F4, 0));
    rules.push_back(R("win_ctrl_left", MOD_WIN | MOD_CTRL, VK_LEFT, 0));
    rules.push_back(R("win_ctrl_right", MOD_WIN | MOD_CTRL, VK_RIGHT, 0));
    rules.push_back(R("win_home", MOD_WIN, VK_HOME, 0));
    for (char d = '0'; d <= '9'; ++d)
        rules.push_back(R(("win_" + std::string(1, d)).c_str(), MOD_WIN, (uint32_t)d, 0));

    rules.push_back(R("alt_tab", MOD_ALT, VK_TAB, 0));
    rules.push_back(R("alt_esc", MOD_ALT, VK_ESCAPE, 0));
    rules.push_back(R("alt_space", MOD_ALT, VK_SPACE, 0));
    rules.push_back(R("alt_f4", MOD_ALT, VK_F4, 0));
    rules.push_back(R("ctrl_esc", MOD_CTRL, VK_ESCAPE, 0));

    return rules;
}

} // namespace wkg
