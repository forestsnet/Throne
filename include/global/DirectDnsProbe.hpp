#pragma once

#include "include/global/DnsProbe.hpp"

namespace Configs {
    struct DirectDnsProbeRound {
        QList<DnsProbeResult> results;
        QString error;
    };

    // Blocking RPC. Call on a worker thread. Every socket uses the core's physical egress.
    // extraNames — остальные имена того же конфига. Ядро разрешает их тем же
    // транспортом, уже поднятым, и требует ровно одну цель: см. dnsprobe.Run.
    DirectDnsProbeRound ProbeDirectDns(const QStringList &addresses, int timeoutMs, const QString &probeName,
                                       const QStringList &extraNames = {});
}
