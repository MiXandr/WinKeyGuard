#include "main_window.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "common.h"
#include "process_info.h"

namespace wkg {

static bool isDarkTheme() {
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    return scheme == Qt::ColorScheme::Dark;
}

MainWindow::MainWindow(ConfigStore& store, GuardController& controller, QWidget* parent)
    : QMainWindow(parent), store_(store), ctrl_(controller) {
    buildUi();
    setupTray();
    applyTheme();

    connect(&controller, &GuardController::protectionChanged,
            this, &MainWindow::onProtectionChanged);
    connect(&controller, &GuardController::capsChanged,
            this, &MainWindow::onCapsChanged);
    connect(&controller, &GuardController::imeSwitched,
            this, &MainWindow::onImeSwitched);
    connect(&controller, &GuardController::adminWarning,
            this, &MainWindow::onAdminWarning);

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this](Qt::ColorScheme) { applyTheme(); });

    rebuildTargetsTable();
    rebuildRulesTable();
    rebuildCustomRulesList();
    rebuildStatus();
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* ev) {
    // Minimize to tray instead of quitting.
    hide();
    if (tray_) tray_->showMessage("WinKeyGuard", wgTr("Minimized to the system tray; still running in the background."));
    ev->ignore();
}

void MainWindow::buildUi() {
    setWindowTitle("WinKeyGuard");
    setWindowIcon(QIcon(":/icon.ico"));

    auto* central = new QWidget(this);
    central->setObjectName("central");
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(16, 16, 16, 12);
    root->setSpacing(12);

    // ---- status header ----
    auto* header = new QWidget(central);
    header->setObjectName("header");
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(14, 12, 14, 12);
    hl->setSpacing(12);

    statusDot_ = new QLabel(header);
    statusDot_->setObjectName("statusDot");
    statusDot_->setFixedSize(12, 12);
    statusDot_->setText("●");

    auto* hv = new QVBoxLayout();
    hv->setSpacing(2);
    statusTitle_ = new QLabel("WinKeyGuard", header);
    statusTitle_->setObjectName("appTitle");
    statusDetail_ = new QLabel(wgTr("Initializing…"), header);
    statusDetail_->setObjectName("appStatus");
    hv->addWidget(statusTitle_);
    hv->addWidget(statusDetail_);
    hl->addWidget(statusDot_, 0, Qt::AlignTop | Qt::AlignLeft);
    hl->addLayout(hv, 1);

    capsLabel_ = new QLabel("", header);
    capsLabel_->setObjectName("capsBadge");
    hl->addWidget(capsLabel_, 0, Qt::AlignRight | Qt::AlignVCenter);

    root->addWidget(header);

    auto* tabs = new QTabWidget(central);
    tabs->setObjectName("tabs");
    root->addWidget(tabs, 1);

    // ================= Tab 1: status / rules =================
    auto* statusTab = new QWidget(tabs);
    auto* sl = new QVBoxLayout(statusTab);
    rulesTable_ = new QTableWidget(statusTab);
    rulesTable_->setColumnCount(2);
    rulesTable_->setHorizontalHeaderLabels({wgTr("Shortcut"), wgTr("Enabled")});
    rulesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    rulesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    rulesTable_->verticalHeader()->setVisible(false);
    rulesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    rulesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(rulesTable_, &QTableWidget::cellClicked, this, &MainWindow::rulesChanged);
    sl->addWidget(new QLabel(wgTr("Windows shortcuts blocked during game protection:")));
    sl->addWidget(rulesTable_, 1);
    tabs->addTab(statusTab, wgTr("Status"));

    // ================= Tab 2: targets =================
    auto* tTab = new QWidget(tabs);
    auto* tl = new QVBoxLayout(tTab);
    targetsTable_ = new QTableWidget(tTab);
    targetsTable_->setColumnCount(3);
    targetsTable_->setHorizontalHeaderLabels(
        {wgTr("Process (EXE)"), wgTr("Enabled"), wgTr("Foreground mode")});
    targetsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    targetsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    targetsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    targetsTable_->verticalHeader()->setVisible(false);
    targetsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    targetsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(targetsTable_, &QTableWidget::cellClicked, this, &MainWindow::targetsChanged);
    tl->addWidget(targetsTable_, 1);

    auto* btnRow = new QHBoxLayout();
    auto* btnAdd = new QPushButton(wgTr("Add EXE"));
    auto* btnPick = new QPushButton(wgTr("Pick from running processes"));
    auto* btnBrowse = new QPushButton(wgTr("Browse EXE"));
    auto* btnRemove = new QPushButton(wgTr("Remove"));
    btnRow->addWidget(btnAdd);
    btnRow->addWidget(btnPick);
    btnRow->addWidget(btnBrowse);
    btnRow->addWidget(btnRemove);
    tl->addLayout(btnRow);
    connect(btnAdd, &QPushButton::clicked, this, &MainWindow::addTargetByName);
    connect(btnPick, &QPushButton::clicked, this, &MainWindow::pickTargetFromProcesses);
    connect(btnBrowse, &QPushButton::clicked, this, &MainWindow::browseTargetExe);
    connect(btnRemove, &QPushButton::clicked, this, &MainWindow::removeSelectedTarget);
    tabs->addTab(tTab, wgTr("Targets"));

    // ================= Tab 3: rules management =================
    auto* rTab = new QWidget(tabs);
    auto* rl = new QVBoxLayout(rTab);
    auto* hint = new QLabel(wgTr(
        "Custom rules are matched as \"modifier + specific key\".\n"
        "Click \"Record shortcut\" and press the combination directly.\n"
        "Win / Ctrl / Alt / Shift themselves are never disabled."));
    hint->setWordWrap(true);
    rl->addWidget(hint);

    rl->addWidget(new QLabel(wgTr("Custom rules added:")));
    customRulesList_ = new QListWidget(rTab);
    customRulesList_->setSelectionMode(QAbstractItemView::SingleSelection);
    rl->addWidget(customRulesList_, 1);

    auto* rbtnRow = new QHBoxLayout();
    auto* btnRec = new QPushButton(wgTr("Record shortcut"));
    auto* btnDel = new QPushButton(wgTr("Remove selected rule"));
    auto* btnReset = new QPushButton(wgTr("Restore default rules"));
    rbtnRow->addWidget(btnRec);
    rbtnRow->addWidget(btnDel);
    rbtnRow->addWidget(btnReset);
    rl->addLayout(rbtnRow);
    connect(btnRec, &QPushButton::clicked, this, &MainWindow::addCustomRule);
    connect(btnDel, &QPushButton::clicked, this, &MainWindow::removeSelectedRule);
    connect(btnReset, &QPushButton::clicked, this, &MainWindow::resetRules);
    tabs->addTab(rTab, wgTr("Shortcut Rules"));

    // ================= Tab 4: settings =================
    auto* sTab = new QWidget(tabs);
    auto* sl2 = new QVBoxLayout(sTab);
    auto* form = new QFormLayout();

    chkWinTick_ = new QCheckBox(wgTr("Enable Win + ` to open the native Windows UI"));
    chkWinTick_->setChecked(store_.data().winTickEnabled);
    form->addRow(wgTr("Win + ` passthrough:"), chkWinTick_);

    comboFullscreenMode_ = new QComboBox();
    comboFullscreenMode_->addItem(wgTr("Auto (fullscreen / borderless detection)"), "auto");
    comboFullscreenMode_->addItem(wgTr("Foreground-window mode (compatibility)"), "foreground");
    const int fmIdx = comboFullscreenMode_->findData(store_.data().fullscreenMode);
    comboFullscreenMode_->setCurrentIndex(fmIdx >= 0 ? fmIdx : 0);
    form->addRow(wgTr("Fullscreen detection:"), comboFullscreenMode_);

    comboLanguage_ = new QComboBox();
    comboLanguage_->addItem(wgTr("Follow system"), "system");
    comboLanguage_->addItem("English", "en");
    comboLanguage_->addItem("简体中文", "zh");
    const int langIdx = comboLanguage_->findData(store_.data().language);
    comboLanguage_->setCurrentIndex(langIdx >= 0 ? langIdx : 0);
    form->addRow(wgTr("Language:"), comboLanguage_);

    chkZoneLeft_ = new QCheckBox(wgTr("Left-hand zone"));
    chkZoneCenter_ = new QCheckBox(wgTr("Center zone"));
    chkZoneRight_ = new QCheckBox(wgTr("Right-hand zone"));
    chkZoneLeft_->setChecked(store_.data().zoneLeft);
    chkZoneCenter_->setChecked(store_.data().zoneCenter);
    chkZoneRight_->setChecked(store_.data().zoneRight);
    auto* zoneRow = new QHBoxLayout();
    zoneRow->addWidget(chkZoneLeft_);
    zoneRow->addWidget(chkZoneCenter_);
    zoneRow->addWidget(chkZoneRight_);
    form->addRow(wgTr("Keyboard zone protection:"), zoneRow);

    chkAutostart_ = new QCheckBox(wgTr("Run at Windows startup"));
    chkAutostart_->setChecked(store_.data().autostart);
    form->addRow(wgTr("Startup:"), chkAutostart_);

    auto* adminBtn = new QPushButton(wgTr("Run as administrator"));
    form->addRow(wgTr("Administrator:"), adminBtn);
    connect(adminBtn, &QPushButton::clicked, this, [this]() {
        if (relaunchElevated()) {
            tray_->showMessage("WinKeyGuard", wgTr("Requested to run as administrator (UAC)."));
        } else {
            QMessageBox::information(this, "WinKeyGuard", wgTr("Cancelled, or could not elevate."));
        }
    });

    // ---- IME linkage section ----
    chkImeEnabled_ = new QCheckBox(wgTr("Caps Lock / IME linkage"));
    chkImeEnabled_->setChecked(store_.data().ime.enabled);
    form->addRow(wgTr("IME linkage:"), chkImeEnabled_);

    comboCapsOff_ = new QComboBox();
    form->addRow(wgTr("After Caps Lock off:"), comboCapsOff_);
    form->addRow("", new QLabel(wgTr(
        "Caps Lock ON: switch to English and block the IME switch hotkeys.\n"
        "Caps Lock OFF: apply the option above and restore normal switching.")));
    rebuildImeCombo();

    auto* emergencyRow = new QHBoxLayout();
    btnRecordEmergency_ = new QPushButton(wgTr("Record"));
    emergencyLabel_ = new QLabel(emergencyText());
    emergencyRow->addWidget(emergencyLabel_);
    emergencyRow->addWidget(btnRecordEmergency_);
    form->addRow(wgTr("Emergency shortcut:"), emergencyRow);
    connect(btnRecordEmergency_, &QPushButton::clicked, this, [this]() {
        RecordedCombo c;
        recordCombo(c);
        if (!c.cancelled && c.mods != 0) {
            store_.data().emergencyMods = c.mods;
            store_.data().emergencyVk = c.vk;
            ctrl_.applyConfigChanged();
            saveConfig();
            emergencyLabel_->setText(emergencyText());
        }
    });

    sl2->addLayout(form);
    sl2->addStretch(1);
    tabs->addTab(sTab, wgTr("Settings"));

    // ---- apply / close buttons ----
    auto* bottom = new QHBoxLayout();
    auto* btnApply = new QPushButton(wgTr("Apply & Save"));
    auto* btnQuit = new QPushButton(wgTr("Quit"));
    bottom->addStretch(1);
    bottom->addWidget(btnApply);
    bottom->addWidget(btnQuit);
    root->addLayout(bottom);
    connect(btnApply, &QPushButton::clicked, this, &MainWindow::applySettings);
    connect(btnQuit, &QPushButton::clicked, this, []() { QApplication::quit(); });

    setCentralWidget(central);
    resize(620, 640);
}

