#include "include/global/DirectDnsSelection.hpp"
#include "include/configs/DnsAddress.hpp"

namespace Configs {
    DirectDnsSelection SelectDirectDns(const QList<DnsProbeResult> &results, const QString &configured,
                                      bool requireTunnelSafe, bool requireAddresses) {
        DirectDnsSelection selection;
        const DnsProbeResult *chosen = nullptr;
        const DnsProbeResult *resolved = nullptr;
        bool answered = false;
        bool eligibleAnswered = false;
        for (const auto &result : results) {
            const bool eligible = !requireTunnelSafe || Fsnt::DnsUsableUnderTunnel(result.address);
            answered |= result.responded || result.ok;
            eligibleAnswered |= eligible && (result.responded || result.ok);
            if (result.ok && !result.addresses.isEmpty() && (!resolved || result.latencyMs < resolved->latencyMs))
                resolved = &result;
            if (!result.ok || !eligible || (requireAddresses && result.addresses.isEmpty())) continue;
            if (!chosen || result.address == configured ||
                (chosen->address != configured && result.latencyMs < chosen->latencyMs)) chosen = &result;
        }
        if (chosen) {
            selection.address = chosen->address;
            selection.serverAddresses = chosen->addresses;
            selection.failure = DirectDnsFailure::None;
        } else {
            if (resolved) selection.serverAddresses = resolved->addresses;
            if (eligibleAnswered) selection.failure = DirectDnsFailure::NameNotResolved;
            else if (answered) selection.failure = DirectDnsFailure::NoUsableResolver;
        }
        return selection;
    }

    QString DirectDnsFailureText(DirectDnsFailure failure) {
        switch (failure) {
            case DirectDnsFailure::None: return {};
            case DirectDnsFailure::NoResponse:
                return QObject::tr("Could not get a direct DNS response. Check the connection or choose another DNS in the settings.");
            case DirectDnsFailure::NoUsableResolver:
                return QObject::tr("DNS servers answered, but none is suitable for the current tunnel settings. Choose an available encrypted DNS with an IP address.");
            case DirectDnsFailure::NameNotResolved:
                return QObject::tr("DNS servers answered, but did not return an address for the requested name. Try another DNS.");
            case DirectDnsFailure::ProbeUnavailable:
                return QObject::tr("Could not check direct DNS through the core. Check that the core is running and the network connection is available.");
        }
        return {};
    }
}
