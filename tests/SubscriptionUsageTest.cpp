#include <QTest>

#include "include/configs/sub/SubscriptionUsage.hpp"

using namespace Subscription;

class SubscriptionUsageTest : public QObject {
    Q_OBJECT
private slots:
    void emptyStringIsInvalid() {
        const auto u = ParseSubscriptionUserInfo("");
        QVERIFY(!u.valid);
        QCOMPARE(u.used(), static_cast<qint64>(0));
    }

    void realHeaderParsed() {
        const auto u = ParseSubscriptionUserInfo(
            "upload=0; download=266338304; total=1099511627776; expire=0");
        QVERIFY(u.valid);
        QCOMPARE(u.download, static_cast<qint64>(266338304));
        QCOMPARE(u.total, static_cast<qint64>(1099511627776));
        QCOMPARE(u.used(), static_cast<qint64>(266338304));
    }

    void zeroTotalMeansUnlimited() {
        const auto u = ParseSubscriptionUserInfo("upload=1; download=2; total=0; expire=0");
        QVERIFY(u.unlimited());
    }

    void zeroExpireHidesExpiry() {
        const auto u = ParseSubscriptionUserInfo("total=100; expire=0");
        QVERIFY(!u.hasExpiry());
    }

    void farFutureExpireHidesExpiry() {
        // 4102444800 = 01.01.2100
        const auto u = ParseSubscriptionUserInfo("total=100; expire=4102444800");
        QVERIFY(!u.hasExpiry());
    }

    void realExpiryIsShown() {
        // 1788566400 — дата из настоящей подписки
        const auto u = ParseSubscriptionUserInfo("total=100; expire=1788566400");
        QVERIFY(u.hasExpiry());
    }

    void garbageIsSkippedNotFatal() {
        const auto u = ParseSubscriptionUserInfo("upload=abc; download=5; nonsense; =7; total");
        QVERIFY(u.valid);
        QCOMPARE(u.download, static_cast<qint64>(5));
        QCOMPARE(u.upload, static_cast<qint64>(0));
    }

    void whitespaceTolerated() {
        const auto u = ParseSubscriptionUserInfo("  upload = 10 ;  DOWNLOAD = 20  ");
        QCOMPARE(u.upload, static_cast<qint64>(10));
        QCOMPARE(u.download, static_cast<qint64>(20));
    }
};

QTEST_APPLESS_MAIN(SubscriptionUsageTest)
#include "SubscriptionUsageTest.moc"
