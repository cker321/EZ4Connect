#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QCloseEvent>
#include <QSettings>
#include <QPointer>
#include <QHash>

#include "application/connectionsession.h"
#include "application/connectionsessionmanager.h"
#include "application/profileservice.h"
#include "application/systemproxysession.h"
#include "presentation/dialogs/settingwindow/settingwindow.h"
#include "presentation/coordinators/authdialogcoordinator.h"
#include "presentation/main/mainwindowcoordinator.h"

namespace Ui
{
    class MainWindow;
}

class ApplicationLogger;
class ApplicationLogFile;
class ConnectionUiController;
class UpdateChecker;
class QLabel;
class QPushButton;
class QTabWidget;
class QWidget;

// Per-tab connection controls, created dynamically for each VPN profile.
struct ConnectionTabWidgets
{
    QWidget *page = nullptr;
    QLabel *statusIndicator = nullptr;
    QLabel *statusTitleLabel = nullptr;
    QLabel *statusDetailLabel = nullptr;
    QPushButton *connectButton = nullptr;
    QPushButton *proxyButton = nullptr;
    QLabel *profileNameLabel = nullptr;
    QLabel *profileDetailLabel = nullptr;
};

class MainWindow : public QMainWindow
{
Q_OBJECT

public:
    explicit MainWindow(
        ApplicationLogger *logger,
        ApplicationLogFile *logFile,
        QWidget *parent = nullptr
    );

    ~MainWindow() override;

public slots:

    void cleanUpWhenQuit();

protected:
    void closeEvent(QCloseEvent *e) override;

    void changeEvent(QEvent *event) override;

private:
    void upgradeSettings();

    void clearLog();

    void resetZjuConnectUi();

    void applyColorScheme(Qt::ColorScheme scheme);

    void updateConnectionState(ConnectionState state);

    void updateTabConnectionState(const QString &profileId, ConnectionState state);

    void updateProfileSummary();

    void showNotification(
        const QString &title,
        const QString &content,
        QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::MessageIcon::Information
    );

    void updateVersionInfo();

    void setupTrayIcon();

    void setupProfileMenu();

    void refreshProfileMenu();

    void setupConnectionTabs();

    void addConnectionTab(const QString &profileId, bool makeCurrent);

    bool switchProfile(const QString &profileId);

    void createProfile();

    void openConfigurationGuide();

    void promptConfigurationGuide(
        const QString &windowTitle,
        const QString &text
    );

    void promptFirstLaunchGuide();

    void renameCurrentProfile();

    void deleteCurrentProfile();

    void gracefullyQuit();

    // Build the per-tab widgets for one profile and wire a controller.
    void buildTabPage(const QString &profileId, ConnectionTabWidgets &widgets);

    // Return the (cached) settings for a profile. The default profile maps to
    // the active `settings` member; named profiles get their own persistent
    // QSettings owned by MainWindow.
    QSettings *settingsForProfile(const QString &profileId);

    QString activeTabProfileId() const;

    Ui::MainWindow *ui;
    QSystemTrayIcon *trayIcon;
    QMenu *trayMenu;
    QMenu *trayProfileMenu;
    QAction *trayConnectAction;
    QAction *trayShowAction;
    QAction *trayCloseAction;
    QAction *newProfileAction;
    QAction *renameProfileAction;
    QAction *deleteProfileAction;

    ConnectionSessionManager *sessionManager = nullptr;
    SystemProxySession *systemProxySession = nullptr;
    AuthDialogCoordinator *authenticationDialogs = nullptr;
    MainWindowCoordinator *coordinator;
    ApplicationLogger *applicationLogger;
    ApplicationLogFile *applicationLogFile;
    UpdateChecker *updateChecker;
    QSettings *settings;
    ProfileService *profileService;

    // Per-profile tab page widgets and controllers.
    QHash<QString, ConnectionTabWidgets> tabWidgets;
    QHash<QString, ConnectionUiController *> tabControllers;

    // Per-profile persistent settings (owned by MainWindow, freed on destruction).
    QHash<QString, QSettings *> profileSettings;

    QPointer<SettingWindow> settingWindow;

};

#endif //MAINWINDOW_H