QString MainWindow::emergencyText() const {
    return QString::fromStdString(comboName(store_.data().emergencyMods,
                                            store_.data().emergencyVk, 0));
}

void MainWindow::recordCombo(RecordedCombo& out) {
    QDialog dlg(this);
    dlg.setWindowTitle(wgTr("Record shortcut"));
    auto* l = new QVBoxLayout(&dlg);
    l->addWidget(new QLabel(wgTr("Press a key combination (e.g. Win + D).\nEsc to cancel.")));
    dlg.resize(320, 140);

    ctrl_.beginRecord();
    QTimer poller;
    connect(&poller, &QTimer::timeout, &dlg, [&]() {
        if (ctrl_.pollRecord(out)) {
            dlg.accept();
        }
    });
    poller.start(30);
    dlg.exec();
    poller.stop();

    // If the dialog was dismissed (e.g. closed via title-bar X) without a
    // recorded combo, make sure the hook leaves recording mode.
    if (ctrl_.recording()) ctrl_.cancelRecord();
}

void MainWindow::rebuildTargetsTable() {
    const auto& targets = store_.data().targets;
    targetsTable_->setRowCount(targets.size());
    for (int i = 0; i < targets.size(); ++i) {
        const TargetEntry& t = targets[i];
        auto* name = new QTableWidgetItem(t.name);
        name->setFlags(name->flags() | Qt::ItemIsEditable);
        targetsTable_->setItem(i, 0, name);

        auto* en = new QTableWidgetItem();
        en->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        en->setCheckState(t.enabled ? Qt::Checked : Qt::Unchecked);
        targetsTable_->setItem(i, 1, en);

        auto* fg = new QTableWidgetItem();
        fg->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        fg->setCheckState(t.foregroundOnly ? Qt::Checked : Qt::Unchecked);
        fg->setToolTip(wgTr("Foreground mode: protect on match (skip fullscreen detection)"));
        targetsTable_->setItem(i, 2, fg);
    }
}

