#include <QTest>

#include "include/configs/sub/ProviderPolicy.hpp"

using namespace Subscription;

namespace {
    QList<QPair<QByteArray, QByteArray>> H(std::initializer_list<QPair<QByteArray, QByteArray>> items) {
        return QList<QPair<QByteArray, QByteArray>>(items);
    }
}

class ProviderPolicyTest : public QObject {
    Q_OBJECT
private slots:
    void emptyHeadersGiveEmptyPolicy() {
        const auto p = ParseProviderPolicy({});
        QVERIFY(p.isEmpty());
        QVERIFY(!p.tunEnable.has_value());
    }

    void absentFlagIsNotFalse() {
        const auto p = ParseProviderPolicy(H({{"support-url", "https://example.invalid"}}));
        QVERIFY(!p.tunEnable.has_value());
        QCOMPARE(p.supportUrl, QStringLiteral("https://example.invalid"));
    }

    void booleansParsed() {
        const auto p = ParseProviderPolicy(H({
            {"tun-enable", "true"},
            {"hide-settings", "false"},
            {"subscription-pin", "1"},
            {"no-limit-enabled", "0"},
        }));
        QCOMPARE(p.tunEnable.value(), true);
        QCOMPARE(p.hideSettings.value(), false);
        QCOMPARE(p.pin.value(), true);
    }

    void headerNamesAreCaseInsensitive() {
        const auto p = ParseProviderPolicy(H({{"TUN-Enable", "true"}}));
        QCOMPARE(p.tunEnable.value(), true);
    }

    void base64ValuesDecoded() {
        // "VmxleFZQTiB8IERFVg==" -> "VlexVPN | DEV"
        const auto p = ParseProviderPolicy(H({{"profile-title", "base64:VmxleFZQTiB8IERFVg=="}}));
        QCOMPARE(p.title, QStringLiteral("VlexVPN | DEV"));
    }

    void brokenBase64FallsBackToRaw() {
        const auto p = ParseProviderPolicy(H({{"profile-title", "base64:!!!not-base64!!!"}}));
        QCOMPARE(p.title, QStringLiteral("!!!not-base64!!!"));
    }

    void numericHeadersParsed() {
        const auto p = ParseProviderPolicy(H({
            {"profile-update-interval", "1"},
            {"subscription-refill-date", "1788566400"},
        }));
        QCOMPARE(p.updateIntervalHours, 1);
        QCOMPARE(p.refillDate, static_cast<qint64>(1788566400));
    }

    void nonNumericIntervalIgnored() {
        const auto p = ParseProviderPolicy(H({{"profile-update-interval", "soon"}}));
        QCOMPARE(p.updateIntervalHours, 0);
    }

    void unknownHeadersPreserved() {
        const auto p = ParseProviderPolicy(H({{"x-brand-new-flag", "yes"}}));
        QCOMPARE(p.unknown.value("x-brand-new-flag").toString(), QStringLiteral("yes"));
    }

    void unrelatedHttpHeadersIgnored() {
        const auto p = ParseProviderPolicy(H({
            {"content-type", "application/json"},
            {"etag", "W/\"abc\""},
            {"date", "Fri, 04 Sep 2026 12:20:53 GMT"},
        }));
        QVERIFY(p.isEmpty());
        QVERIFY(p.unknown.isEmpty());
    }
};

QTEST_APPLESS_MAIN(ProviderPolicyTest)
#include "ProviderPolicyTest.moc"
