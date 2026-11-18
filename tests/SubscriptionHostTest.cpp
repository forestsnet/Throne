#include <QtTest>

#include "include/configs/sub/SubscriptionHost.hpp"

using namespace Subscription;

class SubscriptionHostTest : public QObject {
    Q_OBJECT

    static QString sub() { return QStringLiteral("https://old.example.com/sub/token-abc?mode=v2ray"); }

private slots:
    void movesOnlyTheHost() {
        QCOMPARE(SwapSubscriptionHost(sub(), QStringLiteral("new.example.com")),
                 QStringLiteral("https://new.example.com/sub/token-abc?mode=v2ray"));
    }

    void keepsThePortTheCandidateDoesNotName() {
        QCOMPARE(SwapSubscriptionHost(QStringLiteral("https://old.example.com:8443/sub"),
                                      QStringLiteral("new.example.com")),
                 QStringLiteral("https://new.example.com:8443/sub"));
    }

    void takesThePortTheCandidateNames() {
        QCOMPARE(SwapSubscriptionHost(QStringLiteral("https://old.example.com/sub"),
                                      QStringLiteral("new.example.com:8443")),
                 QStringLiteral("https://new.example.com:8443/sub"));
    }

    // Заголовок на plain http пишет любой, кто сидит на пути, а переезд остаётся навсегда.
    void plainHttpIsRefused() {
        QVERIFY(SwapSubscriptionHost(QStringLiteral("http://old.example.com/sub"),
                                     QStringLiteral("new.example.com"))
                    .isEmpty());
    }

    void theHostItAlreadyDialsIsRefused() {
        QVERIFY(SwapSubscriptionHost(sub(), QStringLiteral("old.example.com")).isEmpty());
        QVERIFY(SwapSubscriptionHost(sub(), QStringLiteral("OLD.EXAMPLE.COM")).isEmpty());
    }

    // Переезд меняет домен. Путь и query несут токен подписчика, и подменять их
    // заголовком нельзя: это уже не переезд, а выдача другой подписки.
    void aCandidateThatIsNotABareHostIsRefused() {
        const QStringList rejected = {
            QStringLiteral("new.example.com/other/token"),
            QStringLiteral("https://new.example.com"),
            QStringLiteral("user@new.example.com"),
            QStringLiteral("new.example.com?token=theirs"),
            QStringLiteral("new.example.com two.example.com"),
            QStringLiteral("localhost"),
            QStringLiteral(""),
            QStringLiteral("   "),
        };
        for (const QString &candidate : rejected) {
            QVERIFY2(SwapSubscriptionHost(sub(), candidate).isEmpty(), qPrintable(candidate));
        }
    }

    void ipv6CandidateKeepsItsBrackets() {
        QCOMPARE(SwapSubscriptionHost(QStringLiteral("https://old.example.com/sub"),
                                      QStringLiteral("[2001:db8::1]:8443")),
                 QStringLiteral("https://[2001:db8::1]:8443/sub"));
    }

    void hostNamesThePortOnlyWhenTheUrlHasOne() {
        QCOMPARE(SubscriptionHost(sub()), QStringLiteral("old.example.com"));
        QCOMPARE(SubscriptionHost(QStringLiteral("https://old.example.com:8443/sub")),
                 QStringLiteral("old.example.com:8443"));
        QVERIFY(SubscriptionHost(QStringLiteral("not a url")).isEmpty());
    }

    void mirrorsSplitOnWhateverSeparatorThePanelChose() {
        const auto hosts = ParseMirrorHosts(
            QStringLiteral("a.example.com, b.example.com;c.example.com\nd.example.com:8443"));
        QCOMPARE(hosts, QStringList({QStringLiteral("a.example.com"), QStringLiteral("b.example.com"),
                                     QStringLiteral("c.example.com"), QStringLiteral("d.example.com:8443")}));
    }

    void mirrorsDropRepeatsAndWhatDoesNotParse() {
        const auto hosts = ParseMirrorHosts(
            QStringLiteral("a.example.com, A.EXAMPLE.COM, nonsense/path, https://b.example.com, b.example.com"));
        QCOMPARE(hosts, QStringList({QStringLiteral("a.example.com"), QStringLiteral("b.example.com")}));
    }

    // Зеркало дают, когда свой адрес уже не ответил: предлагать его снова нечего.
    void candidatesKeepThePathAndSkipTheHostThatFailed() {
        const auto candidates = MirrorCandidates(sub(), QStringLiteral("old.example.com, m1.example.com"));
        QCOMPARE(candidates, QStringList({QStringLiteral("https://m1.example.com/sub/token-abc?mode=v2ray")}));
    }

    void anHttpSubscriptionGetsNoMirrorsEither() {
        QVERIFY(MirrorCandidates(QStringLiteral("http://old.example.com/sub"),
                                 QStringLiteral("m1.example.com"))
                    .isEmpty());
    }
};

QTEST_MAIN(SubscriptionHostTest)
#include "SubscriptionHostTest.moc"
