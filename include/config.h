#pragma once
#include "common.h"

#include <QCoreApplication>
#include <QString>
#include <QVector>

namespace wkg {

// Single-context translation helper (context "WinKeyGuard").
inline QString wgTr(const char* sourceText) {
    return QCoreApplication::translate("WinKeyGuard", sourceText);
}

// One target process. `name` is the executable basename (case-insensitive).
// `foregroundOnly` is the "foreground-window mode" compat option: protect on
// foreground match without requiring a fullscreen check.
// `rules` is reserved for future per-process rule overrides (v1 uses global).
struct TargetEntry {
    QString name;
    bool enabled = true;
    bool foregroundOnly = false;
    QVector<HotkeyRule> rules;  // reserved: empty = use global rules
};

// Behaviour after Caps Lock is switched OFF.
enum class CapsOffAction {
    Nothing = 0,
    SwitchToSpecific = 1
};

struct ImeConfig {
    bool enabled = true;
    CapsOffAction capsOffAction = CapsOffAction::Nothing;
    QString capsOffTarget;  // identifier of the IME to switch to
};

struct Config {
    QVector<TargetEntry> targets;

    // Global rules (the "Win" rule is included; winTick handled separately).
    std::vector<HotkeyRule> rules;

    bool winTickEnabled = true;
    bool autostart = false;
    QString fullscreenMode = "auto";  // "auto" | "foreground"
    QString language = "system";      // "system" | "en" | "zh"

    // keyboard zone protection (recommendation only; interception is combo-based)
    bool zoneLeft = true;
    bool zoneCenter = false;
    bool zoneRight = false;

    uint32_t emergencyMods = MOD_CTRL | MOD_ALT;
    uint32_t emergencyVk = VK_F12;

    ImeConfig ime;

    // Derived, published snapshot for the hook thread.
    std::shared_ptr<const RuleSnapshot> toSnapshot() const;
};

class ConfigStore {
public:
    explicit ConfigStore(const QString& path);
    bool load();
    bool save() const;

    Config& data() { return data_; }
    const Config& data() const { return data_; }

    QString path() const { return path_; }

    // Reset only the game rules to defaults (keep targets/ime/settings).
    void resetRulesToDefault();
    // Merge in default rules that are missing (keeps user toggles for existing ids).
    void mergeDefaults();

private:
    QString path_;
    Config data_;
};

// Default config.json path (next to the executable).
QString defaultConfigPath();

} // namespace wkg
