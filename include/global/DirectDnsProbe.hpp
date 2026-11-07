#pragma once

#include "include/global/DnsProbe.hpp"

namespace Configs {
    struct DirectDnsProbeRound {
        QList<DnsProbeResult> results;
        QString error;
    };

    // Blocking RPC. Call on a worker thread. Every socket uses the core's physical egress.
    DirectDnsProbeRound ProbeDirectDns(const QStringList &addresses, int timeoutMs, const QString &probeName);
}
