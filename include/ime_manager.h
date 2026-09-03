#pragma once
#include <Windows.h>
#include <msctf.h>
#include <objbase.h>

#include <QString>
#include <QVector>

namespace wkg {

struct ImeEntry {
    enum class Kind { KeyboardLayout, TextService };
    Kind kind = Kind::KeyboardLayout;

    QString id;     // stable identifier used in config.json
    QString name;   // display name
    LANGID langid = 0;
    bool isEnglish = false;

    // keyboard layout
    HKL hkl = nullptr;
    QString layoutId;  // 8-hex string e.g. "00000409"

    // text service
    CLSID clsid{};
    GUID guidProfile{};
};

// Enumerates the input methods actually installed on this machine
// (keyboard layouts + TSF text services such as Microsoft Pinyin / WeChat).
class ImeManager {
public:
    ImeManager();
    ~ImeManager();

    QVector<ImeEntry> enumerate();
    void resetEnumeration() { enumerated_ = false; cache_.clear(); }

    bool hasEnglish() const;
    QString englishId() const;   // empty if none
    ImeEntry findById(const QString& id) const;

    // Session-wide switch of the active input method.
    bool switchToEnglish(QString* errMsg = nullptr);
    bool switchTo(const QString& id, QString* errMsg = nullptr);

    // Current active text-service / layout description (informational).
    QString currentDescription() const;

    QString lastError() const { return lastError_; }

    // Caps Lock toggle state (true = ON). Does not modify the key/LED.
    static bool capsLockOn();

private:
    bool ensureProfiles();
    QVector<ImeEntry> enumerateLayouts();
    QVector<ImeEntry> enumerateTextServices();
    bool activateLayout(const ImeEntry& e);
    bool activateTextService(const ImeEntry& e);
    HKL findHklForLang(LANGID langid) const;

    QVector<ImeEntry> cache_;
    bool enumerated_ = false;
    bool comInitialized_ = false;

    ITfInputProcessorProfiles* profiles_ = nullptr;
    ITfInputProcessorProfileMgr* profileMgr_ = nullptr;

    QString lastError_;
};

} // namespace wkg
