#include "include/global/ExitLookupPlan.hpp"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace Configs_network {
    namespace {
        // Первым ip2location: тот же источник спрашивает проверка «IP и
        // страна», и две подписи должны называть одну страну. Вторым — адрес
        // Cloudflare: имени у него нет, разрешать нечего, и он отвечает, когда
        // резолвер ядра молчит под туннелем.
        const QStringList &autoOrder() {
            static const QStringList order = {
                QStringLiteral("ip2location"), QStringLiteral("cloudflare-ip"), QStringLiteral("ip-api"),
                QStringLiteral("ipwho"),       QStringLiteral("ipinfo"),        QStringLiteral("cloudflare"),
                QStringLiteral("cloudflare-ip2"),
            };
            return order;
        }
    } // namespace

    QStringList ExitLookupChain(const QString &providerId, const QStringList &known) {
        QStringList chain;
        const QString chosen = providerId.trimmed();
        if (!chosen.isEmpty() && known.contains(chosen)) {
            chain << chosen;
            return chain;
        }
        for (const QString &id : autoOrder()) {
            if (known.contains(id)) chain << id;
        }
        return chain;
    }

    QStringList ParseDohAddresses(const QByteArray &json) {
        const auto doc = QJsonDocument::fromJson(json);
        if (!doc.isObject()) return {};
        QStringList addresses;
        const auto answers = doc.object().value(QLatin1String("Answer")).toArray();
        for (const auto &value : answers) {
            const auto record = value.toObject();
            // Тип 1 — запись A. CNAME и прочее по дороге к ней нам не адрес.
            if (record.value(QLatin1String("type")).toInt() != 1) continue;
            const QString data = record.value(QLatin1String("data")).toString().trimmed();
            if (data.isEmpty() || addresses.contains(data)) continue;
            const QHostAddress parsed(data);
            if (parsed.protocol() != QAbstractSocket::IPv4Protocol) continue;
            addresses << data;
        }
        return addresses;
    }

    QString UrlWithAddress(const QString &url, const QString &address) {
        QUrl parsed(url.trimmed(), QUrl::StrictMode);
        if (!parsed.isValid() || parsed.host().isEmpty()) return {};
        const QHostAddress host(address.trimmed());
        if (host.isNull()) return {};
        // Скобки вокруг IPv6 ставит сам QUrl, а toString отдаёт их обратно.
        parsed.setHost(host.toString());
        if (!parsed.isValid() || parsed.host().isEmpty()) return {};
        return parsed.toString(QUrl::FullyEncoded);
    }
} // namespace Configs_network
