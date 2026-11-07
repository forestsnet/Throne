#include "include/global/DirectDnsProbe.hpp"

#include "include/api/RPC.h"
#include "include/configs/DnsAddress.hpp"

namespace Configs {
    DirectDnsProbeRound ProbeDirectDns(const QStringList &addresses, int timeoutMs, const QString &probeName) {
        DirectDnsProbeRound round;
        if (!API::defaultClient || !API::defaultClient->IsConnected()) {
            round.error = QStringLiteral("core is not connected");
            return round;
        }
        libcore::DNSProbeRequest request;
        request.probe_name = probeName.trimmed().isEmpty() ? "example.com" : probeName.trimmed().toStdString();
        request.timeout_ms = timeoutMs;
        for (const auto &address : addresses) {
            const auto parsed = Fsnt::ParseDnsAddress(address);
            if (!parsed.valid) {
                round.results.append({address, false, -1, parsed.error, {}});
                continue;
            }
            libcore::DNSProbeTarget target;
            target.address = address.toStdString();
            target.type = parsed.type.toStdString();
            target.host = (parsed.isDhcp() ? parsed.interfaceName : parsed.server).toStdString();
            target.port = parsed.port < 0 ? Fsnt::DefaultDnsPort(parsed.type) : parsed.port;
            target.path = parsed.path.toStdString();
            request.targets.push_back(std::move(target));
        }
        if (request.targets.empty()) return round;
        bool rpcOK = false;
        QString error;
        const auto response = API::defaultClient->ProbeDNS(&rpcOK, request, &error);
        if (!rpcOK) {
            round.error = error.isEmpty() ? QStringLiteral("direct DNS RPC failed") : error;
            return round;
        }
        round.error = QString::fromStdString(response.error.value_or(""));
        for (const auto &result : response.results) {
            DnsProbeResult item;
            item.address = QString::fromStdString(result.address.value_or(""));
            item.ok = result.ok.value_or(false);
            item.responded = result.responded.value_or(false);
            item.latencyMs = result.latency_ms.value_or(-1);
            item.error = QString::fromStdString(result.error.value_or(""));
            for (const auto &address : result.addresses) item.addresses.append(QString::fromStdString(address));
            round.results.append(std::move(item));
        }
        return round;
    }
}
