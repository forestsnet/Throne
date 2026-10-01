#include <QtTest>

#include "include/configs/DnsAddress.hpp"

using Configs::DnsAddress;
using Configs::DnsAddressWithType;
using Configs::FormatDnsAddress;
using Configs::ParseDnsAddress;

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
};

QTEST_GUILESS_MAIN(DnsAddressTest)

#include "DnsAddressTest.moc"
