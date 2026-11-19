#include "include/global/ExitLookup.hpp"

#include "include/global/CountryHelper.hpp"
#include "include/global/ExitLookupPlan.hpp"
#include "include/global/HTTPRequestHelper.hpp"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <QUrl>

namespace Configs_network {
    namespace {
        constexpr qint64 kExitLookupAutoBudgetMs = 20000;
        // Срок одной попытки. Десять секунд умолчания съедали весь бюджет за
        // две неудачи, и до проверки по адресу очередь не доходила.
        constexpr int kExitLookupAttemptMs = 6000;
        constexpr int kDohAttemptMs = 4000;

        enum class ExitLookupFormat { IpApi, IpWho, IpInfo, IpApiCo, Ip2Location, FreeIpApi, CloudflareTrace };

        struct ExitLookupEntry {
            ExitLookupProvider provider;
            ExitLookupFormat format;
        };

        const QList<ExitLookupEntry> &exitLookupEntries() {
            static const QList<ExitLookupEntry> entries = {
                {{QStringLiteral("ip-api"), QStringLiteral("ip-api.com"), QStringLiteral("http://ip-api.com/json/")}, ExitLookupFormat::IpApi},
                {{QStringLiteral("ipwho"), QStringLiteral("ipwho.is"), QStringLiteral("https://ipwho.is/")}, ExitLookupFormat::IpWho},
                {{QStringLiteral("ipinfo"), QStringLiteral("ipinfo.io"), QStringLiteral("https://ipinfo.io/json")}, ExitLookupFormat::IpInfo},
                {{QStringLiteral("ipapi-co"), QStringLiteral("ipapi.co"), QStringLiteral("https://ipapi.co/json/")}, ExitLookupFormat::IpApiCo},
                {{QStringLiteral("ip2location"), QStringLiteral("IP2Location.io"), QStringLiteral("https://api.ip2location.io/")},
                 ExitLookupFormat::Ip2Location},
                {{QStringLiteral("freeipapi"), QStringLiteral("FreeIPAPI"), QStringLiteral("https://free.freeipapi.com/api/v1/json")},
                 ExitLookupFormat::FreeIpApi},
                {{QStringLiteral("cloudflare"), QStringLiteral("Cloudflare"), QStringLiteral("https://www.cloudflare.com/cdn-cgi/trace")},
                 ExitLookupFormat::CloudflareTrace},
                // Без имени: сертификат Cloudflare выписан и на эти адреса, а
                // trace у них тот же и со строкой loc. Отвечают там, где DNS
                // ядра уже молчит, и разрешать для них нечего.
                {{QStringLiteral("cloudflare-ip"), QStringLiteral("Cloudflare (1.1.1.1)"), QStringLiteral("https://1.1.1.1/cdn-cgi/trace")},
                 ExitLookupFormat::CloudflareTrace},
                {{QStringLiteral("cloudflare-ip2"), QStringLiteral("Cloudflare (1.0.0.1)"), QStringLiteral("https://1.0.0.1/cdn-cgi/trace")},
                 ExitLookupFormat::CloudflareTrace},
            };
            return entries;
        }

        const ExitLookupEntry *exitLookupFind(const QString &id) {
            for (const auto &entry : exitLookupEntries()) {
                if (entry.provider.id == id) return &entry;
            }
            return nullptr;
        }

        // XX is Cloudflare's "unknown"; its Tor marker T1 already fails the letter test.
        QString exitLookupCountryCode(const QString &raw) {
            const QString code = raw.trimmed();
            if (code.size() != 2) return {};
            for (const QChar c : code) {
                if (c.unicode() > 127 || !c.isLetter()) return {};
            }
            const QString upper = code.toUpper();
            return upper == QLatin1String("XX") ? QString() : upper;
        }

