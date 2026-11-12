#ifndef MAINWINDOWCOORDINATOR_H
#define MAINWINDOWCOORDINATOR_H

#include <QObject>
#include <QString>

class AuthDialogCoordinator;
class ConnectionSession;
class ConnectionSessionManager;
class ProfileService;
class SystemProxySession;
class UpdateChecker;
class QWidget;

class MainWindowCoordinator : public QObject
{
    Q_OBJECT

public:
    MainWindowCoordinator(
        QWidget *parentWidget,
        const QString &overrideConfigPath,
        QObject *parent = nullptr
    );

    ProfileService *profiles() const;
    ConnectionSessionManager *sessions() const;
    SystemProxySession *systemProxy() const;
    UpdateChecker *updates() const;
    AuthDialogCoordinator *authenticationDialogs() const;

    void prepareForShutdown();

    // Connect a session's authentication signals so that interactive input is
    // routed back to the correct session.
    void wireSession(const QString &profileId, ConnectionSession *session);

private:
    ProfileService *profileService;
    ConnectionSessionManager *sessionManager;
    SystemProxySession *systemProxySession;
    UpdateChecker *updateChecker;
    AuthDialogCoordinator *authenticationCoordinator;

    // The profile currently awaiting interactive authentication input.
    QString activeAuthProfileId;
};

#endif // MAINWINDOWCOORDINATOR_H