void MainWindow::rebuildRulesTable() {
    const auto& rules = store_.data().rules;
    rulesTable_->setRowCount(rules.size());
    for (int i = 0; i < rules.size(); ++i) {
        const HotkeyRule& r = rules[i];
        auto* name = new QTableWidgetItem(QString::fromStdString(r.display));
        if (r.id == "win") name->setText(wgTr("Win (key itself)"));
        rulesTable_->setItem(i, 0, name);

        auto* en = new QTableWidgetItem();
        en->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        en->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
        rulesTable_->setItem(i, 1, en);
    }
}

void MainWindow::rebuildImeCombo() {
    if (!comboCapsOff_) return;
    const QString prev = comboCapsOff_->currentData().toString();
    comboCapsOff_->blockSignals(true);
    comboCapsOff_->clear();
    comboCapsOff_->addItem(wgTr("Do nothing"), "none");

    imeEntries_ = ctrl_.imeList();
    for (const auto& e : imeEntries_) {
        QString label = e.name;
        if (e.isEnglish) label += wgTr(" (English)");
        comboCapsOff_->addItem(label, e.id);
    }
    const int idx = comboCapsOff_->findData(prev);
    comboCapsOff_->setCurrentIndex(idx >= 0 ? idx : 0);
    comboCapsOff_->blockSignals(false);
}

