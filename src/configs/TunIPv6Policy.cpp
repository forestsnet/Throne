#include "include/configs/TunIPv6Policy.hpp"

#include <QJsonArray>

namespace Configs {
    void ApplyTunIPv6Policy(QJsonObject &config, const QString &tunTag, const QString &systemDnsTag) {
        auto route = config["route"].toObject();
        auto rules = route["rules"].toArray();
        // Reject before sniff accepts TCP. A successful handshake followed by a
        // close can prevent browsers from trying the working IPv4 address.
        rules.prepend(QJsonObject{
            {"inbound", tunTag}, {"ip_version", 6},
            {"action", "reject"}, {"no_drop", true},
        });
        // The tunnel's IPv6 DNS address must still answer, including TCP DNS.
        rules.prepend(QJsonObject{
            {"inbound", tunTag}, {"ip_version", 6}, {"port", 53},
            {"action", "hijack-dns"},
        });
        route["rules"] = rules;
        config["route"] = route;

        auto dns = config["dns"].toObject();
        auto dnsRules = dns["rules"].toArray();
        // Do not advertise unusable addresses to tunnel clients. Internal
        // endpoint lookups and the mixed proxy keep their own DNS policy.
        dnsRules.prepend(QJsonObject{
            {"inbound", QJsonArray{tunTag, systemDnsTag}}, {"query_type", "AAAA"},
            {"action", "predefined"}, {"rcode", "NOERROR"},
        });
        dns["rules"] = dnsRules;
        config["dns"] = dns;
    }
}
