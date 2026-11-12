#include "connectionsession.h"

ConnectionSession::ConnectionSession(CoreProcess *coreProcess, QObject *parent)
    : QObject(parent),
      coreProcess(coreProcess)
{
    coreProcess->setParent(this);
    reconnectTimer.setSingleShot(true);

    connect(coreProcess, &CoreProcess::outputRead, this, &ConnectionSession::outputRead);
    connect(coreProcess, &CoreProcess::graphCaptcha, this, &ConnectionSession::graphCaptcha);
    connect(coreProcess, &CoreProcess::smsCode, this, &ConnectionSession::smsCode);
    connect(coreProcess, &CoreProcess::totpCode, this, &ConnectionSession::totpCode);
    connect(coreProcess, &CoreProcess::randCode, this, &ConnectionSession::randCode);
    connect(coreProcess, &CoreProcess::radiusCode, this, &ConnectionSession::radiusCode);
    connect(coreProcess, &CoreProcess::ssoAuth, this, &ConnectionSession::ssoAuth);
    connect(coreProcess, &CoreProcess::askSudoPass,
            this, &ConnectionSession::handleSudoPasswordRequest);

    connect(coreProcess, &CoreProcess::error, this, [this](ZJU_ERROR error)
    {
        sessionState.recordError(error);
    });
    connect(coreProcess, &CoreProcess::started, this, [this]()
    {
        emit stateChanged(sessionState.state());
    });
    connect(coreProcess, &CoreProcess::connectionEstablished, this, [this]()
    {
        sessionState.connectionEstablished();
        emit stateChanged(sessionState.state());
    });
    connect(coreProcess, &CoreProcess::finished,
            this, &ConnectionSession::handleCoreFinished);
    connect(&reconnectTimer, &QTimer::timeout, this, [this]()
    {
        if (sessionState.state() != ConnectionState::Reconnecting)
        {
            return;
        }
        sessionState.beginReconnect();
        emit stateChanged(sessionState.state());
        startCore();
    });
}

bool ConnectionSession::start(const ConnectionProfile &profile, const ReconnectPolicy &policy)
{
    if (!sessionState.requestStart(policy))
    {
        return false;
    }

    currentProfile = profile;
    emit stateChanged(sessionState.state());
    startCore();
    return true;
}

void ConnectionSession::setAllocatedPorts(int socksPort, int httpPort)
{
    allocatedSocksPort = socksPort;
    allocatedHttpPort = httpPort;
    hasAllocatedPorts = socksPort > 0 && httpPort > 0;
}

int ConnectionSession::socksPort() const
{
    if (hasAllocatedPorts)
    {
        return allocatedSocksPort;
    }
    // Fall back to the port parsed from the running profile's bind address.
    const QString bind = currentProfile.proxy.socksBind;
    const int colon = bind.lastIndexOf(':');
    return colon >= 0 ? bind.mid(colon + 1).toInt() : 0;
}

int ConnectionSession::httpPort() const
{
    if (hasAllocatedPorts)
    {
        return allocatedHttpPort;
    }
    const QString bind = currentProfile.proxy.httpBind;
    const int colon = bind.lastIndexOf(':');
    return colon >= 0 ? bind.mid(colon + 1).toInt() : 0;
}

void ConnectionSession::stop()
{
    const ConnectionState previousState = sessionState.state();
    const bool processNeedsStop = sessionState.requestStop();
    if (previousState == ConnectionState::Reconnecting)
    {
        reconnectTimer.stop();
        emit stateChanged(sessionState.state());
        emit finished(sessionState.error());
        return;
    }

    if (processNeedsStop)
    {
        emit stateChanged(sessionState.state());
        coreProcess->stop();
    }
}

void ConnectionSession::submitInput(const QByteArray &data)
{
    coreProcess->writeInput(data);
}

void ConnectionSession::cancelInteractiveInput()
{
    submitInput("\r\n");
    stop();
}

void ConnectionSession::submitSudoPassword(const QString &password, bool remember)
{
    if (password.isEmpty())
    {
        stop();
        return;
    }

    if (remember)
    {
        savedSudoPassword = password;
    }
    sudoPasswordSubmitted = true;
    submitInput(password.toUtf8() + "\n");
}

ConnectionState ConnectionSession::state() const
{
    return sessionState.state();
}

bool ConnectionSession::isActive() const
{
    return sessionState.isActive();
}

void ConnectionSession::startCore()
{
    sudoPasswordSubmitted = false;

    if (hasAllocatedPorts)
    {
        // Rewrite the proxy bind addresses to the uniquely allocated ports.
        // The original bind prefix (127.0.0.1 / [::]) is preserved.
        const QString socksBind = currentProfile.proxy.socksBind;
        const QString httpBind = currentProfile.proxy.httpBind;

        auto rewritePort = [](const QString &bind, int port) -> QString
        {
            const int colon = bind.lastIndexOf(':');
            if (colon < 0)
            {
                return bind + QStringLiteral(":") + QString::number(port);
            }
            return bind.left(colon + 1) + QString::number(port);
        };

        currentProfile.proxy.socksBind = rewritePort(socksBind, allocatedSocksPort);
        currentProfile.proxy.httpBind = rewritePort(httpBind, allocatedHttpPort);
    }

    coreProcess->start(currentProfile);
}

void ConnectionSession::handleCoreFinished()
{
    if (sessionState.processFinished() == ProcessFinishAction::Reconnect)
    {
        emit stateChanged(sessionState.state());
        emit reconnectScheduled(sessionState.reconnectDelayMs());
        reconnectTimer.start(sessionState.reconnectDelayMs());
        return;
    }

    emit stateChanged(sessionState.state());
    emit finished(sessionState.error());
}

void ConnectionSession::handleSudoPasswordRequest()
{
    if (savedSudoPassword.isEmpty())
    {
        emit askSudoPass();
        return;
    }

    if (sudoPasswordSubmitted)
    {
        savedSudoPassword.clear();
        emit savedSudoPasswordRejected();
        emit askSudoPass();
        return;
    }

    sudoPasswordSubmitted = true;
    submitInput(savedSudoPassword.toUtf8() + "\n");
}
