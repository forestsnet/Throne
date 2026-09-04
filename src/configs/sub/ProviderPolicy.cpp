#include "include/configs/sub/ProviderPolicy.hpp"

#include <QJsonDocument>
#include <QSet>

namespace Subscription {
    namespace {
        // Значение может прийти как "base64:<...>"; при неудаче декодирования
        // возвращаем исходную строку — подписку из-за этого не отвергаем.
        QString decodeValue(const QByteArray &raw) {
            const QString text = QString::fromUtf8(raw).trimmed();
            if (!text.startsWith("base64:", Qt::CaseInsensitive)) return text;

            const QByteArray payload = text.mid(7).toUtf8();
            // Строгий режим обязателен: по умолчанию декодер молча пропускает
            // недопустимые символы и возвращает мусор со статусом Ok.
            const auto decoded = QByteArray::fromBase64Encoding(
                payload, QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
            if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok) {
                return text.mid(7);
            }
            return QString::fromUtf8(decoded.decoded);
        }

        std::optional<bool> toBool(const QString &value) {
            const QString v = value.trimmed().toLower();
            if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
            if (v == "false" || v == "0" || v == "no" || v == "off") return false;
            return std::nullopt;
        }
    }

    bool ProviderPolicy::isEmpty() const {
        return title.isEmpty() && announce.isEmpty() && supportUrl.isEmpty()
            && webPageUrl.isEmpty() && providerId.isEmpty()
            && refillDate == 0 && updateIntervalHours == 0
            && !tunEnable && !alwaysHwid && !autoUpdate && !dnsFromJson
            && !hideSettings && !hideUrl && !pin && !collapse && !pingOnOpen
            && perAppProxyList.isEmpty() && unknown.isEmpty();
    }

    ProviderPolicy ParseProviderPolicy(const QList<QPair<QByteArray, QByteArray>> &headers) {
        // Заголовки, которые шлёт любой HTTP-сервер: в unknown им не место.
        static const QSet<QString> ignored = {
            "server", "date", "content-type", "content-length", "content-encoding",
            "set-cookie", "expires", "cache-control", "pragma", "etag", "vary",
            "connection", "transfer-encoding", "accept-ranges", "age", "location",
            "cross-origin-opener-policy", "cross-origin-resource-policy",
            "referrer-policy", "access-control-allow-origin", "content-disposition",
            "strict-transport-security", "x-content-type-options", "x-frame-options",
            "subscription-userinfo", // читается отдельно, в group->info
        };

        ProviderPolicy p;
        for (const auto &[nameRaw, valueRaw] : headers) {
            const QString name = QString::fromUtf8(nameRaw).trimmed().toLower();
            const QString value = decodeValue(valueRaw);

            if (name == "profile-title")                        { p.title = value; continue; }
            if (name == "announce")                             { p.announce = value; continue; }
            if (name == "support-url")                          { p.supportUrl = value; continue; }
            if (name == "profile-web-page-url")                 { p.webPageUrl = value; continue; }
            if (name == "providerid")                           { p.providerId = value; continue; }
            if (name == "per-app-proxy-list")                   { p.perAppProxyList = value; continue; }

            if (name == "profile-update-interval") {
                bool ok = false;
                const int hours = value.toInt(&ok);
                if (ok && hours > 0) p.updateIntervalHours = hours;
                continue;
            }
            if (name == "subscription-refill-date") {
                bool ok = false;
                const qint64 ts = value.toLongLong(&ok);
                if (ok && ts > 0) p.refillDate = ts;
                continue;
            }

            if (name == "tun-enable")                          { p.tunEnable = toBool(value); continue; }
            if (name == "subscription-always-hwid-enable")     { p.alwaysHwid = toBool(value); continue; }
            if (name == "subscription-auto-update-enable")     { p.autoUpdate = toBool(value); continue; }
            if (name == "dns-from-json-enable")                { p.dnsFromJson = toBool(value); continue; }
            if (name == "hide-settings")                       { p.hideSettings = toBool(value); continue; }
            if (name == "hide-url")                            { p.hideUrl = toBool(value); continue; }
            if (name == "subscription-pin")                    { p.pin = toBool(value); continue; }
            if (name == "subscriptions-collapse")              { p.collapse = toBool(value); continue; }
            if (name == "subscription-ping-onopen-enabled")    { p.pingOnOpen = toBool(value); continue; }

            if (ignored.contains(name)) continue;
            p.unknown[name] = value;
        }
        return p;
    }
}
