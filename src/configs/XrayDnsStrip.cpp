#include "include/configs/XrayDnsStrip.hpp"

#include <QJsonArray>
#include <QSet>
#include <QString>

namespace Configs {
    XrayDnsStripResult StripXrayDns(QJsonObject &config) {
        XrayDnsStripResult result;
        config.remove("dns");

        // Собираем теги исходящих с протоколом dns и выкидываем их самих.
        QSet<QString> dnsTags;
        QJsonArray keptOutbounds;
        for (const auto &value : config["outbounds"].toArray()) {
            const auto outbound = value.toObject();
            if (outbound["protocol"].toString() == QStringLiteral("dns")) {
                dnsTags.insert(outbound["tag"].toString());
                continue;
            }
            keptOutbounds.append(outbound);
        }
        if (!dnsTags.isEmpty()) {
            config["outbounds"] = keptOutbounds;
            result.outbounds = static_cast<int>(dnsTags.size());
        }

        // И правила, которые в них целились: без исходящего такое правило
        // отправляет трафик в никуда.
        auto routing = config["routing"].toObject();
        QJsonArray keptRules;
        for (const auto &value : routing["rules"].toArray()) {
            const auto rule = value.toObject();
            if (dnsTags.contains(rule["outboundTag"].toString())) {
                ++result.rules;
                continue;
            }
            keptRules.append(rule);
        }
        if (result.rules > 0) {
            routing["rules"] = keptRules;
            config["routing"] = routing;
        }

        return result;
    }
}