void MainWindow::applyTheme() {
    const bool dark = isDarkTheme();
    const QString text = dark ? "#e8eaed" : "#1f2430";
    const QString muted = dark ? "#9aa0a6" : "#5f6672";
    const QString panel = dark ? "#202124" : "#ffffff";
    const QString base = dark ? "#1b1c1f" : "#f5f6f8";
    const QString border = dark ? "#3c4043" : "#d9dce1";
    const QString accent = "#2f81f7";
    const QString green = "#1a9e50";
    const QString rowAlt = dark ? "#26272b" : "#fbfbfc";

    setStyleSheet(QString(R"(
        QWidget#central { background: %1; color: %2; }
        QWidget#header {
            background: %3; border: 1px solid %4; border-radius: 10px;
        }
        QLabel#appTitle { font-size: 17px; font-weight: 700; color: %2; }
        QLabel#appStatus { font-size: 13px; color: %5; }
        QLabel#capsBadge {
            font-size: 12px; color: %5; background: %1;
            border: 1px solid %4; border-radius: 9px; padding: 3px 10px;
        }
        QLabel#statusDot { color: transparent; border-radius: 6px; background: %6; }
        QLabel#statusDot[active="true"] { background: %7; }

        QTabWidget::pane { border: 1px solid %4; border-radius: 8px; background: %3; top: -1px; }
        QTabBar::tab {
            background: transparent; color: %5; padding: 8px 18px;
            margin-right: 2px; border-bottom: 2px solid transparent;
        }
        QTabBar::tab:selected { color: %8; border-bottom: 2px solid %8; font-weight: 600; }
        QTabBar::tab:hover { color: %2; }

        QTableWidget {
            background: %3; alternate-background-color: %9;
            color: %2; gridline-color: %4; border: 1px solid %4;
            border-radius: 6px; selection-background-color: %8; selection-color: white;
        }
        QHeaderView::section {
            background: %1; color: %5; border: none; border-bottom: 1px solid %4;
            padding: 6px 8px; font-weight: 600;
        }
        QTableWidget::item { padding: 4px 6px; }

        QPushButton {
            background: %8; color: white; border: none; border-radius: 6px;
            padding: 7px 14px; font-weight: 500;
        }
        QPushButton:hover { background: %10; }
        QPushButton:pressed { background: %11; }
        QPushButton:disabled { background: %4; color: %5; }

        QComboBox, QLineEdit {
            background: %3; color: %2; border: 1px solid %4; border-radius: 6px;
            padding: 5px 8px; min-height: 18px;
        }
        QComboBox::drop-down { border: none; width: 22px; }
        QComboBox QAbstractItemView {
            background: %3; color: %2; border: 1px solid %4; selection-background-color: %8;
        }
        QCheckBox { color: %2; spacing: 8px; }
        QCheckBox::indicator { width: 16px; height: 16px; }
        QCheckBox::indicator:unchecked { background: %3; border: 1px solid %4; border-radius: 4px; }
        QCheckBox::indicator:checked { background: %8; border: 1px solid %8; border-radius: 4px; }

        QScrollBar:vertical { background: %1; width: 10px; border-radius: 5px; }
        QScrollBar::handle:vertical { background: %4; border-radius: 5px; min-height: 24px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }

        QMessageBox, QDialog { background: %3; }
    )")
        .arg(base, text, panel, border, muted, muted, green, accent,
             rowAlt, dark ? "#5a9cf9" : "#4a90f4", dark ? "#1a6ad0" : "#2a6fd0"));
}

