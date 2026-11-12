#include "mainwindowcoordinator.h"

#include <memory>

#include "application/connectionsession.h"
#include "application/connectionsessionmanager.h"
#include "application/profileservice.h"
#include "application/systemproxysession.h"
#include "infrastructure/platform/platformsystemproxybackend.h"
#include "infrastructure/settings/profilemanager.h"
#include "infrastructure/update/updatechecker.h"
#include "presentation/coordinators/authdialogcoordinator.h"

MainWindowCoordinator::MainWindowCoordinator(
    QWidget *parentWidget,
    const QString &overrideConfigPath,
    QObject *parent
)
    : QObject(parent),
      profileService(new ProfileService(
          std::make_unique<ProfileManager>(),
          overrideConfigPath,
          this
      )),
      sessionManager(new ConnectionSessionManager(this)),
      systemProxySession(new SystemProxySession(
          std::make_unique<PlatformSystemProxyBackend>(),
          this
      )),
      updateChecker(new UpdateChecker(this)),
      authenticationCoordinator(new AuthDialogCoordinator(
          parentWidget,
          profileService->settings(),
          this
      ))
{
    connect(
        systemProxySession,
        &SystemProxySession::operationFinished,
        this,
        [this](bool enabled)
        {
            if (enabled && !sessionManager->anyActive())
            {
                systemProxySession->disable();
            }
        }
    );

    connect(
        authenticationCoordinator,
        &AuthDialogCoordinator::sudoPasswordSubmitted,
        this,
        [this](const QString &password, bool remember)
        {
            ConnectionSession *session =
                sessionManager->find(activeAuthProfileId);
            if (session != nullptr)
            {
                session->submitSudoPassword(password, remember);
            }
        }
    );
    connect(
        authenticationCoordinator,
        &AuthDialogCoordinator::interactiveInputSubmitted,
        this,
        [this](const QByteArray &input)
        {
            ConnectionSession *session =
                sessionManager->find(activeAuthProfileId);
            if (session != nullptr)
            {
                session->submitInput(input);
            }
        }
    );
    connect(
        authenticationCoordinator,
        &AuthDialogCoordinator::interactiveInputCancelled,
        this,
        [this]()
        {
            ConnectionSession *session =
                sessionManager->find(activeAuthProfileId);
            if (session != nullptr)
            {
                session->cancelInteractiveInput();
            }
        }
    );

    connect(
        sessionManager,
        &ConnectionSessionManager::systemProxyOwnerChanged,
        this,
        [this](const QString &)
        {
            if (systemProxySession->isEnabled()
                && sessionManager->systemProxyOwnerId().isEmpty())
            {
                systemProxySession->disable();
            }
        }
    );
}

ProfileService *MainWindowCoordinator::profiles() const
{
    return profileService;
}

ConnectionSessionManager *MainWindowCoordinator::sessions() const
{
    return sessionManager;
}

SystemProxySession *MainWindowCoordinator::systemProxy() const
{
    return systemProxySession;
}

UpdateChecker *MainWindowCoordinator::updates() const
{
    return updateChecker;
}

AuthDialogCoordinator *
MainWindowCoordinator::authenticationDialogs() const
{
    return authenticationCoordinator;
}

void MainWindowCoordinator::prepareForShutdown()
{
    systemProxySession->clearBeforeShutdown();
    sessionManager->stopAll();
}

void MainWindowCoordinator::wireSession(
    const QString &profileId,
    ConnectionSession *session
)
{
    connect(session, &ConnectionSession::askSudoPass, this,
        [this, profileId]()
        {
            activeAuthProfileId = profileId;
            authenticationCoordinator->requestSudoPassword();
        });
    connect(session, &ConnectionSession::graphCaptcha, this,
        [this, profileId](const QString &graphFile)
        {
            activeAuthProfileId = profileId;
            authenticationCoordinator->requestGraphCaptcha(graphFile);
        });
    connect(session, &ConnectionSession::smsCode, this,
        [this, profileId](bool showSkip)
        {
            activeAuthProfileId = profileId;
            authenticationCoordinator->requestSmsCode(showSkip);
        });
    connect(session, &ConnectionSession::totpCode, this,
        [this, profileId]()
        {
            activeAuthProfileId = profileId;
            authenticationCoordinator->requestTotpCode();
        });
    connect(session, &ConnectionSession::randCode, this,
        [this, profileId]()
        {
            activeAuthProfileId = profileId;
            authenticationCoordinator->requestSmsCode(false);
        });
    connect(session, &ConnectionSession::radiusCode, this,
        [this, profileId](bool showSkip)
        {
            activeAuthProfileId = profileId;
            authenticationCoordinator->requestRadiusCode(showSkip);
        });
    connect(session, &ConnectionSession::ssoAuth, this,
        [this, profileId]()
        {
            activeAuthProfileId = profileId;
            authenticationCoordinator->requestSsoLogin();
        });
}