        // Fills info from one answer; returns the reason the provider gives for failing, if it signals a failure.
        QString exitLookupParse(ExitLookupFormat format, const QByteArray &body, ExitInfo *info) {
            if (format == ExitLookupFormat::CloudflareTrace) {
                for (const auto &line : QString::fromUtf8(body).split(QLatin1Char('\n'))) {
                    const auto eq = line.indexOf(QLatin1Char('='));
                    if (eq <= 0) continue;
                    const auto key = line.left(eq).trimmed();
                    if (key == QLatin1String("ip")) info->ip = line.mid(eq + 1).trimmed();
                    else if (key == QLatin1String("loc")) info->countryCode = line.mid(eq + 1).trimmed();
                }
                return {};
            }

            const auto doc = QJsonDocument::fromJson(body);
            if (!doc.isObject()) return QStringLiteral("unparsable answer");
            const auto obj = doc.object();
            const auto field = [&obj](const char *key) {
                const QString value = obj.value(QLatin1String(key)).toString().trimmed();
                // IP2Location answers "-" for whatever it does not know.
                return value == QLatin1String("-") ? QString() : value;
            };
            const auto failure = [](const QString &reason) {
                return reason.isEmpty() ? QStringLiteral("the service reported a failure") : reason;
            };

            switch (format) {
                case ExitLookupFormat::IpApi:
                    if (field("status") != QLatin1String("success")) return failure(field("message"));
                    *info = {field("query"), field("countryCode"), field("country"), field("city")};
                    break;
                case ExitLookupFormat::IpWho:
                    if (const auto success = obj.value(QLatin1String("success")); success.isBool() && !success.toBool()) {
                        return failure(field("message"));
                    }
                    *info = {field("ip"), field("country_code"), field("country"), field("city")};
                    break;
                case ExitLookupFormat::IpInfo:
                    *info = {field("ip"), field("country"), {}, field("city")};
                    break;
                case ExitLookupFormat::IpApiCo:
                    if (obj.value(QLatin1String("error")).toBool()) return failure(field("reason"));
                    *info = {field("ip"), field("country_code"), field("country_name"), field("city")};
                    break;
                case ExitLookupFormat::Ip2Location:
                    if (const auto error = obj.value(QLatin1String("error")); error.isObject()) {
                        return failure(error.toObject().value(QLatin1String("error_message")).toString().trimmed());
                    }
                    *info = {field("ip"), field("country_code"), field("country_name"), field("city_name")};
                    break;
                case ExitLookupFormat::FreeIpApi:
                    *info = {field("ipAddress"), field("countryCode"), field("countryName"), field("cityName")};
                    break;
                case ExitLookupFormat::CloudflareTrace:
                    break;
            }
            return {};
        }

        // Имя провайдера разрешает ядро, и под туннелем оно иногда молчит до
        // самого таймаута. Тогда разрешаем сами — по адресам резолверов, так
        // что для самого резолва имя уже не нужно, и не одним сервером.
        QStringList resolveViaDoh(const QString &host) {
            if (host.isEmpty() || !QHostAddress(host).isNull()) return {};
            static const QStringList resolvers = {
                QStringLiteral("https://1.1.1.1/dns-query?type=A&name=%1"),
                QStringLiteral("https://8.8.8.8/resolve?type=A&name=%1"),
            };
            const QString name = QString::fromUtf8(QUrl::toPercentEncoding(host));
            for (const QString &resolver : resolvers) {
                HttpGetOptions options;
                options.useProxy = true;
                options.timeoutMs = kDohAttemptMs;
                options.headers << qMakePair(QByteArray("Accept"), QByteArray("application/dns-json"));
                const auto resp = NetworkRequestHelper::HttpGet(resolver.arg(name), options);
                if (!resp.error.isEmpty()) continue;
                const auto addresses = ParseDohAddresses(resp.data);
                if (!addresses.isEmpty()) return addresses;
            }
            return {};
        }