void MainWindow::rebuildStatus() {
    // placeholder; the signal handler fills in real values
}

void MainWindow::onProtectionChanged(bool active, const QString& exe, const QString& mode) {
    statusDetail_->setText(QString("%1 | %2: %3 | %4: %5")
                               .arg(active ? wgTr("Game protection active") : wgTr("Game protection inactive"),
                                    wgTr("Program"), exe.isEmpty() ? "-" : exe,
                                    wgTr("Window"), mode));
    statusDot_->setProperty("active", active);
    statusDot_->style()->unpolish(statusDot_);
    statusDot_->style()->polish(statusDot_);

    if (tray_) {
        tray_->setToolTip(QString("WinKeyGuard\n%1 | %2").arg(
            active ? wgTr("Game protection: ON") : wgTr("Game protection: OFF"), exe));
    }
}

void MainWindow::onCapsChanged(bool on, bool imeLock, const QString& ime) {
    capsLabel_->setText(QString("Caps Lock: %1 | %2: %3 %4")
                            .arg(on ? "ON" : "OFF", wgTr("IME"), ime,
                                 imeLock ? wgTr("(English locked)") : ""));
}

void MainWindow::onImeSwitched(const QString& desc, bool ok, const QString& err) {
    if (ok) {
        showStatusMessage(wgTr("IME switched: ") + desc);
    } else {
        capsLabel_->setText(wgTr("IME switch failed: ") + desc + " — " + err);
    }
}

void MainWindow::onAdminWarning(const QString& exe) {
    showStatusMessage(wgTr("Note: target program ") + exe +
                      wgTr(" is running elevated; a non-elevated keyboard hook may not fully cover it. Use Settings → Run as administrator."));
}

void MainWindow::showStatusMessage(const QString& msg) {
    if (tray_) tray_->showMessage("WinKeyGuard", msg, QSystemTrayIcon::Information, 4000);
    statusDetail_->setText(msg);
}

