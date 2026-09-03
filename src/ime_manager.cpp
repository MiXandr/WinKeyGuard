#include "ime_manager.h"

#include <QCoreApplication>

#include <algorithm>

namespace wkg {

static QString trc(const char* s) {
    return QCoreApplication::translate("WinKeyGuard", s);
}

static QString guidToString(const GUID& g) {
    OLECHAR buf[40];
    StringFromGUID2(g, buf, 40);
    return QString::fromWCharArray(buf);
}

static QString layoutIdFromHkl(HKL hkl) {
    // LOWORD(hkl) is the LANGID. Standard layouts use KLID == LANGID
    // (e.g. 0x0409 -> "00000409"). This is stable and usable by
    // LoadKeyboardLayoutW as a fallback.
    return QString::asprintf("%08X", (unsigned)((UINT_PTR)hkl & 0xFFFF));
}

ImeManager::ImeManager() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // S_OK means *we* initialized COM and must balance it; S_FALSE means it was
    // already initialized by someone else (e.g. Qt) and is not ours to undo.
    comInitialized_ = (hr == S_OK);
}

ImeManager::~ImeManager() {
    if (profileMgr_) { profileMgr_->Release(); profileMgr_ = nullptr; }
    if (profiles_) { profiles_->Release(); profiles_ = nullptr; }
    if (comInitialized_) CoUninitialize();
}

static QString langName(LANGID langid) {
    wchar_t buf[128] = {0};
    if (GetLocaleInfoW(MAKELCID(langid, SORT_DEFAULT), LOCALE_SLANGUAGE, buf, 128) > 0)
        return QString::fromWCharArray(buf);
    return QString::asprintf("Lang 0x%04X", (unsigned)langid);
}

QVector<ImeEntry> ImeManager::enumerateLayouts() {
    QVector<ImeEntry> out;
    const int n = GetKeyboardLayoutList(0, nullptr);
    if (n <= 0) return out;

    std::vector<HKL> list((size_t)n);
    GetKeyboardLayoutList(n, list.data());

    for (HKL hkl : list) {
        LANGID langid = LOWORD((UINT_PTR)hkl);
        ImeEntry e;
        e.kind = ImeEntry::Kind::KeyboardLayout;
        e.hkl = hkl;
        e.langid = langid;
        e.layoutId = layoutIdFromHkl(hkl);
        e.id = "layout:" + e.layoutId;
        e.isEnglish = (PRIMARYLANGID(langid) == LANG_ENGLISH);
        e.name = langName(langid);
        out.push_back(e);
    }
    return out;
}

QVector<ImeEntry> ImeManager::enumerateTextServices() {
    QVector<ImeEntry> out;
    if (!ensureProfiles()) return out;

    ULONG count = 0;
    LANGID* langs = nullptr;
    if (FAILED(profiles_->GetLanguageList(&langs, &count))) return out;
    if (!langs || count == 0) return out;

    for (ULONG i = 0; i < count; ++i) {
        LANGID langid = langs[i];
        IEnumTfLanguageProfiles* penum = nullptr;
        if (FAILED(profiles_->EnumLanguageProfiles(langid, &penum)) || !penum)
            continue;

        TF_LANGUAGEPROFILE prof{};
        ULONG fetched = 0;
        while (penum->Next(1, &prof, &fetched) == S_OK && fetched == 1) {
            if (prof.catid == GUID_TFCAT_TIP_KEYBOARD) {
                ImeEntry e;
                e.kind = ImeEntry::Kind::TextService;
                e.langid = prof.langid;
                e.clsid = prof.clsid;
                e.guidProfile = prof.guidProfile;
                e.id = "tsf:" + guidToString(prof.clsid) + ":" + guidToString(prof.guidProfile);
                e.isEnglish = (PRIMARYLANGID(prof.langid) == LANG_ENGLISH);

                BSTR desc = nullptr;
                if (SUCCEEDED(profiles_->GetLanguageProfileDescription(
                        prof.clsid, prof.langid, prof.guidProfile, &desc)) && desc) {
                    e.name = QString::fromWCharArray(desc);
                    SysFreeString(desc);
                }
                if (e.name.isEmpty())
                    e.name = langName(prof.langid);

                // skip keyboard-layout-style text services we already report
                bool dup = false;
                for (const auto& x : out)
                    if (x.id == e.id) { dup = true; break; }
                if (!dup) out.push_back(e);
            }
            prof = TF_LANGUAGEPROFILE{};
        }
        penum->Release();
    }
    CoTaskMemFree(langs);

    // sort: English first, then by name
    std::sort(out.begin(), out.end(), [](const ImeEntry& a, const ImeEntry& b) {
        if (a.isEnglish != b.isEnglish) return a.isEnglish;
        return a.name < b.name;
    });
    return out;
}

QVector<ImeEntry> ImeManager::enumerate() {
    if (!enumerated_) {
        cache_.clear();
        const QVector<ImeEntry> ts = enumerateTextServices();
        const QVector<ImeEntry> kl = enumerateLayouts();
        for (const auto& e : ts) cache_.push_back(e);
        for (const auto& e : kl) cache_.push_back(e);
        enumerated_ = true;
    }
    return cache_;
}

