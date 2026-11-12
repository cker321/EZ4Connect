#ifndef CONNECTIONSESSIONMANAGER_H
#define CONNECTIONSESSIONMANAGER_H

#include <QHash>
#include <QObject>
#include <QString>

#include "application/portallocator.h"

class ConnectionSession;

// ConnectionSessionManager owns one ConnectionSession per VPN profile, allowing
// multiple VPN connections to run simultaneously. It also enforces mutual
// exclusion for the (global) system proxy: at most one session may be the
// "system proxy owner" at any time.
class ConnectionSessionManager : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionSessionManager(QObject *parent = nullptr);
    ~ConnectionSessionManager() override;

    // Return the session for profileId, creating it lazily if necessary. The
    // session is allocated a unique SOCKS/HTTP port pair derived from the base
    // ports. An empty profileId denotes the default profile.
    ConnectionSession *sessionFor(const QString &profileId, int baseSocks, int baseHttp);

    // Look up an existing session without creating one (nullptr if absent).
    ConnectionSession *find(const QString &profileId) const;

    // Profile ids of sessions that are currently active (connected).
    QStringList activeProfileIds() const;

    // Whether any session is currently active.
    bool anyActive() const;

    // System-proxy mutual exclusion.
    QString systemProxyOwnerId() const;
    bool claimSystemProxy(const QString &profileId);
    void releaseSystemProxy();
    PortAllocator::PortPair portsFor(const QString &profileId) const;

    // Stop every active session (used on shutdown).
    void stopAll();

signals:
    // Emitted when a session is created for the first time.
    void sessionCreated(const QString &profileId);
    // Emitted when the set of active sessions changes.
    void activeChanged(const QString &profileId, bool active);
    // Emitted whenever the system-proxy owner changes.
    void systemProxyOwnerChanged(const QString &profileId);

private:
    QHash<QString, ConnectionSession *> sessions_;
    PortAllocator portAllocator_;
    QString systemProxyOwnerId_;
};

#endif // CONNECTIONSESSIONMANAGER_H
