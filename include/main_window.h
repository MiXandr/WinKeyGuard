#pragma once
#include <QMainWindow>

#include "config.h"
#include "guard_controller.h"

class QLabel;
class QTableWidget;
class QComboBox;
class QCheckBox;
class QSystemTrayIcon;
class QLineEdit;
class QListWidget;
class QPushButton;
class QAction;

namespace wkg {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(ConfigStore& store, GuardController& controller, QWidget* parent = nullptr);
    ~MainWindow() override;

    void setupTray();
    void showStatusMessage(const QString& msg);

protected:
    void closeEvent(QCloseEvent* ev) override;

private slots:
    void onProtectionChanged(bool active, const QString& exe, const QString& mode);
    void onCapsChanged(bool on, bool imeLock, const QString& ime);
    void onImeSwitched(const QString& desc, bool ok, const QString& err);
    void onAdminWarning(const QString& exe);

    // targets
    void addTargetByName();
    void pickTargetFromProcesses();
    void browseTargetExe();
    void removeSelectedTarget();
    void targetsChanged(int row, int col);

    // rules
    void addCustomRule();
    void removeSelectedRule();
    void resetRules();
    void rulesChanged(int row, int col);

    // settings
    void applySettings();

    void recordCombo(RecordedCombo& out);

private:
    void buildUi();
    void applyTheme();
    void rebuildTargetsTable();
    void rebuildRulesTable();
    void rebuildImeCombo();
    void rebuildStatus();
    void saveConfig();
    void applyAutostart();
    QString emergencyText() const;

    ConfigStore& store_;
    GuardController& ctrl_;

    QLabel* statusDot_ = nullptr;
    QLabel* statusTitle_ = nullptr;
    QLabel* statusDetail_ = nullptr;
    QLabel* capsLabel_ = nullptr;

    QTableWidget* targetsTable_ = nullptr;
    QTableWidget* rulesTable_ = nullptr;
    QListWidget* customRulesList_ = nullptr;

    QCheckBox* chkWinTick_ = nullptr;
    QCheckBox* chkAutostart_ = nullptr;
    QComboBox* comboFullscreenMode_ = nullptr;
    QCheckBox* chkZoneLeft_ = nullptr;
    QCheckBox* chkZoneCenter_ = nullptr;
    QCheckBox* chkZoneRight_ = nullptr;

    QCheckBox* chkImeEnabled_ = nullptr;
    QComboBox* comboCapsOff_ = nullptr;
    QPushButton* btnRecordEmergency_ = nullptr;
    QLabel* emergencyLabel_ = nullptr;
    QComboBox* comboLanguage_ = nullptr;

    QSystemTrayIcon* tray_ = nullptr;
    QAction* actPause_ = nullptr;

    QVector<ImeEntry> imeEntries_;

    void rebuildCustomRulesList();
};

} // namespace wkg
