#include <QtTest>

#include "include/configs/DnsAddress.hpp"

using Fsnt::DefaultDnsPort;
using Fsnt::DnsAddress;
using Fsnt::DnsAddressWithType;
using Fsnt::DnsUsableUnderTunnel;
using Fsnt::FormatDnsAddress;
using Fsnt::ParseDnsAddress;

class DnsAddressTest : public QObject {
    Q_OBJECT

private slots:
    void bareAddressIsUdp() {
        const auto parsed = ParseDnsAddress("77.88.8.8");
        QVERIFY(parsed.valid);
        QCOMPARE(parsed.type, QStringLiteral("udp"));
        QCOMPARE(parsed.server, QStringLiteral("77.88.8.8"));
        QCOMPARE(parsed.port, -1);
    }

    // Из-за этого и ломался совет «впишите udp://», который до правки уезжал
    // в адрес сервера целиком.
    void explicitUdpSchemeIsUnderstood() {
        const auto parsed = ParseDnsAddress("udp://77.88.8.8");
        QVERIFY(parsed.valid);
        QCOMPARE(parsed.type, QStringLiteral("udp"));
        QCOMPARE(parsed.server, QStringLiteral("77.88.8.8"));
    }

    void schemesWithPort() {
        const auto tls = ParseDnsAddress("tls://77.88.8.8:853");
        QVERIFY(tls.valid);
        QCOMPARE(tls.type, QStringLiteral("tls"));
        QCOMPARE(tls.server, QStringLiteral("77.88.8.8"));
        QCOMPARE(tls.port, 853);

        const auto tcp = ParseDnsAddress("tcp://1.1.1.1");
        QVERIFY(tcp.valid);
        QCOMPARE(tcp.type, QStringLiteral("tcp"));
        QCOMPARE(tcp.port, -1);
    }

    void httpsKeepsPath() {
        const auto parsed = ParseDnsAddress("https://dns.google/dns-query");
        QVERIFY(parsed.valid);
        QCOMPARE(parsed.type, QStringLiteral("https"));
        QCOMPARE(parsed.server, QStringLiteral("dns.google"));
        QCOMPARE(parsed.path, QStringLiteral("/dns-query"));
    }

    void bareIpv6KeepsEveryColon() {
        const auto parsed = ParseDnsAddress("2606:4700:4700::1111");
        QVERIFY(parsed.valid);
        QCOMPARE(parsed.server, QStringLiteral("2606:4700:4700::1111"));
        QCOMPARE(parsed.port, -1);
    }

    void bracketedIpv6TakesPort() {
        const auto parsed = ParseDnsAddress("tls://[2606:4700:4700::1111]:853");
        QVERIFY(parsed.valid);
        QCOMPARE(parsed.server, QStringLiteral("2606:4700:4700::1111"));
        QCOMPARE(parsed.port, 853);
        QCOMPARE(FormatDnsAddress(parsed), QStringLiteral("tls://[2606:4700:4700::1111]:853"));
    }

    void localAndDhcp() {
        QVERIFY(ParseDnsAddress("local").isLocal());
        const auto dhcp = ParseDnsAddress("dhcp://auto");
        QVERIFY(dhcp.isDhcp());
        QVERIFY(dhcp.interfaceName.isEmpty());
        QCOMPARE(ParseDnsAddress("dhcp://en0").interfaceName, QStringLiteral("en0"));
    }

    void badInputIsRejectedWithReason() {
        for (const auto &bad : {"", "   ", "sdns://abc", "tls://", "tls://1.1.1.1:0",
                                "tls://1.1.1.1:70000", "tls://1.1.1.1/path"}) {
            const auto parsed = ParseDnsAddress(bad);
            QVERIFY2(!parsed.valid, bad);
            QVERIFY2(!parsed.error.isEmpty(), bad);
        }
    }

    void roundTrip() {
        for (const auto &text : {"tls://77.88.8.8", "https://dns.google/dns-query",
                                 "tcp://1.1.1.1:5353", "udp://8.8.8.8"}) {
            QCOMPARE(FormatDnsAddress(ParseDnsAddress(text)), QString(text));
        }
    }

    // Подбор рабочего сервера меняет транспорт, не трогая сам сервер.
    void transportSwap() {
        const auto dot = ParseDnsAddress("tls://77.88.8.8:853");
        QCOMPARE(DnsAddressWithType(dot, "udp"), QStringLiteral("udp://77.88.8.8"));
        QCOMPARE(DnsAddressWithType(dot, "tcp"), QStringLiteral("tcp://77.88.8.8"));
        QCOMPARE(DnsAddressWithType(dot, "https"), QStringLiteral("https://77.88.8.8/dns-query"));

        const auto doh = ParseDnsAddress("https://dns.adguard-dns.com/dns-query");
        QCOMPARE(DnsAddressWithType(doh, "tls"), QStringLiteral("tls://dns.adguard-dns.com"));

        QVERIFY(DnsAddressWithType(ParseDnsAddress("local"), "udp").isEmpty());
    }

    // Что из этого выживет под туннелем. Проверять пробой нельзя: пока туннеля
    // нет, отвечает всё, а strict_route потом рубит 53-й порт мимо туннеля.
    void survivesTunnel() {
        QCOMPARE(DefaultDnsPort("tls"), 853);
        QCOMPARE(DefaultDnsPort("quic"), 853);
        QCOMPARE(DefaultDnsPort("https"), 443);
        QCOMPARE(DefaultDnsPort("h3"), 443);
        QCOMPARE(DefaultDnsPort("udp"), 53);
        QCOMPARE(DefaultDnsPort("tcp"), 53);

        // Мимо 53-го порта и по адресу, а не по имени.
        for (const auto *good : {"tls://77.88.8.8", "https://9.9.9.9/dns-query",
                                 "tls://1.1.1.1:853", "udp://77.88.8.8:5353"}) {
            QVERIFY2(DnsUsableUnderTunnel(good), good);
        }

        // 53-й порт: фильтр ядра глушит его и для самого ядра.
        for (const auto *blocked : {"77.88.8.8", "udp://8.8.8.8", "tcp://1.1.1.1",
                                    "tcp://9.9.9.9:53"}) {
            QVERIFY2(!DnsUsableUnderTunnel(blocked), blocked);
        }

        // Имя: разрешать его под туннелем нечем, выйдет та же петля.
        for (const auto *named : {"tls://dns.google", "https://dns.yandex.ru/dns-query",
                                  "tls://dns.quad9.net"}) {
            QVERIFY2(!DnsUsableUnderTunnel(named), named);
        }

        // Системный резолвер и адрес от маршрутизатора — оба петля или 53-й порт.
        QVERIFY(!DnsUsableUnderTunnel("local"));
        QVERIFY(!DnsUsableUnderTunnel("localhost"));
        QVERIFY(!DnsUsableUnderTunnel("dhcp://auto"));
        QVERIFY(!DnsUsableUnderTunnel(""));

        // IPv6 литералом — тоже адрес.
        QVERIFY(DnsUsableUnderTunnel("tls://[2a02:6b8::feed:0ff]"));
    }
};

QTEST_GUILESS_MAIN(DnsAddressTest)

#include "DnsAddressTest.moc"
