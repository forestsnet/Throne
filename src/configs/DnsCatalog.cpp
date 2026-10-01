#include "include/configs/DnsCatalog.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "include/configs/DnsAddress.hpp"
#include "include/database/SettingsRepo.h"
#include "include/global/Configs.hpp"
#include "include/global/Logger.hpp"

namespace Configs {
    namespace {
        const auto kFileName = QStringLiteral("dns_servers.json");
        const auto kBuiltIn = QStringLiteral(":/neko/dns_servers.json");

        QList<DnsCatalogEntry> parse(const QByteArray &raw, QString *error) {
            QList<DnsCatalogEntry> entries;
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(raw, &parseError);
            if (parseError.error != QJsonParseError::NoError) {
                if (error != nullptr) *error = parseError.errorString();
                return entries;
            }
            const auto servers = document.object().value("servers").toArray();
            if (servers.isEmpty()) {
                if (error != nullptr) *error = QStringLiteral("в списке нет ни одного сервера");
                return entries;
            }
            for (const auto &value : servers) {
                const auto object = value.toObject();
                DnsCatalogEntry entry;
                entry.title = object.value("title").toString().trimmed();
                for (const auto &address : object.value("addresses").toArray()) {
                    const auto text = address.toString().trimmed();
                    // Битые адреса выкидываем здесь, чтобы подбор не тратил на
                    // них ожидание, а человек увидел причину в журнале.
                    const auto parsed = ParseDnsAddress(text);
                    if (!parsed.valid) {
                        LOG_WARN(QString("dns catalog: %1 is skipped (%2)").arg(text, parsed.error));
                        continue;
                    }
                    entry.addresses << text;
                }
                if (entry.title.isEmpty() || entry.addresses.isEmpty()) continue;

                const auto roles = object.value("roles").toArray();
                if (!roles.isEmpty()) {
                    entry.forDirect = false;
                    entry.forRemote = false;
                    for (const auto &role : roles) {
                        if (role.toString() == QLatin1String("direct")) entry.forDirect = true;
                        if (role.toString() == QLatin1String("remote")) entry.forRemote = true;
                    }
                }
                entries << entry;
            }
            if (entries.isEmpty() && error != nullptr) {
                *error = QStringLiteral("ни одной пригодной записи");
            }
            return entries;
        }

        QList<DnsCatalogEntry> builtIn() {
            QFile file(kBuiltIn);
            if (!file.open(QIODevice::ReadOnly)) {
                LOG_WARN("dns catalog: the built-in list is missing from the build");
                return {};
            }
            return parse(file.readAll(), nullptr);
        }
    } // namespace

    QString DnsCatalogPath() {
        return QDir(Configs::GetBasePath()).filePath(kFileName);
    }

    QList<DnsCatalogEntry> DnsCatalog() {
        QFile own(DnsCatalogPath());
        if (own.exists() && own.open(QIODevice::ReadOnly)) {
            QString error;
            const auto entries = parse(own.readAll(), &error);
            if (!entries.isEmpty()) return entries;
            // Свой список сломан — не оставлять же человека совсем без списка.
            LOG_WARN(QString("dns catalog: %1 is unusable (%2), falling back to the built-in list")
                         .arg(DnsCatalogPath(), error));
        }
        return builtIn();
    }

    bool SaveDnsCatalog(const QString &json, QString *error) {
        QString parseError;
        if (parse(json.toUtf8(), &parseError).isEmpty()) {
            if (error != nullptr) *error = parseError;
            return false;
        }
        QFile file(DnsCatalogPath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error != nullptr) *error = file.errorString();
            return false;
        }
        file.write(json.toUtf8());
        file.close();
        return true;
    }

    QStringList DnsCandidates(const QString &configured, bool direct) {
        QStringList candidates;
        const auto add = [&candidates](const QString &address) {
            if (address.isEmpty()) return;
            if (candidates.contains(address, Qt::CaseInsensitive)) return;
            candidates << address;
        };

        const auto chosen = ParseDnsAddress(configured);
        if (chosen.valid) {
            add(configured);
            // Тот же сервер другим транспортом. Режут обычно транспорт, а не
            // сервер, и это самый дешёвый способ выбраться.
            for (const auto &type : {"udp", "tls", "https", "tcp"}) {
                add(DnsAddressWithType(chosen, QString::fromLatin1(type)));
            }
        }

        for (const auto &own : Configs::dataManager->settingsRepo->dns_custom_servers) {
            add(own.trimmed());
        }

        QStringList system;
        for (const auto &entry : DnsCatalog()) {
            if (direct && !entry.forDirect) continue;
            if (!direct && !entry.forRemote) continue;
            if (entry.isSystem()) {
                if (direct) system << entry.addresses;
                continue;
            }
            for (const auto &address : entry.addresses) add(address);
        }
        for (const auto &address : system) add(address);

        return candidates;
    }
} // namespace Configs
