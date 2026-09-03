#include "config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QSaveFile>

namespace wkg {

QString defaultConfigPath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath("config.json");
}

std::shared_ptr<const RuleSnapshot> Config::toSnapshot() const {
    auto snap = std::make_shared<RuleSnapshot>();

    // Win master (Win alone)
    snap->winMaster = true;
    for (const auto& r : rules) {
        if (r.id == "win") {
            snap->winMaster = r.enabled;
            break;
        }
    }
    snap->winTick = winTickEnabled;
    snap->emergencyMods = emergencyMods;
    snap->emergencyVk = emergencyVk;

    for (const auto& r : rules) {
        const bool hasWin = (r.mods & MOD_WIN) != 0;
        if (!hasWin) {
            // Alt / Ctrl / Alt+Ctrl / Shift combos -> block chord key.
            if (r.enabled) snap->altCtrlBlocks.push_back(r);
        } else if (r.mods == MOD_WIN && r.vk != 0) {
            // Pure "Win + key". If disabled, user wants it to reach Windows:
            // record it so the hook can replay it through SendInput.
            if (!r.enabled) snap->allowedWinVks.insert(r.vk);
        }
        // Multi-modifier Win rules (Win+Shift+S ...) are inherently blocked
        // while the Win master is on (the Win keydown is swallowed).
    }
    return snap;
}

ConfigStore::ConfigStore(const QString& path) : path_(path) {
    data_.rules = defaultRules();
}

static HotkeyRule ruleFromJson(const QJsonObject& o) {
    HotkeyRule r;
    r.id = o.value("id").toString().toStdString();
    r.display = o.value("display").toString().toStdString();
    r.mods = (uint32_t)o.value("mods").toInt();
    r.vk = (uint32_t)o.value("vk").toInt();
    r.scan = (uint32_t)o.value("scan").toInt();
    r.enabled = o.value("enabled").toBool(true);
    if (r.display.empty())
        r.display = comboName(r.mods, r.vk, r.scan);
    return r;
}

static QJsonObject ruleToJson(const HotkeyRule& r) {
    QJsonObject o;
    o["id"] = QString::fromStdString(r.id);
    o["display"] = QString::fromStdString(r.display);
    o["mods"] = (int)r.mods;
    o["vk"] = (int)r.vk;
    o["scan"] = (int)r.scan;
    o["enabled"] = r.enabled;
    return o;
}

bool ConfigStore::load() {
    QFile f(path_);
    if (!f.exists()) {
        data_ = Config{};
        data_.rules = defaultRules();
        save();
        return true;
    }
    if (!f.open(QIODevice::ReadOnly)) return false;

    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError parseErr{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseErr);

    // Corrupt / invalid JSON: back up the bad file once, then regenerate.
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        const QString bak = path_ + ".corrupt";
        QFile::remove(bak);
        QFile::rename(path_, bak);
        data_ = Config{};
        data_.rules = defaultRules();
        save();
        return true;
    }

    const QJsonObject root = doc.object();

    data_ = Config{};
    data_.rules = defaultRules();

    // targets
    data_.targets.clear();
    for (const auto& v : root.value("targets").toArray()) {
        const QJsonObject o = v.toObject();
        TargetEntry t;
        t.name = o.value("name").toString();
        t.enabled = o.value("enabled").toBool(true);
        t.foregroundOnly = o.value("foregroundOnly").toBool(false);
        if (!t.name.isEmpty()) data_.targets.push_back(t);
    }

    // rules: overlay saved enabled-state + custom rules over defaults
    QMap<QString, HotkeyRule> savedRules;
    for (const auto& v : root.value("rules").toArray()) {
        HotkeyRule r = ruleFromJson(v.toObject());
        savedRules[QString::fromStdString(r.id)] = r;
    }
    for (auto& r : data_.rules) {
        auto it = savedRules.find(QString::fromStdString(r.id));
        if (it != savedRules.end()) {
            r.enabled = it->enabled;
            if (!it->display.empty()) r.display = it->display;
        }
    }
    // custom rules (ids not in defaults)
    for (const auto& r : savedRules) {
        if (!r.id.empty() && r.id.rfind("custom_", 0) == 0) {
            data_.rules.push_back(r);
        }
    }

    data_.winTickEnabled = root.value("winTickEnabled").toBool(true);
    data_.autostart = root.value("autostart").toBool(false);
    data_.fullscreenMode = root.value("fullscreenMode").toString("auto");
    data_.language = root.value("language").toString("system");

    const QJsonObject zones = root.value("zones").toObject();
    data_.zoneLeft = zones.value("left").toBool(true);
    data_.zoneCenter = zones.value("center").toBool(false);
    data_.zoneRight = zones.value("right").toBool(false);

    const QJsonObject em = root.value("emergency").toObject();
    data_.emergencyMods = (uint32_t)em.value("mods").toInt(MOD_CTRL | MOD_ALT);
    data_.emergencyVk = (uint32_t)em.value("vk").toInt(VK_F12);

    const QJsonObject ime = root.value("ime").toObject();
    data_.ime.enabled = ime.value("enabled").toBool(true);
    data_.ime.capsOffAction = (ime.value("capsOffAction").toString() == "switch")
                                  ? CapsOffAction::SwitchToSpecific
                                  : CapsOffAction::Nothing;
    data_.ime.capsOffTarget = ime.value("capsOffTarget").toString();

    return true;
}

bool ConfigStore::save() const {
    QJsonObject root;
    root["version"] = 1;

    QJsonArray targets;
    for (const auto& t : data_.targets) {
        QJsonObject o;
        o["name"] = t.name;
        o["enabled"] = t.enabled;
        o["foregroundOnly"] = t.foregroundOnly;
        targets.append(o);
    }
    root["targets"] = targets;

    QJsonArray rules;
    for (const auto& r : data_.rules)
        rules.append(ruleToJson(r));
    root["rules"] = rules;

    root["winTickEnabled"] = data_.winTickEnabled;
    root["autostart"] = data_.autostart;
    root["fullscreenMode"] = data_.fullscreenMode;
    root["language"] = data_.language;

    QJsonObject zones;
    zones["left"] = data_.zoneLeft;
    zones["center"] = data_.zoneCenter;
    zones["right"] = data_.zoneRight;
    root["zones"] = zones;

    QJsonObject em;
    em["mods"] = (int)data_.emergencyMods;
    em["vk"] = (int)data_.emergencyVk;
    root["emergency"] = em;

    QJsonObject ime;
    ime["enabled"] = data_.ime.enabled;
    ime["capsOffAction"] = (data_.ime.capsOffAction == CapsOffAction::SwitchToSpecific) ? "switch" : "none";
    ime["capsOffTarget"] = data_.ime.capsOffTarget;
    root["ime"] = ime;

    QSaveFile f(path_);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) return false;
    return true;
}

void ConfigStore::resetRulesToDefault() {
    data_.rules = defaultRules();
}

void ConfigStore::mergeDefaults() {
    const auto defs = defaultRules();
    for (const auto& d : defs) {
        bool found = false;
        for (const auto& r : data_.rules) {
            if (r.id == d.id) { found = true; break; }
        }
        if (!found) data_.rules.push_back(d);
    }
}

} // namespace wkg
