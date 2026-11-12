#include "portallocator.h"

#include <QTcpServer>

namespace
{
constexpr int kMaxProbeOffset = 1000;
}

bool PortAllocator::isPortFree(int port)
{
    QTcpServer probe;
    // Bind to loopback only; the core listens on 127.0.0.1 by default.
    return probe.listen(QHostAddress::LocalHost, static_cast<quint16>(port));
}

PortAllocator::PortPair PortAllocator::allocate(
    const QString &profileId,
    int baseSocks,
    int baseHttp
)
{
    if (allocations_.contains(profileId))
    {
        return allocations_.value(profileId);
    }

    // Determine a starting offset. The primary profile (first allocation) gets
    // the base ports verbatim; every additional profile starts offset by 2 so
    // that SOCKS and HTTP remain adjacent but distinct across connections.
    const int offsetStep = allocations_.isEmpty() ? 0 : 2;

    int socksPort = baseSocks;
    int httpPort = baseHttp;
    bool found = false;

    for (int offset = offsetStep; offset < kMaxProbeOffset; offset += 2)
    {
        const int candidateSocks = baseSocks + offset;
        const int candidateHttp = baseHttp + offset;

        // Skip candidates already handed out to other profiles.
        bool clash = false;
        for (const PortPair &existing : allocations_)
        {
            if (existing.socksPort == candidateSocks
                || existing.httpPort == candidateHttp
                || existing.socksPort == candidateHttp
                || existing.httpPort == candidateSocks)
            {
                clash = true;
                break;
            }
        }
        if (clash)
        {
            continue;
        }

        if (isPortFree(candidateSocks) && isPortFree(candidateHttp))
        {
            socksPort = candidateSocks;
            httpPort = candidateHttp;
            found = true;
            break;
        }
    }

    if (!found)
    {
        // Fall back to requesting the OS to pick arbitrary free ports.
        QTcpServer socksProbe;
        socksProbe.listen(QHostAddress::LocalHost, 0);
        socksPort = socksProbe.serverPort();
        QTcpServer httpProbe;
        httpProbe.listen(QHostAddress::LocalHost, 0);
        httpPort = httpProbe.serverPort();
    }

    PortPair pair{socksPort, httpPort};
    allocations_.insert(profileId, pair);
    return pair;
}

PortAllocator::PortPair PortAllocator::lookup(const QString &profileId) const
{
    return allocations_.value(profileId, PortPair{-1, -1});
}

void PortAllocator::release(const QString &profileId)
{
    allocations_.remove(profileId);
}

void PortAllocator::clear()
{
    allocations_.clear();
}