// ---------------- targets ----------------
void MainWindow::addTargetByName() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, wgTr("Add target program"),
                                               wgTr("Enter the executable name (e.g. Game.exe):"),
                                               QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    TargetEntry t;
    t.name = name.trimmed();
    t.enabled = true;
    store_.data().targets.push_back(t);
    rebuildTargetsTable();
    saveConfig();
    ctrl_.refreshNow();
}

void MainWindow::pickTargetFromProcesses() {
    const auto procs = listRunningProcesses();
    QDialog dlg(this);
    dlg.setWindowTitle(wgTr("Pick from running processes"));
    auto* l = new QVBoxLayout(&dlg);
    auto* list = new QListWidget(&dlg);
    for (const auto& p : procs) {
        if (p.name.toLower().endsWith(".exe"))
            list->addItem(p.name);
    }
    list->sortItems();
    l->addWidget(list);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    l->addWidget(bb);
    dlg.resize(320, 400);
    if (dlg.exec() != QDialog::Accepted) return;
    auto* item = list->currentItem();
    if (!item) return;
    TargetEntry t;
    t.name = item->text();
    t.enabled = true;
    store_.data().targets.push_back(t);
    rebuildTargetsTable();
    saveConfig();
}

void MainWindow::browseTargetExe() {
    const QString file = QFileDialog::getOpenFileName(this, wgTr("Choose game EXE"), QString(),
                                                      "Executable (*.exe)");
    if (file.isEmpty()) return;
    TargetEntry t;
    t.name = QFileInfo(file).fileName();
    t.enabled = true;
    store_.data().targets.push_back(t);
    rebuildTargetsTable();
    saveConfig();
}

void MainWindow::removeSelectedTarget() {
    const int row = targetsTable_->currentRow();
    if (row < 0 || row >= store_.data().targets.size()) return;
    store_.data().targets.removeAt(row);
    rebuildTargetsTable();
    saveConfig();
    ctrl_.refreshNow();
}

void MainWindow::targetsChanged(int row, int col) {
    if (row < 0 || row >= store_.data().targets.size()) return;
    TargetEntry& t = store_.data().targets[row];
    if (col == 0) {
        auto* item = targetsTable_->item(row, 0);
        if (item) t.name = item->text().trimmed();
    } else if (col == 1) {
        t.enabled = targetsTable_->item(row, 1)->checkState() == Qt::Checked;
    } else if (col == 2) {
        t.foregroundOnly = targetsTable_->item(row, 2)->checkState() == Qt::Checked;
    }
    saveConfig();
    ctrl_.refreshNow();
}

// ---------------- rules ----------------
void MainWindow::rebuildCustomRulesList() {
    if (!customRulesList_) return;
    customRulesList_->clear();
    for (const auto& r : store_.data().rules) {
        if (r.id.rfind("custom_", 0) == 0) {
            customRulesList_->addItem(QString::fromStdString(r.display));
        }
    }
}

void MainWindow::addCustomRule() {
    RecordedCombo c;
    recordCombo(c);
    if (c.cancelled || c.mods == 0) return;

    // Reject duplicates (same mods + key already present).
    for (const auto& x : store_.data().rules) {
        const bool sameKey = (x.scan != 0) ? (x.scan == c.scan) : (x.vk == c.vk);
        if (x.mods == c.mods && sameKey) {
            QMessageBox::information(this, "WinKeyGuard",
                wgTr("This combination is already in the rule list."));
            return;
        }
    }

    HotkeyRule r;
    r.mods = c.mods;
    r.vk = c.vk;
    r.scan = c.scan;
    r.enabled = true;
    r.display = comboName(c.mods, c.vk, c.scan);

    int n = 0;
    for (const auto& x : store_.data().rules)
        if (x.id.rfind("custom_", 0) == 0) n++;
    r.id = "custom_" + std::to_string(n);

    store_.data().rules.push_back(r);
    rebuildRulesTable();
    rebuildCustomRulesList();
    saveConfig();
    ctrl_.publishSnapshot();
    showStatusMessage(wgTr("Rule added: ") + QString::fromStdString(r.display));
}

