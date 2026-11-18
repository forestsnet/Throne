#include "include/configs/CustomOutbounds.hpp"

namespace Configs
{
    namespace
    {
        QStringList gatherServerNames(const QJsonArray& outbounds, QString (*addressOf)(const QJsonObject&))
        {
            QStringList names;
            for (const auto& value : outbounds) {
                const auto name = addressOf(value.toObject()).trimmed();
                if (name.isEmpty() || names.contains(name, Qt::CaseInsensitive)) continue;
                names.append(name);
            }
            return names;
        }
    } // namespace

    std::optional<QString> xrayAgreedName(const QStringList& lowerCased)
    {
        if (lowerCased.isEmpty()) return QString();
        if (lowerCased.count(lowerCased.first()) != lowerCased.size()) return std::nullopt;
        return lowerCased.first();
    }

    QStringList keysNamed(const QJsonObject& o, const QString& name)
    {
        QStringList keys;
        for (const auto& key : o.keys()) {
            if (key.compare(name, Qt::CaseInsensitive) == 0) keys << key;
        }
        return keys;
    }

    bool isXrayInfra(const QString& protocol)
    {
        // direct and block are Xray's aliases of freedom and blackhole.
        return protocol == "freedom" || protocol == "direct" || protocol == "blackhole" || protocol == "block"
               || protocol == "dns" || protocol == "loopback";
    }

    bool isSingBoxInfra(const QString& type)
    {
        return type == "direct" || type == "block" || type == "dns";
    }

    QJsonValue xrayAgreed(const QJsonObject& o, const QString& name)
    {
        const auto keys = keysNamed(o, name);
        for (const auto& key : keys) {
            if (o.value(key) != o.value(keys.first())) return QJsonValue(QJsonValue::Undefined);
        }
        return keys.isEmpty() ? QJsonValue(QJsonValue::Undefined) : o.value(keys.first());
    }

    std::optional<QString> xrayAgreedProtocol(const QJsonObject& o)
    {
        QStringList protocols;
        for (const auto& key : keysNamed(o, "protocol")) protocols << o.value(key).toString().toLower();
        return xrayAgreedName(protocols);
    }

    std::optional<bool> xrayInlinePeer(const QJsonObject& settings)
    {
        bool set = false;
        bool unset = false;
        for (const auto& key : keysNamed(settings, "address")) (settings.value(key).isNull() ? unset : set) = true;
        if (set && unset) return std::nullopt;
        return set;
    }

    QJsonObject xrayPeer(const QJsonObject& settings, const QString& protocol)
    {
        const auto inlinePeer = xrayInlinePeer(settings);
        if (!inlinePeer) return {};
        if (*inlinePeer) return settings;
        const auto list = protocol == "vmess" || protocol == "vless" ? QStringLiteral("vnext") : QStringLiteral("servers");
        return xrayAgreed(settings, list).toArray().at(0).toObject();
    }

    QJsonObject xrayOutboundPeer(const QJsonObject& o)
    {
        const auto protocol = xrayAgreedProtocol(o);
        if (!protocol || protocol->isEmpty()) return {};
        return xrayPeer(xrayAgreed(o, "settings").toObject(), *protocol);
    }

    QString xrayOutboundAddress(const QJsonObject& o)
    {
        return xrayAgreed(xrayOutboundPeer(o), "address").toString();
    }

    QJsonObject firstXrayEgress(const QJsonArray& outbounds)
    {
        for (const auto& value : outbounds) {
            if (auto o = value.toObject(); !isXrayInfra(o["protocol"].toString())) return o;
        }
        return {};
    }

    QStringList XrayConfigServerNames(const QJsonObject& root)
    {
        return gatherServerNames(xrayAgreed(root, "outbounds").toArray(), [](const QJsonObject& o) -> QString {
            const auto protocol = xrayAgreedProtocol(o);
            // Протокола нет или написания спорят — неизвестно даже, куда он
            // звонит, и выдумывать за него адрес нельзя.
            if (!protocol || protocol->isEmpty() || isXrayInfra(*protocol)) return {};
            return xrayOutboundAddress(o);
        });
    }

    QStringList SingBoxConfigServerNames(const QJsonObject& root)
    {
        return gatherServerNames(root["outbounds"].toArray(), [](const QJsonObject& o) -> QString {
            const auto type = o["type"].toString();
            // selector и urltest своего адреса не имеют, и server у них пуст:
            // их участники лежат в том же списке и просматриваются сами.
            if (type.isEmpty() || isSingBoxInfra(type)) return {};
            return o["server"].toString();
        });
    }
} // namespace Configs
