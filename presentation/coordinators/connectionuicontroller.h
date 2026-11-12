#ifndef CONNECTIONUICONTROLLER_H
#define CONNECTIONUICONTROLLER_H

#include <QObject>
#include <QSystemTrayIcon>

#include <functional>

#include "core/connectionerror.h"

class QAction;
class ApplicationLogger;
class AuthDialogCoordinator;
class ConnectionSession;
class ConnectionSessionManager;
class QPushButton;
class QSettings;
class SystemProxySession;
class QWidget;

// Controls one tab's connection controls. Each VPN profile tab owns its own
// ConnectionUiController bound to the profile's dedicated ConnectionSession,
// so multiple connections can be driven independently.
class ConnectionUiController : public QObject
{
    Q_OBJECT

public:
    using SettingsProvider = std::function<QSettings *()>;
    using NotificationHandler = std::function<void(
        const QString &title,
        const QString &content,
        QSystemTrayIcon::MessageIcon icon
    )>;
    using PortOverrideProvider = std::function<void(int &socksPort, int &httpPort)>;

    ConnectionUiController(
        QWidget *parentWidget,
        QPushButton *connectButton,
        QPushButton *proxyButton,
        QAction *trayConnectAction,
        ConnectionSession *connectionSession,
        SystemProxySession *systemProxySession,
        AuthDialogCoordinator *authenticationDialogs,
        ApplicationLogger *applicationLogger,
        SettingsProvider settingsProvider,
        QString profileId,
        NotificationHandler notificationHandler,
        QObject *parent = nullptr
    );

    // Let the UI reflect the current session state (used on tab switch / init).
    void refreshButtonState();

private:
    void handleConnectClicked();
    void handleProxyClicked();
    void startConnection(
        const QString &username,
        const QString &password,
        const QString &phone = QString()
    );
    void showConnectionError(ZJU_ERROR error);
    QSettings *settings() const;

    QWidget *parentWidget;
    QPushButton *connectButton;
    QPushButton *proxyButton;
    QAction *trayConnectAction;
    ConnectionSession *connectionSession;
    SystemProxySession *systemProxySession;
    AuthDialogCoordinator *authenticationDialogs;
    SettingsProvider settingsProvider;
    QString profileId;
    NotificationHandler notificationHandler;
};

#endif // CONNECTIONUICONTROLLER_H
