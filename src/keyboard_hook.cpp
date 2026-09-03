#include "keyboard_hook.h"

#include <windowsx.h>

namespace wkg {

static KeyboardHook* g_hook = nullptr;

KeyboardHook::~KeyboardHook() {
    stop();
}

bool KeyboardHook::start(SharedState* state) {
    if (running_) return true;
    state_ = state;
    stopRequested_ = false;

    thread_ = std::thread([this]() {
        // Install the low-level hook on this thread and run a message loop
        // so the system can deliver hook callbacks here.
        g_hook = this;
        hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &KeyboardHook::proc,
                                  GetModuleHandleW(nullptr), 0);
        if (hook_) {
            running_ = true;
            MSG msg;
            while (!stopRequested_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            UnhookWindowsHookEx(hook_);
            hook_ = nullptr;
        }
        running_ = false;
        g_hook = nullptr;

        // Release any stuck synthetic state before exiting.
        if (winReplayed_) injectWinUp();
    });
    return true;
}

void KeyboardHook::stop() {
    if (!thread_.joinable()) return;
    stopRequested_ = true;
    PostThreadMessageW(GetThreadId(thread_.native_handle()), WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
}

uint32_t KeyboardHook::currentMods() const {
    uint32_t m = 0;
    if (winCount_ > 0) m |= MOD_WIN;
    if (ctrlCount_ > 0) m |= MOD_CTRL;
    if (altCount_ > 0) m |= MOD_ALT;
    if (shiftCount_ > 0) m |= MOD_SHIFT;
    return m;
}

void KeyboardHook::injectKey(uint32_t vk, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = (WORD)vk;
    if (!down) in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
}

void KeyboardHook::injectWinDown() { injectKey(VK_LWIN, true); }
void KeyboardHook::injectWinUp() { injectKey(VK_LWIN, false); }

void KeyboardHook::injectWinTap() {
    injectWinDown();
    injectWinUp();
}

LRESULT KeyboardHook::handleRecording(bool down, uint32_t vk, uint32_t scan) {
    // While recording, swallow everything; capture the first "modifier + key".
    if (down) {
        if (isModifierVk(vk)) {
            swallowedDown_[vk] = true;  // swallow its up too
            return 1;
        }
        const uint32_t mods = currentMods();
        if (vk == VK_ESCAPE && mods == 0) {
            std::lock_guard<std::mutex> lk(state_->recordMutex);
            state_->recorded = RecordedCombo{0, 0, 0, true};
            state_->recordDone = true;
            state_->recordActive = false;
            swallowedDown_[vk] = true;
            return 1;
        }
        if (mods == 0) {
            swallowedDown_[vk] = true;  // keep waiting; ignore bare key
            return 1;
        }
        std::lock_guard<std::mutex> lk(state_->recordMutex);
        state_->recorded = RecordedCombo{mods, vk, scan, false};
        state_->recordDone = true;
        state_->recordActive = false;
        swallowedDown_[vk] = true;
        return 1;
    }
    return 1;  // swallow ups too (their downs were swallowed)
}

LRESULT KeyboardHook::handle(int nCode, WPARAM wParam, LPARAM lParam) {
    auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
    const uint32_t vk = info->vkCode;
    const uint32_t scan = info->scanCode & 0xFF;
    const bool injected = (info->flags & LLKHF_INJECTED) != 0;

    // Never re-process our own SendInput injections.
    if (injected) return CallNextHookEx(hook_, nCode, wParam, lParam);

    // ---- update physical modifier counts ---------------------------------
    if (down) {
        if (isWinVk(vk)) winCount_++;
        else if (isCtrlVk(vk)) ctrlCount_++;
        else if (isAltVk(vk)) altCount_++;
        else if (isShiftVk(vk)) shiftCount_++;
    } else if (up) {
        if (isWinVk(vk)) { if (winCount_ > 0) winCount_--; }
        else if (isCtrlVk(vk)) { if (ctrlCount_ > 0) ctrlCount_--; }
        else if (isAltVk(vk)) { if (altCount_ > 0) altCount_--; }
        else if (isShiftVk(vk)) { if (shiftCount_ > 0) shiftCount_--; }
    }

    // ---- swallow keyups for keys whose keydown we blocked -----------------
    if (up) {
        if (vk < 256) {
            const bool wasSwallowed = swallowedDown_[vk];
            if (wasSwallowed) swallowedDown_[vk] = false;
            if (chordKeyReplayed_ && vk == chordKeyVk_) {
                chordKeyReplayed_ = false;
                injectKey(vk, false);
            }
            if (wasSwallowed) return 1;
        }
    }

    const auto snap = std::atomic_load(&state_->rules);

    // ---- recording mode --------------------------------------------------
    if (state_->recordActive.load()) {
        return handleRecording(down, vk, scan);
    }

    // ---- emergency toggle (Ctrl+Alt+F12) — highest priority ---------------
    if (down && !isModifierVk(vk)) {
        const uint32_t mods = currentMods();
        if (vk == snap->emergencyVk && mods == snap->emergencyMods) {
            const bool cur = state_->emergencySuspend.load();
            state_->emergencySuspend.store(!cur);
            // clear transient state
            state_->passthrough.store(false);
            winSwallowed_ = winReplayed_ = winForImeOnly_ = imeSpaceBlocked_ = false;
            chordKeyReplayed_ = false;
            swallowPendingWinUp_ = swallowPendingTickUp_ = false;
            return 1;  // swallow the F12
        }
    }

    // ---- emergency suspend: pass everything ------------------------------
    if (state_->emergencySuspend.load())
        return CallNextHookEx(hook_, nCode, wParam, lParam);

    // ---- Win+` passthrough: pass everything, swallow orphan ups -----------
    if (state_->passthrough.load()) {
        if (up && isWinVk(vk) && swallowPendingWinUp_) {
            swallowPendingWinUp_ = false;
            winSwallowed_ = winReplayed_ = winForImeOnly_ = imeSpaceBlocked_ = false;
            return 1;
        }
        if (up && vk == tickVk_ && swallowPendingTickUp_) {
            swallowPendingTickUp_ = false;
            return 1;
        }
        return CallNextHookEx(hook_, nCode, wParam, lParam);
    }

    const bool protect = state_->protectionActive.load();
    const bool imeLock = state_->imeLockActive.load();
    const uint32_t imeMask = state_->imeSwitchMask.load();
    if (!protect && !imeLock)
        return CallNextHookEx(hook_, nCode, wParam, lParam);

    // ---- Win key handling ------------------------------------------------
    if (isWinVk(vk)) {
        if (down) {
            const bool gameSwallow = protect && snap->winMaster;
            const bool imeSwallow = imeLock && (imeMask & IME_WINSPACE);
            if (gameSwallow || imeSwallow) {
                winSwallowed_ = true;
                winForImeOnly_ = imeSwallow && !gameSwallow;
                winReplayed_ = false;
                imeSpaceBlocked_ = false;
                winSwallowedVk_ = vk;
                return 1;
            }
            return CallNextHookEx(hook_, nCode, wParam, lParam);
        } else {  // up
            if (winSwallowed_ && vk == winSwallowedVk_) {
                winSwallowed_ = false;
                if (winReplayed_) {
                    injectWinUp();
                    winReplayed_ = false;
                } else if (winForImeOnly_ && !imeSpaceBlocked_) {
                    // Win pressed alone during IME lock: replay a tap so the
                    // native Start menu still opens normally.
                    injectWinTap();
                }
                winForImeOnly_ = false;
                imeSpaceBlocked_ = false;
                return 1;
            }
            return CallNextHookEx(hook_, nCode, wParam, lParam);
        }
    }

    // ---- IME lock: Alt+Shift / Ctrl+Shift --------------------------------
    if (imeLock && down && isModifierVk(vk) && !isWinVk(vk)) {
        const bool curAlt = isAltVk(vk);
        const bool curShift = isShiftVk(vk);
        const bool curCtrl = isCtrlVk(vk);
        // Alt + Shift (second key down while the other is held, no Ctrl)
        if ((imeMask & IME_ALTSHIFT) &&
            ((curShift && altCount_ > 0 && ctrlCount_ == 0) ||
             (curAlt && shiftCount_ > 0 && ctrlCount_ == 0))) {
            swallowedDown_[vk] = true;
            return 1;
        }
        // Ctrl + Shift
        if ((imeMask & IME_CTRLSHIFT) &&
            ((curShift && ctrlCount_ > 0 && altCount_ == 0) ||
             (curCtrl && shiftCount_ > 0 && altCount_ == 0))) {
            swallowedDown_[vk] = true;
            return 1;
        }
    }

    // ---- non-modifier keys -----------------------------------------------
    if (down) {
        // Win + Space during IME lock (Win swallowed for IME only)
        if (winSwallowed_ && winForImeOnly_ && (imeMask & IME_WINSPACE) &&
            vk == VK_SPACE) {
            imeSpaceBlocked_ = true;
            swallowedDown_[vk] = true;
            return 1;
        }

        // Win + ` passthrough (game protection)
        if (winSwallowed_ && !winForImeOnly_ && snap->winTick && scan == SCAN_TICK) {
            state_->passthrough.store(true);
            injectWinTap();
            swallowPendingWinUp_ = true;
            swallowPendingTickUp_ = true;
            tickVk_ = vk;
            winSwallowed_ = winReplayed_ = winForImeOnly_ = imeSpaceBlocked_ = false;
            return 1;
        }

        // Win + chord while Win is swallowed
        if (winSwallowed_) {
            if (protect && snap->winMaster) {
                // Game mode: allowed (disabled) rules are replayed to Windows,
                // everything else passes through to the game (Win is swallowed).
                if (isAllowedWinRule(*snap, vk)) {
                    if (!winReplayed_) {
                        injectWinDown();
                        winReplayed_ = true;
                    }
                    injectKey(vk, true);
                    chordKeyReplayed_ = true;
                    chordKeyVk_ = vk;
                    swallowedDown_[vk] = true;  // swallow physical down; up handled above
                    return 1;
                }
                return CallNextHookEx(hook_, nCode, wParam, lParam);
            }
            // IME-only Win mode: replay Win once, then pass any non-space key.
            if (!winReplayed_) {
                injectWinDown();
                winReplayed_ = true;
            }
            return CallNextHookEx(hook_, nCode, wParam, lParam);
        }

        // Alt/Ctrl block rules (game protection)
        if (protect) {
            const uint32_t mods = currentMods();
            if (matchesGameRule(*snap, mods, vk, scan)) {
                swallowedDown_[vk] = true;
                return 1;
            }
        }
    }

    return CallNextHookEx(hook_, nCode, wParam, lParam);
}

LRESULT CALLBACK KeyboardHook::proc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0) return CallNextHookEx(g_hook ? g_hook->hook_ : nullptr, nCode, wParam, lParam);
    if (g_hook) return g_hook->handle(nCode, wParam, lParam);
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

} // namespace wkg
