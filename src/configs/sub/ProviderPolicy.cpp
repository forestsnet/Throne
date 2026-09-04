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

        void putOpt(QJsonObject &o, const char *key, const std::optional<bool> &v) {
            if (v.has_value()) o[key] = v.value();   // отсутствие = ключа нет
        }

        std::optional<bool> getOpt(const QJsonObject &o, const char *key) {
            if (!o.contains(key)) return std::nullopt;
            return o.value(key).toBool();
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

    QString SerializeProviderPolicy(const ProviderPolicy &policy) {
        QJsonObject o;
        o["schema"] = policy.schema;
        if (!policy.title.isEmpty())           o["title"] = policy.title;
        if (!policy.announce.isEmpty())        o["announce"] = policy.announce;
        if (!policy.supportUrl.isEmpty())      o["supportUrl"] = policy.supportUrl;
        if (!policy.webPageUrl.isEmpty())      o["webPageUrl"] = policy.webPageUrl;
        if (!policy.providerId.isEmpty())      o["providerId"] = policy.providerId;
        if (policy.refillDate != 0)            o["refillDate"] = policy.refillDate;
        if (policy.updateIntervalHours != 0)   o["updateIntervalHours"] = policy.updateIntervalHours;
        if (!policy.perAppProxyList.isEmpty()) o["perAppProxyList"] = policy.perAppProxyList;

        putOpt(o, "tunEnable", policy.tunEnable);
        putOpt(o, "alwaysHwid", policy.alwaysHwid);
        putOpt(o, "autoUpdate", policy.autoUpdate);
        putOpt(o, "dnsFromJson", policy.dnsFromJson);
        putOpt(o, "hideSettings", policy.hideSettings);
        putOpt(o, "hideUrl", policy.hideUrl);
        putOpt(o, "pin", policy.pin);
        putOpt(o, "collapse", policy.collapse);
        putOpt(o, "pingOnOpen", policy.pingOnOpen);

        if (!policy.unknown.isEmpty()) o["unknown"] = policy.unknown;

        return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }

    ProviderPolicy DeserializeProviderPolicy(const QString &json) {
        ProviderPolicy p;
        if (json.trimmed().isEmpty()) return p;

        const auto doc = QJsonDocument::fromJson(json.toUtf8());
        if (!doc.isObject()) return p;   // битая строка -> пустая политика, ограничений нет
        const auto o = doc.object();

        p.schema              = o.value("schema").toInt(1);
        p.title               = o.value("title").toString();
        p.announce            = o.value("announce").toString();
        p.supportUrl          = o.value("supportUrl").toString();
        p.webPageUrl          = o.value("webPageUrl").toString();
        p.providerId          = o.value("providerId").toString();
        p.refillDate          = o.value("refillDate").toVariant().toLongLong();
        p.updateIntervalHours = o.value("updateIntervalHours").toInt(0);
        p.perAppProxyList     = o.value("perAppProxyList").toString();

        p.tunEnable    = getOpt(o, "tunEnable");
        p.alwaysHwid   = getOpt(o, "alwaysHwid");
        p.autoUpdate   = getOpt(o, "autoUpdate");
        p.dnsFromJson  = getOpt(o, "dnsFromJson");
        p.hideSettings = getOpt(o, "hideSettings");
        p.hideUrl      = getOpt(o, "hideUrl");
        p.pin          = getOpt(o, "pin");
        p.collapse     = getOpt(o, "collapse");
        p.pingOnOpen   = getOpt(o, "pingOnOpen");

        p.unknown = o.value("unknown").toObject();
        return p;
    }

    namespace {
        ProviderPolicy g_active;
        int g_activeGid = -1;

        // Ограничение действует, только если панель прислала его как true.
        bool on(const std::optional<bool> &flag) { return flag.has_value() && flag.value(); }
    }

    const ProviderPolicy &ActiveProviderPolicy() { return g_active; }
    int ActiveProviderPolicyGroup() { return g_activeGid; }

    void SetActiveProviderPolicy(const ProviderPolicy &policy, int gid) {
        g_active = policy;
        g_activeGid = gid;
    }

    void ClearActiveProviderPolicy() {
        g_active = ProviderPolicy{};
        g_activeGid = -1;
    }

    bool PolicyHidesSettings()  { return on(g_active.hideSettings); }
    bool PolicyHidesUrl()       { return on(g_active.hideUrl); }

    // Закрепление относится только к своей группе: чужие подписки удалять можно.
    bool PolicyBlocksDeletion(int gid) {
        return gid >= 0 && gid == g_activeGid && on(g_active.pin);
    }
}
