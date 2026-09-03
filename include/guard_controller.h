#pragma once
#include <QObject>
#include <QTimer>

#include "common.h"
#include "config.h"
#include "fullscreen.h"
#include "ime_manager.h"
#include "keyboard_hook.h"

namespace wkg {

// Coordinates the keyboard hook, fullscreen/foreground detection and the
// Caps Lock <-> IME linkage, all from the GUI thread.
class GuardController : public QObject {
    Q_OBJECT
public:
    explicit GuardController(ConfigStore& store, QObject* parent = nullptr);
    ~GuardController() override;

    bool start();
    void stop();

    // Publish a fresh rules snapshot + IME mask to the hook thread.
    void publishSnapshot();
    void applyConfigChanged();

    // Caps/IME
    ImeManager& ime() { return ime_; }
    QVector<ImeEntry> imeList() { return ime_.enumerate(); }
    void refreshImeList() { ime_.resetEnumeration(); ime_.enumerate(); }
    void refreshImeMask();
    bool capsOn() const { return capsOn_; }
    bool imeLock() const { return imeLockActive_; }

    // Shortcut recording (GUI polls until recordDone).
    void beginRecord();
    bool pollRecord(RecordedCombo& out);
    void cancelRecord();
    bool recording() const { return state_.recordActive.load(); }

    // Emergency toggle (also exposed in tray).
    void toggleEmergency();
    bool emergencySuspended() const { return state_.emergencySuspend.load(); }

    SharedState& state() { return state_; }

    void setUserPaused(bool paused);

    // Re-run protection evaluation immediately (called by the GUI after edits).
    void refreshNow() { evaluateProtection(); }

signals:
    void protectionChanged(bool active, QString exe, QString modeText);
    void capsChanged(bool on, bool imeLock, QString currentIme);
    void imeSwitched(QString description, bool ok, QString error);
    void adminWarning(QString exe);
    void statusMessage(QString msg);

private slots:
    void onTick();

private:
    void evaluateProtection();
    void handleCaps(bool nowOn);
    void switchEnglishLocked();
    void applyCapsOffAction();

    ConfigStore& store_;
    SharedState state_;
    KeyboardHook hook_;
    ImeManager ime_;
    QTimer timer_;
    HWINEVENTHOOK fgHook_ = nullptr;

    bool started_ = false;
    bool capsOn_ = false;
    bool imeLockActive_ = false;
    bool userPaused_ = false;
    bool warnedAdmin_ = false;

    QString currentExe_;
    bool currentMatched_ = false;
    WindowMode currentMode_ = WindowMode::Unknown;

    // Last emitted protection status (avoid redundant GUI updates each tick).
    bool lastActive_ = false;
    QString lastExe_;
    QString lastMode_;
    bool lastEmitted_ = false;

    static void CALLBACK fgEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                     LONG idObject, LONG idChild, DWORD thread,
                                     DWORD time);
};

} // namespace wkg
