#include <QTest>
#include "include/global/DirectDnsSelection.hpp"

using namespace Configs;

class DirectDnsSelectionTest : public QObject {
    Q_OBJECT
private slots:
    void plaintextAnswersDoNotMeanNoResponse() {
        const auto result = SelectDirectDns({{"udp://1.1.1.1", true, 10, {}, {"192.0.2.1"}}}, {}, true, true);
        QVERIFY(result.address.isEmpty());
        QCOMPARE(result.failure, DirectDnsFailure::NoUsableResolver);
        QCOMPARE(result.serverAddresses, QStringList{"192.0.2.1"});
        QVERIFY(!DirectDnsFailureText(result.failure).contains("No DNS server answered"));
    }
    void noAnswersHaveTheirOwnFailure() {
        const auto result = SelectDirectDns({{"tls://1.1.1.1", false, -1, "timeout", {}}}, {}, true, true);
        QCOMPARE(result.failure, DirectDnsFailure::NoResponse);
        QVERIFY(result.serverAddresses.isEmpty());
    }
    void aResponseWithoutTheServerAddressCannotStartTheTunnel() {
        const auto result = SelectDirectDns({{"tls://1.1.1.1", true, 10, {}, {}}}, {}, true, true);
        QCOMPARE(result.failure, DirectDnsFailure::NameNotResolved);
        QVERIFY(result.address.isEmpty());
    }
    void negativeDnsResponsesAreNotTimeouts() {
        const auto result = SelectDirectDns({{"tls://1.1.1.1", false, 10, "NXDOMAIN", {}, true}}, {}, true, true);
        QCOMPARE(result.failure, DirectDnsFailure::NameNotResolved);
        QVERIFY(result.address.isEmpty());
        QVERIFY(DirectDnsFailureText(result.failure).contains("did not return an address"));
    }
    void fastestSuccessfulAlternativeWins() {
        const auto result = SelectDirectDns({{"tls://1.1.1.1", false, -1, "timeout", {}},
                                             {"tls://8.8.8.8", true, 80, {}, {"192.0.2.2"}},
                                             {"https://9.9.9.9/dns-query", true, 15, {}, {"192.0.2.3"}}},
                                            "tls://1.1.1.1", true, true);
        QCOMPARE(result.address, QString("https://9.9.9.9/dns-query"));
        QCOMPARE(result.serverAddresses, QStringList{"192.0.2.3"});
    }
    void configuredResolverWinsOverFasterAlternative() {
        const auto result = SelectDirectDns({{"tls://1.1.1.1", true, 10, {}, {"192.0.2.1"}},
                                             {"tls://8.8.8.8", true, 80, {}, {"192.0.2.2"}}},
                                            "tls://8.8.8.8", true, true);
        QCOMPARE(result.address, QString("tls://8.8.8.8"));
        QCOMPARE(result.serverAddresses, QStringList{"192.0.2.2"});
        QCOMPARE(result.failure, DirectDnsFailure::None);
    }
    void systemProxyCanUsePlaintext() {
        const auto result = SelectDirectDns({{"udp://1.1.1.1", true, 10, {}, {"192.0.2.1"}}}, {}, false, true);
        QCOMPARE(result.address, QString("udp://1.1.1.1"));
        QCOMPARE(result.failure, DirectDnsFailure::None);
    }
    void unsafeConfiguredResolverDoesNotBeatEncryptedAlternative() {
        const auto result = SelectDirectDns({{"udp://1.1.1.1", true, 10, {}, {"192.0.2.1"}},
                                             {"tls://8.8.8.8", true, 80, {}, {"192.0.2.2"}}},
                                            "udp://1.1.1.1", true, true);
        QCOMPARE(result.address, QString("tls://8.8.8.8"));
        QCOMPARE(result.serverAddresses, QStringList{"192.0.2.2"});
    }
};

QTEST_GUILESS_MAIN(DirectDnsSelectionTest)
#include "DirectDnsSelectionTest.moc"
