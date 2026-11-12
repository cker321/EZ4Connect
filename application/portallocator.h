#ifndef PORTALLOCATOR_H
#define PORTALLOCATOR_H

#include <QHash>
#include <QString>

// PortAllocator assigns each VPN profile a unique pair of local proxy ports
// (SOCKS5 + HTTP) so that multiple zju-connect instances can run side by side
// without binding to the same port and failing with LISTEN_FAILED.
//
// The primary (default/first) connection uses the user-configured base ports;
// subsequent connections probe upward from the base ports until free ports are
// found.
class PortAllocator
{
public:
    PortAllocator() = default;

    // Assign (and cache) a unique port pair for the given profileId.
    // baseSocks / baseHttp are the user-configured base ports.
    // Returns the pair {socksPort, httpPort}.
    struct PortPair
    {
        int socksPort;
        int httpPort;
    };

    PortPair allocate(const QString &profileId, int baseSocks, int baseHttp);

    // Look up an already-allocated pair without probing. Returns {-1, -1} when
    // the profile has not been allocated yet.
    PortPair lookup(const QString &profileId) const;

    // Forget the allocation for a profile so its ports can be reused.
    void release(const QString &profileId);

    void clear();

private:
    static bool isPortFree(int port);

    QHash<QString, PortPair> allocations_;
};

#endif // PORTALLOCATOR_H
