#include "guard_controller.h"

#include <QMetaObject>

#include "process_info.h"

namespace wkg {

static GuardController* g_controller = nullptr;

GuardController::GuardController(ConfigStore& store, QObject* parent)
    : QObject(parent), store_(store) {
    connect(&timer_, &QTimer::timeout, this, &GuardController::onTick);
}

GuardController::~GuardController() {
    stop();
}

bool GuardController::start() {
    if (started_) return true;

    publishSnapshot();
    refreshImeMask();

    // Initial Caps Lock state.
    capsOn_ = ImeManager::capsLockOn();
    if (store_.data().ime.enabled && capsOn_) {
        switchEnglishLocked();
        imeLockActive_ = true;
    }
    state_.imeLockActive.store(imeLockActive_);

    if (!hook_.start(&state_)) {
        emit statusMessage(wgTr("Keyboard hook failed to start"));
        return false;
    }

    // Immediate foreground-change notifications (out-of-context on GUI thread).
    g_controller = this;
    fgHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                              nullptr, &GuardController::fgEventProc, 0, 0,
                              WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    evaluateProtection();

    timer_.start(100);  // 10 Hz: cheap Win32 calls only
    started_ = true;
    return true;
}

void GuardController::stop() {
    if (!started_) return;
    started_ = false;
    timer_.stop();
    if (fgHook_) {
        UnhookWinEvent(fgHook_);
        fgHook_ = nullptr;
    }
    g_controller = nullptr;
    hook_.stop();
    // Release any IME lock so the user never gets stuck.
    state_.imeLockActive.store(false);
    state_.protectionActive.store(false);
}

void GuardController::publishSnapshot() {
    auto snap = store_.data().toSnapshot();
    std::atomic_store(&state_.rules, snap);
    state_.imeSwitchMask.store(detectImeSwitchMask());
}

void GuardController::applyConfigChanged() {
    publishSnapshot();
    refreshImeMask();
    // If the IME feature was turned off, release the lock.
    if (!store_.data().ime.enabled) {
        imeLockActive_ = false;
        state_.imeLockActive.store(false);
    }
    evaluateProtection();
}

void GuardController::refreshImeMask() {
    state_.imeSwitchMask.store(detectImeSwitchMask());
}

void GuardController::beginRecord() {
    state_.recordActive.store(true);
    state_.recordDone.store(false);
    {
        std::lock_guard<std::mutex> lk(state_.recordMutex);
        state_.recorded = RecordedCombo{};
    }
}

bool GuardController::pollRecord(RecordedCombo& out) {
    if (!state_.recordDone.load()) return false;
    std::lock_guard<std::mutex> lk(state_.recordMutex);
    out = state_.recorded;
    return true;
}

void GuardController::cancelRecord() {
    state_.recordActive.store(false);
    state_.recordDone.store(false);
    {
        std::lock_guard<std::mutex> lk(state_.recordMutex);
        state_.recorded = RecordedCombo{0, 0, 0, true};
    }
}

void GuardController::toggleEmergency() {
    const bool cur = state_.emergencySuspend.load();
    state_.emergencySuspend.store(!cur);
    emit statusMessage(cur ? wgTr("Emergency suspend off; protection resumed")
                           : wgTr("Emergency suspend: protection paused"));
}

void GuardController::setUserPaused(bool paused) {
    userPaused_ = paused;
    evaluateProtection();
}

void GuardController::onTick() {
    const bool caps = ImeManager::capsLockOn();
    if (caps != capsOn_) {
        handleCaps(caps);
    }
    evaluateProtection();
}

void GuardController::handleCaps(bool nowOn) {
    capsOn_ = nowOn;
    if (!store_.data().ime.enabled) return;

    if (nowOn) {
        switchEnglishLocked();
        imeLockActive_ = true;
        state_.imeLockActive.store(true);
        emit capsChanged(true, true, ime_.currentDescription());
    } else {
        imeLockActive_ = false;
        state_.imeLockActive.store(false);
        applyCapsOffAction();
        emit capsChanged(false, false, ime_.currentDescription());
    }
}

void GuardController::switchEnglishLocked() {
    refreshImeList();
    QString err;
    const bool ok = ime_.switchToEnglish(&err);
    if (!ok) {
        emit imeSwitched(wgTr("English input method"), false, err);
        emit statusMessage(wgTr("No English input method / keyboard layout detected"));
    } else {
        emit imeSwitched(wgTr("English input method"), true, QString());
    }
}

void GuardController::applyCapsOffAction() {
    const ImeConfig& c = store_.data().ime;
    if (c.capsOffAction != CapsOffAction::SwitchToSpecific) return;
    if (c.capsOffTarget.isEmpty()) return;

    refreshImeList();
    const ImeEntry e = ime_.findById(c.capsOffTarget);
    if (e.id.isEmpty()) {
        emit statusMessage(wgTr("The configured input method is uninstalled or missing: ") + c.capsOffTarget);
        return;
    }
    QString err;
    const bool ok = ime_.switchTo(c.capsOffTarget, &err);
    emit imeSwitched(e.name, ok, err);
}

void GuardController::evaluateProtection() {
    HWND fg = GetForegroundWindow();
    const QString exe = processNameForWindow(fg);

    bool matched = false;
    bool foregroundOnly = false;
    for (const auto& t : store_.data().targets) {
        if (!t.enabled) continue;
        if (t.name.compare(exe, Qt::CaseInsensitive) == 0) {
            matched = true;
            foregroundOnly = t.foregroundOnly;
            break;
        }
    }

    currentExe_ = exe;
    currentMatched_ = matched;

    WindowMode mode = WindowMode::Unknown;
    if (matched) {
        mode = foregroundOnly ? WindowMode::Fullscreen : classifyWindowMode(fg);
    }
    currentMode_ = mode;

    const bool fullscreen = (mode == WindowMode::Fullscreen);
    const bool emergency = state_.emergencySuspend.load();

    // Admin elevation warning (once per foreground target).
    if (matched) {
        const DWORD pid = processIdForWindow(fg);
        const bool elevated = isProcessElevated(pid);
        state_.targetElevated.store(elevated);
        if (elevated && !isCurrentProcessElevated() && !warnedAdmin_) {
            warnedAdmin_ = true;
            emit adminWarning(exe);
        }
    } else {
        state_.targetElevated.store(false);
    }

    const bool active = matched && fullscreen && !emergency && !userPaused_;

    // When the game re-becomes foreground+fullscreen, re-arm after Win+`.
    if (active) state_.passthrough.store(false);

    state_.protectionActive.store(active);

    QString modeText;
    if (!matched) modeText = wgTr("No target matched");
    else if (foregroundOnly) modeText = wgTr("Foreground mode");
    else if (fullscreen) modeText = wgTr("Fullscreen");
    else modeText = wgTr("Windowed");

    // Avoid redundant GUI updates on every 100 ms tick.
    if (!lastEmitted_ || active != lastActive_ || exe != lastExe_ || modeText != lastMode_) {
        lastEmitted_ = true;
        lastActive_ = active;
        lastExe_ = exe;
        lastMode_ = modeText;
        emit protectionChanged(active, exe, modeText);
    }
}

void CALLBACK GuardController::fgEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (g_controller) {
        QMetaObject::invokeMethod(g_controller, "onTick", Qt::QueuedConnection);
    }
}

} // namespace wkg
