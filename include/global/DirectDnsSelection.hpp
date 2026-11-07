#pragma once

#include "include/global/DnsProbe.hpp"

namespace Configs {
    enum class DirectDnsFailure { None, NoResponse, NoUsableResolver, NameNotResolved, ProbeUnavailable };

    struct DirectDnsSelection {
        QString address;
        QStringList serverAddresses;
        DirectDnsFailure failure = DirectDnsFailure::NoResponse;
    };

    DirectDnsSelection SelectDirectDns(const QList<DnsProbeResult> &results, const QString &configured,
                                      bool requireTunnelSafe, bool requireAddresses);
    QString DirectDnsFailureText(DirectDnsFailure failure);
}
