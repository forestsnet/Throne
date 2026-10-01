#pragma once

#include <QString>

namespace Configs {
    // Разбор адреса DNS-сервера в том виде, в каком его вписывает человек:
    // 1.1.1.1, tls://77.88.8.8, https://dns.google/dns-query, [2606:4700::1111]:853.
    //
    // Раньше разбор жил внутри buildDnsObj() и ошибался дважды: схему udp:// он не
    // знал вовсе и уносил её в адрес сервера, а литерал IPv6 резал по первому
    // двоеточию, превращая 2606:4700::1111 в хост «2606» и порт 4700. И то и
    // другое ядро принимало молча, а человек видел только то, что ничего не
    // работает.
    struct DnsAddress {
        // Тип сервера в терминах sing-box: udp, tcp, tls, quic, https, h3, dhcp, local.
        QString type;
        QString server;
        int port = -1;          // -1 — не задан, ядро возьмёт умолчание по типу
        QString path;           // только https и h3
        QString interfaceName;  // только dhcp, пусто означает «выбрать самому»
        bool valid = false;
        QString error;          // человеческим языком, для подсказки в настройках

        [[nodiscard]] bool isLocal() const { return type == QLatin1String("local"); }
        [[nodiscard]] bool isDhcp() const { return type == QLatin1String("dhcp"); }
    };

    DnsAddress ParseDnsAddress(const QString &address);

    // Обратно в строку. Нужно и каталогу серверов, и подбору: там адрес
    // пересобирается с другим транспортом.
    QString FormatDnsAddress(const DnsAddress &address);

    // Тот же сервер другим транспортом: tls://77.88.8.8 -> udp://77.88.8.8.
    // Для dhcp и local возвращает пустую строку: менять там нечего.
    QString DnsAddressWithType(const DnsAddress &address, const QString &type);
} // namespace Configs
