#pragma once
#include "common.h"

#include <thread>

namespace wkg {

// Low-level keyboard hook (WH_KEYBOARD_LL) running on its own thread with a
// message loop. The callback is kept minimal: no file IO, no GUI, no blocking.
class KeyboardHook {
public:
    KeyboardHook() = default;
    ~KeyboardHook();

    KeyboardHook(const KeyboardHook&) = delete;
    KeyboardHook& operator=(const KeyboardHook&) = delete;

    bool start(SharedState* state);
    void stop();
    bool isRunning() const { return running_; }

private:
    static LRESULT CALLBACK proc(int nCode, WPARAM wParam, LPARAM lParam);
    LRESULT handle(int nCode, WPARAM wParam, LPARAM lParam);
    LRESULT handleRecording(bool down, uint32_t vk, uint32_t scan);
    void injectKey(uint32_t vk, bool down);
    void injectWinDown();
    void injectWinUp();
    void injectWinTap();

    // Modifier bookkeeping (hook thread only).
    uint32_t currentMods() const;

    HHOOK hook_ = nullptr;
    std::thread thread_;
    SharedState* state_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};

    // Physical modifier state (tracked from hook events; counts handle L/R).
    int winCount_ = 0;
    int ctrlCount_ = 0;
    int altCount_ = 0;
    int shiftCount_ = 0;

    // Win-key chord state machine.
    bool winSwallowed_ = false;    // Win down swallowed, OS hasn't seen it
    bool winForImeOnly_ = false;   // swallow was for IME lock (not game master)
    bool winReplayed_ = false;     // synthetic Win down injected
    bool imeSpaceBlocked_ = false; // swallowed Space during swallowed Win
    uint32_t winSwallowedVk_ = 0;

    // Replayed chord (allowed Win+X rules).
    bool chordKeyReplayed_ = false;
    uint32_t chordKeyVk_ = 0;

    // Passthrough (Win+`) pending orphan ups.
    bool swallowPendingWinUp_ = false;
    bool swallowPendingTickUp_ = false;
    uint32_t tickVk_ = 0;

    bool swallowedDown_[256] = {false};  // vk codes whose keydown we blocked
};

} // namespace wkg
