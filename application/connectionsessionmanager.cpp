#include "connectionsessionmanager.h"

#include "application/connectionsession.h"
#include "infrastructure/coreprocess/zjuconnectprocess.h"

ConnectionSessionManager::ConnectionSessionManager(QObject *parent)
    : QObject(parent)
{
}

ConnectionSessionManager::~ConnectionSessionManager()
{
    qDeleteAll(sessions_);
    sessions_.clear();
}

ConnectionSession *ConnectionSessionManager::sessionFor(
    const QString &profileId,
    int baseSocks,
    int baseHttp
)
{
    if (sessions_.contains(profileId))
    {
        return sessions_.value(profileId);
    }

    const PortAllocator::PortPair ports =
        portAllocator_.allocate(profileId, baseSocks, baseHttp);

    auto *session = new ConnectionSession(new ZjuConnectProcess(), this);
    session->setProperty("profileId", profileId);
    session->setAllocatedPorts(ports.socksPort, ports.httpPort);

    connect(session, &ConnectionSession::stateChanged, this,
        [this, profileId](ConnectionState state)
        {
            const bool active = state == ConnectionState::Starting
                || state == ConnectionState::Running
                || state == ConnectionState::Stopping
                || state == ConnectionState::Reconnecting;
            emit activeChanged(profileId, active);
        });

    connect(session, &ConnectionSession::finished, this,
        [this, profileId](ZJU_ERROR)
        {
            if (systemProxyOwnerId_ == profileId)
            {
                releaseSystemProxy();
            }
        });

    sessions_.insert(profileId, session);
    emit sessionCreated(profileId);
    return session;
}

ConnectionSession *ConnectionSessionManager::find(const QString &profileId) const
{
    return sessions_.value(profileId, nullptr);
}

QStringList ConnectionSessionManager::activeProfileIds() const
{
    QStringList result;
    for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it)
    {
        if (it.value()->isActive())
        {
            result.append(it.key());
        }
    }
    return result;
}

bool ConnectionSessionManager::anyActive() const
{
    for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it)
    {
        if (it.value()->isActive())
        {
            return true;
        }
    }
    return false;
}

QString ConnectionSessionManager::systemProxyOwnerId() const
{
    return systemProxyOwnerId_;
}

bool ConnectionSessionManager::claimSystemProxy(const QString &profileId)
{
    if (profileId == systemProxyOwnerId_)
    {
        return true;
    }

    if (!sessions_.contains(profileId))
    {
        return false;
    }

    const QString previous = systemProxyOwnerId_;
    systemProxyOwnerId_ = profileId;
    emit systemProxyOwnerChanged(systemProxyOwnerId_);

    if (previous != profileId)
    {
        emit systemProxyOwnerChanged(previous);
    }
    return true;
}

void ConnectionSessionManager::releaseSystemProxy()
{
    if (systemProxyOwnerId_.isEmpty())
    {
        return;
    }
    const QString previous = systemProxyOwnerId_;
    systemProxyOwnerId_.clear();
    emit systemProxyOwnerChanged(previous);
}

PortAllocator::PortPair ConnectionSessionManager::portsFor(const QString &profileId) const
{
    return portAllocator_.lookup(profileId);
}

void ConnectionSessionManager::stopAll()
{
    for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it)
    {
        if (it.value()->isActive())
        {
            it.value()->stop();
        }
    }
}