bool ImeManager::hasEnglish() const {
    for (const auto& e : cache_)
        if (e.isEnglish) return true;
    return false;
}

QString ImeManager::englishId() const {
    // prefer an English keyboard layout
    for (const auto& e : cache_)
        if (e.isEnglish && e.kind == ImeEntry::Kind::KeyboardLayout) return e.id;
    for (const auto& e : cache_)
        if (e.isEnglish) return e.id;
    return QString();
}

ImeEntry ImeManager::findById(const QString& id) const {
    for (const auto& e : cache_)
        if (e.id == id) return e;
    return ImeEntry{};
}

bool ImeManager::ensureProfiles() {
    if (profiles_) return true;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
                                  (void**)&profiles_);
    if (FAILED(hr) || !profiles_) {
        lastError_ = trc("Failed to initialize the TSF input service");
        return false;
    }
    profiles_->QueryInterface(IID_ITfInputProcessorProfileMgr, (void**)&profileMgr_);
    return true;
}

HKL ImeManager::findHklForLang(LANGID langid) const {
    for (const auto& e : cache_)
        if (e.kind == ImeEntry::Kind::KeyboardLayout && e.langid == langid)
            return e.hkl;
    // fall back: load by known layout id
    return nullptr;
}

bool ImeManager::activateLayout(const ImeEntry& e) {
    if (!e.hkl) return false;
    // Make sure it is loaded system-wide, then request foreground windows
    // switch to it.
    HKL loaded = LoadKeyboardLayoutW((LPCWSTR)e.layoutId.utf16(), KLF_ACTIVATE);
    if (!loaded) loaded = e.hkl;
    ActivateKeyboardLayout(loaded, 0);
    // Session-wide request so the foreground (game) window switches too.
    SendMessageTimeoutW(HWND_BROADCAST, WM_INPUTLANGCHANGEREQUEST, 0,
                        (LPARAM)loaded, SMTO_ABORTIFHUNG, 2000, nullptr);
    return true;
}

bool ImeManager::activateTextService(const ImeEntry& e) {
    if (!ensureProfiles() || !profileMgr_) return false;

    HKL hkl = findHklForLang(e.langid);
    if (!hkl) {
        // try to load the language's default keyboard layout
        wchar_t lid[16];
        swprintf_s(lid, L"%08X", (unsigned)e.langid);
        hkl = LoadKeyboardLayoutW(lid, KLF_ACTIVATE);
    }

    HRESULT hr = profileMgr_->ActivateProfile(
        TF_PROFILETYPE_INPUTPROCESSOR, e.langid, e.clsid, e.guidProfile, hkl,
        TF_IPPMF_FORPROCESS | TF_IPPMF_FORSESSION);
    if (FAILED(hr)) {
        lastError_ = trc("Failed to switch input method") + QString(" (0x%1)").arg((unsigned)hr, 8, 16, QChar('0'));
        return false;
    }
    // Also switch the language for foreground windows.
    if (hkl)
        SendMessageTimeoutW(HWND_BROADCAST, WM_INPUTLANGCHANGEREQUEST, 0,
                            (LPARAM)hkl, SMTO_ABORTIFHUNG, 2000, nullptr);
    return true;
}

bool ImeManager::switchToEnglish(QString* errMsg) {
    enumerate();
    const QString id = englishId();
    if (id.isEmpty()) {
        lastError_ = trc("No English input method / keyboard layout detected");
        if (errMsg) *errMsg = lastError_;
        return false;
    }
    bool ok = switchTo(id, errMsg);
    if (!ok && errMsg) *errMsg = lastError_;
    return ok;
}

bool ImeManager::switchTo(const QString& id, QString* errMsg) {
    enumerate();
    const ImeEntry e = findById(id);
    if (e.id.isEmpty()) {
        lastError_ = trc("The target input method does not exist or was uninstalled");
        if (errMsg) *errMsg = lastError_;
        return false;
    }
    bool ok = (e.kind == ImeEntry::Kind::TextService) ? activateTextService(e)
                                                      : activateLayout(e);
    if (!ok) lastError_ = trc("Failed to switch input method");
    if (!ok && errMsg) *errMsg = lastError_;
    return ok;
}

QString ImeManager::currentDescription() const {
    if (!profiles_) {
        // best-effort without TSF
        HKL hkl = GetKeyboardLayout(0);
        return langName(LOWORD((UINT_PTR)hkl));
    }
    LANGID langid = 0;
    CLSID clsid{};
    GUID prof{};
    if (SUCCEEDED(profiles_->GetCurrentLanguage(&langid))) {
        // active text service for current language
        if (SUCCEEDED(profiles_->GetActiveLanguageProfile(clsid, &langid, &prof))) {
            BSTR desc = nullptr;
            if (SUCCEEDED(profiles_->GetLanguageProfileDescription(clsid, langid, prof, &desc)) && desc) {
                QString s = QString::fromWCharArray(desc);
                SysFreeString(desc);
                return s;
            }
        }
    }
    HKL hkl = GetKeyboardLayout(0);
    return langName(LOWORD((UINT_PTR)hkl));
}

bool ImeManager::capsLockOn() {
    return (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
}

} // namespace wkg