void MainWindow::removeSelectedRule() {
    if (!customRulesList_) return;
    const int listRow = customRulesList_->currentRow();
    if (listRow < 0) return;

    // Map list row -> custom rule index in store_.data().rules.
    int seen = -1;
    int ruleIndex = -1;
    for (int i = 0; i < store_.data().rules.size(); ++i) {
        if (store_.data().rules[i].id.rfind("custom_", 0) == 0) {
            ++seen;
            if (seen == listRow) { ruleIndex = i; break; }
        }
    }
    if (ruleIndex < 0) return;

    store_.data().rules.erase(store_.data().rules.begin() + ruleIndex);
    rebuildRulesTable();
    rebuildCustomRulesList();
    saveConfig();
    ctrl_.publishSnapshot();
    showStatusMessage(wgTr("Rule removed."));
}

void MainWindow::resetRules() {
    store_.resetRulesToDefault();
    rebuildRulesTable();
    rebuildCustomRulesList();
    saveConfig();
    ctrl_.publishSnapshot();
    showStatusMessage(wgTr("Rules restored to defaults."));
}

void MainWindow::rulesChanged(int row, int col) {
    if (row < 0 || row >= store_.data().rules.size() || col != 1) return;
    store_.data().rules[row].enabled =
        rulesTable_->item(row, 1)->checkState() == Qt::Checked;
    saveConfig();
    ctrl_.publishSnapshot();
}

// ---------------- settings ----------------
void MainWindow::applySettings() {
    store_.data().winTickEnabled = chkWinTick_->isChecked();
    store_.data().fullscreenMode = comboFullscreenMode_->currentData().toString();
    store_.data().zoneLeft = chkZoneLeft_->isChecked();
    store_.data().zoneCenter = chkZoneCenter_->isChecked();
    store_.data().zoneRight = chkZoneRight_->isChecked();
    store_.data().autostart = chkAutostart_->isChecked();
    store_.data().ime.enabled = chkImeEnabled_->isChecked();
    store_.data().language = comboLanguage_->currentData().toString();

    const QVariant cd = comboCapsOff_->currentData();
    if (cd.isValid() && cd.toString() == "none") {
        store_.data().ime.capsOffAction = CapsOffAction::Nothing;
        store_.data().ime.capsOffTarget.clear();
    } else if (cd.isValid()) {
        store_.data().ime.capsOffAction = CapsOffAction::SwitchToSpecific;
        store_.data().ime.capsOffTarget = cd.toString();
    }

    saveConfig();
    ctrl_.applyConfigChanged();
    applyAutostart();
    rebuildStatus();
    showStatusMessage(wgTr("Settings saved"));
}

void MainWindow::applyAutostart() {
    QSettings s("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                QSettings::NativeFormat);
    if (store_.data().autostart) {
        s.setValue("WinKeyGuard", QDir::toNativeSeparators(QApplication::applicationFilePath()));
    } else {
        s.remove("WinKeyGuard");
    }
}

void MainWindow::saveConfig() {
    store_.save();
}

void MainWindow::setupTray() {
    tray_ = new QSystemTrayIcon(windowIcon(), this);
    auto* menu = new QMenu(this);
    auto* title = menu->addAction("WinKeyGuard");
    title->setEnabled(false);
    menu->addSeparator();

    actPause_ = menu->addAction(wgTr("Pause protection"));
    actPause_->setCheckable(true);
    connect(actPause_, &QAction::toggled, this, [this](bool paused) {
        ctrl_.setUserPaused(paused);
        actPause_->setText(paused ? wgTr("Resume protection") : wgTr("Pause protection"));
    });

    auto* actEmerg = menu->addAction(wgTr("Emergency suspend/resume (Ctrl+Alt+F12)"));
    connect(actEmerg, &QAction::triggered, this, [this]() { ctrl_.toggleEmergency(); });

    menu->addSeparator();
    auto* actOpen = menu->addAction(wgTr("Open settings"));
    connect(actOpen, &QAction::triggered, this, [this]() {
        show();
        raise();
        activateWindow();
    });
    auto* actQuit = menu->addAction(wgTr("Quit"));
    connect(actQuit, &QAction::triggered, this, []() { QApplication::quit(); });

    tray_->setContextMenu(menu);
    tray_->show();
}

} // namespace wkg
