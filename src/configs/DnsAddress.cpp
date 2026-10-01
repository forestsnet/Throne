#include "include/configs/DnsAddress.hpp"

#include <QStringList>

namespace Configs {
    namespace {
        const QStringList kSchemes = {"udp", "tcp", "tls", "quic", "https", "h3"};

        bool typeCarriesPath(const QString &type) {
            return type == QLatin1String("https") || type == QLatin1String("h3");
        }

        // Литерал адреса, а не имя. Хватает признака «нет ни одной буквы»:
        // имена без букв не бывают, а QHostAddress тянул бы в разбор строки
        // зависимость от Qt6::Network.
        bool looksLikeIpLiteral(const QString &host) {
            for (const auto ch : host) {
                if (ch.isLetter()) return false;
            }
            return !host.isEmpty();
        }

        DnsAddress invalid(const QString &why) {
            DnsAddress result;
            result.error = why;
            return result;
        }

        // Хост и порт из того, что осталось после схемы. Литерал IPv6 здесь
        // приходится разбирать руками: в нём двоеточий больше одного, и отделить
        // порт можно только по скобкам либо по их отсутствию.
        bool splitHostPort(const QString &input, QString &host, int &port, QString &error) {
            QString rest = input;
            port = -1;

            if (rest.startsWith('[')) {
                const auto close = rest.indexOf(']');
                if (close < 0) {
                    error = QStringLiteral("не закрыта скобка в адресе IPv6");
                    return false;
                }
                host = rest.mid(1, close - 1);
                rest = rest.mid(close + 1);
                if (rest.isEmpty()) return true;
                if (!rest.startsWith(':')) {
                    error = QStringLiteral("после ] ожидается :порт");
                    return false;
                }
                rest = rest.mid(1);
            } else if (rest.count(':') > 1) {
                // Голый IPv6 без скобок: порта в такой записи быть не может.
                host = rest;
                return true;
            } else {
                const auto colon = rest.indexOf(':');
                if (colon < 0) {
                    host = rest;
                    return true;
                }
                host = rest.left(colon);
                rest = rest.mid(colon + 1);
            }

            if (rest.isEmpty()) {
                error = QStringLiteral("порт не указан");
                return false;
            }
            bool ok = false;
            const int parsed = rest.toInt(&ok);
            if (!ok || parsed < 1 || parsed > 65535) {
                error = QStringLiteral("порт должен быть числом от 1 до 65535");
                return false;
            }
            port = parsed;
            return true;
        }
    } // namespace

    DnsAddress ParseDnsAddress(const QString &address) {
        const QString trimmed = address.trimmed();
        if (trimmed.isEmpty()) return invalid(QStringLiteral("адрес пуст"));

        // «local» и его давние написания вроде local+underlying: сервер системы.
        if (trimmed.startsWith(QLatin1String("local"), Qt::CaseInsensitive)) {
            DnsAddress result;
            result.type = QStringLiteral("local");
            result.valid = true;
            return result;
        }

        if (trimmed.startsWith(QLatin1String("dhcp://"), Qt::CaseInsensitive)) {
            DnsAddress result;
            result.type = QStringLiteral("dhcp");
            result.interfaceName = trimmed.mid(7);
            if (result.interfaceName == QLatin1String("auto")) result.interfaceName.clear();
            result.valid = true;
            return result;
        }

        QString type = QStringLiteral("udp");
        QString rest = trimmed;
        const auto schemeEnd = trimmed.indexOf(QLatin1String("://"));
        if (schemeEnd > 0) {
            const QString scheme = trimmed.left(schemeEnd).toLower();
            if (!kSchemes.contains(scheme)) {
                return invalid(QStringLiteral("неизвестная схема «%1»").arg(scheme));
            }
            type = scheme;
            rest = trimmed.mid(schemeEnd + 3);
        } else if (trimmed.contains(QLatin1String("//"))) {
            return invalid(QStringLiteral("адрес похож на ссылку, но схема не распознана"));
        }

        QString path;
        if (typeCarriesPath(type)) {
            const auto slash = rest.indexOf('/');
            if (slash >= 0) {
                path = rest.mid(slash);
                rest = rest.left(slash);
            }
        } else if (rest.contains('/')) {
            return invalid(QStringLiteral("путь допустим только для https и h3"));
        }

        if (rest.isEmpty()) return invalid(QStringLiteral("не указан адрес сервера"));

        DnsAddress result;
        result.type = type;
        result.path = path;
        QString error;
        if (!splitHostPort(rest, result.server, result.port, error)) return invalid(error);
        if (result.server.isEmpty()) return invalid(QStringLiteral("не указан адрес сервера"));
        if (result.server.contains(' ')) return invalid(QStringLiteral("адрес содержит пробел"));

        result.valid = true;
        return result;
    }

    QString FormatDnsAddress(const DnsAddress &address) {
        if (!address.valid) return {};
        if (address.isLocal()) return QStringLiteral("local");
        if (address.isDhcp()) {
            return QStringLiteral("dhcp://%1")
                .arg(address.interfaceName.isEmpty() ? QStringLiteral("auto") : address.interfaceName);
        }

        QString host = address.server;
        // Порт рядом с литералом IPv6 читается только в скобках.
        if (address.port != -1 && host.count(':') > 1) host = QStringLiteral("[%1]").arg(host);

        QString result = QStringLiteral("%1://%2").arg(address.type, host);
        if (address.port != -1) result += QStringLiteral(":%1").arg(address.port);
        if (!address.path.isEmpty()) result += address.path;
        return result;
    }

    QString DnsAddressWithType(const DnsAddress &address, const QString &type) {
        if (!address.valid || address.isLocal() || address.isDhcp()) return {};
        if (!kSchemes.contains(type)) return {};

        DnsAddress copy = address;
        copy.type = type;
        // Порт и путь принадлежали прежнему транспорту: 853 у DoT и /dns-query у
        // DoH ничего не значат для UDP, а подставленные наугад только мешают.
        copy.port = -1;
        copy.path.clear();
        if (typeCarriesPath(type)) {
            if (looksLikeIpLiteral(address.server)) {
                // DoH по голому адресу без пути не работает: у всех известных
                // резолверов он /dns-query.
                copy.path = QStringLiteral("/dns-query");
            } else {
                copy.path = address.path.isEmpty() ? QStringLiteral("/dns-query") : address.path;
            }
        }
        return FormatDnsAddress(copy);
    }
} // namespace Configs