        // address пуст — обычный запрос по имени. Иначе идём на адрес, а имя
        // уносим в Host и в проверку сертификата: у провайдеров за CDN без
        // него отвечает чужой сайт или не отвечает никто.
        bool exitLookupTry(const ExitLookupEntry &entry, const QString &address, ExitInfo *info, QString *error) {
            HttpGetOptions options;
            options.useProxy = true;
            options.timeoutMs = kExitLookupAttemptMs;
            QString url = entry.provider.url;
            if (!address.isEmpty()) {
                const QString host = QUrl(entry.provider.url, QUrl::StrictMode).host();
                url = UrlWithAddress(entry.provider.url, address);
                if (url.isEmpty() || host.isEmpty()) {
                    *error = entry.provider.label + QStringLiteral(": could not ask %1 directly").arg(address);
                    return false;
                }
                // HTTP/2 берёт authority из URL, и имя до сервера не доедет.
                options.http2 = false;
                options.peerVerifyName = host;
                options.headers << qMakePair(QByteArray("Host"), host.toUtf8());
            }
            const auto resp = NetworkRequestHelper::HttpGet(url, options);
            QString reason = resp.error;
            if (reason.isEmpty()) reason = exitLookupParse(entry.format, resp.data, info);
            info->countryCode = exitLookupCountryCode(info->countryCode);
            if (info->country.isEmpty() && !info->countryCode.isEmpty()) info->country = CountryCodeToName(info->countryCode);
            if (reason.isEmpty() && !info->hasCountry()) reason = QStringLiteral("no country in the answer");
            if (reason.isEmpty()) return true;
            *error = entry.provider.label + (address.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(address)) +
                     QStringLiteral(": ") + reason;
            return false;
        }
    } // namespace

    const QList<ExitLookupProvider> &ExitLookupProviders() {
        static const QList<ExitLookupProvider> providers = [] {
            QList<ExitLookupProvider> list;
            for (const auto &entry : exitLookupEntries()) list << entry.provider;
            return list;
        }();
        return providers;
    }

    ExitInfo LookupExit(const QString &providerId, QString *error) {
        QStringList known;
        for (const auto &entry : exitLookupEntries()) known << entry.provider.id;

        QElapsedTimer elapsed;
        elapsed.start();
        QStringList errors;
        QString answeredIp;
        // Имя уже один раз не разрешилось силами ядра, а своими разрешилось:
        // у остальных провайдеров ждать его повторно незачем, они резолвятся
        // тем же мёртвым резолвером и сгорят по тому же сроку.
        bool preferAddress = false;
        const auto keep = [&](const ExitInfo &info) {
            if (answeredIp.isEmpty()) answeredIp = info.ip;
        };

        for (const QString &id : ExitLookupChain(providerId, known)) {
            const auto *entry = exitLookupFind(id);
            if (entry == nullptr) continue;
            if (!errors.isEmpty() && elapsed.elapsed() >= kExitLookupAutoBudgetMs) break;
            const QString host = QUrl(entry->provider.url, QUrl::StrictMode).host();

            QStringList addresses;
            if (preferAddress) addresses = resolveViaDoh(host);
            if (addresses.isEmpty()) {
                ExitInfo info;
                QString reason;
                if (exitLookupTry(*entry, {}, &info, &reason)) {
                    if (error != nullptr) error->clear();
                    return info;
                }
                errors << reason;
                keep(info);
                if (elapsed.elapsed() >= kExitLookupAutoBudgetMs) break;
                addresses = resolveViaDoh(host);
            }
            // Один адрес: не ответил он — соседние из того же ответа стоят за
            // тем же сервером и ответят так же.
            if (addresses.isEmpty()) continue;
            preferAddress = true;
            ExitInfo viaAddress;
            QString addressReason;
            if (exitLookupTry(*entry, addresses.first(), &viaAddress, &addressReason)) {
                if (error != nullptr) error->clear();
                return viaAddress;
            }
            errors << addressReason;
            keep(viaAddress);
        }
        if (error != nullptr) *error = errors.join(QStringLiteral(" · "));
        ExitInfo partial;
        partial.ip = answeredIp;
        return partial;
    }

    QString ExitLocationText(const ExitInfo &info) {
        QString text = info.country.isEmpty() ? info.countryCode : info.country;
        if (text.isEmpty()) return {};
        if (!info.city.isEmpty()) text += QStringLiteral(", ") + info.city;
        if (!info.countryCode.isEmpty()) text = CountryCodeToFlag(info.countryCode) + QLatin1Char(' ') + text;
        return text;
    }
} // namespace Configs_network
