#include "include/global/ExitLookupPlan.hpp"

#include <QTest>

using namespace Configs_network;

namespace {
    QStringList knownIds() {
        return {QStringLiteral("ip-api"),     QStringLiteral("ipwho"),         QStringLiteral("ipinfo"),
                QStringLiteral("ipapi-co"),   QStringLiteral("ip2location"),   QStringLiteral("freeipapi"),
                QStringLiteral("cloudflare"), QStringLiteral("cloudflare-ip"), QStringLiteral("cloudflare-ip2")};
    }
} // namespace

class ExitLookupPlanTest : public QObject {
    Q_OBJECT

private slots:
    // ip2location первым: тот же источник спрашивает проверка «IP и страна»,
    // и две подписи в окне должны называть одну страну. Проверка по адресу
    // второй: когда DNS ядра молчит, первая попытка сгорает по сроку, и очередь
    // не должна тратить на имена весь бюджет прежде, чем дойти до адреса.
    void theAutoOrderStartsWhereTheOtherCheckDoesAndAsksTheAddressNext() {
        QCOMPARE(ExitLookupChain(QStringLiteral("auto"), knownIds()),
                 QStringList({QStringLiteral("ip2location"), QStringLiteral("cloudflare-ip"),
                              QStringLiteral("ip-api"), QStringLiteral("ipwho"), QStringLiteral("ipinfo"),
                              QStringLiteral("cloudflare"), QStringLiteral("cloudflare-ip2")}));
    }

    void anUnknownIdWalksTheAutoOrderToo() {
        QCOMPARE(ExitLookupChain(QStringLiteral("whatever"), knownIds()),
                 ExitLookupChain(QStringLiteral("auto"), knownIds()));
        QCOMPARE(ExitLookupChain(QString(), knownIds()), ExitLookupChain(QStringLiteral("auto"), knownIds()));
    }

    // Выбранного спрашивают его одного: человек выбрал провайдера ради его
    // данных, и подменять их чужими молча нельзя. Имя ему разрешат сами, если
    // ядро не смогло, — это уже дело вызывающего.
    void aChosenProviderIsAskedAlone() {
        QCOMPARE(ExitLookupChain(QStringLiteral("ip-api"), knownIds()), QStringList({QStringLiteral("ip-api")}));
        QCOMPARE(ExitLookupChain(QStringLiteral("  ipwho  "), knownIds()), QStringList({QStringLiteral("ipwho")}));
    }

    void theChainNamesOnlyWhatTheBuildKnows() {
        const QStringList known = {QStringLiteral("ip-api"), QStringLiteral("cloudflare-ip")};
        QCOMPARE(ExitLookupChain(QStringLiteral("auto"), known),
                 QStringList({QStringLiteral("cloudflare-ip"), QStringLiteral("ip-api")}));
        // Провайдера, которого эта сборка не знает, спрашивать нечем — идём по очереди.
        QCOMPARE(ExitLookupChain(QStringLiteral("ipwho"), known),
                 QStringList({QStringLiteral("cloudflare-ip"), QStringLiteral("ip-api")}));
    }

    void addressesComeFromTheAnswerRecordsInOrder() {
        const QByteArray json = R"({"Status":0,"Answer":[
            {"name":"ip-api.com","type":5,"TTL":60,"data":"cdn.ip-api.com."},
            {"name":"cdn.ip-api.com","type":1,"TTL":60,"data":"208.95.112.1"},
            {"name":"cdn.ip-api.com","type":1,"TTL":60,"data":"208.95.112.2"},
            {"name":"cdn.ip-api.com","type":1,"TTL":60,"data":"208.95.112.1"}]})";
        QCOMPARE(ParseDohAddresses(json), QStringList({QStringLiteral("208.95.112.1"), QStringLiteral("208.95.112.2")}));
    }

    // Ответы, которые эти резолверы отдают на самом деле: у Google имя с
    // точкой на конце и лишнее поле Comment, у Cloudflare — без них.
    void theAnswersTheseResolversActuallySendAreRead() {
        const QByteArray cloudflare =
            R"({"Status":0,"TC":false,"RD":true,"RA":true,"AD":false,"CD":false,)"
            R"("Question":[{"name":"ip-api.com","type":1}],)"
            R"("Answer":[{"name":"ip-api.com","type":1,"TTL":53,"data":"208.95.112.1"}]})";
        const QByteArray google =
            R"({"Status":0,"TC":false,"RD":true,"RA":true,"AD":false,"CD":false,)"
            R"("Question":[{"name":"ip-api.com.","type":1}],)"
            R"("Answer":[{"name":"ip-api.com.","type":1,"TTL":60,"data":"208.95.112.1"}],)"
            R"("Comment":"Response from 176.124.112.100."})";
        QCOMPARE(ParseDohAddresses(cloudflare), QStringList({QStringLiteral("208.95.112.1")}));
        QCOMPARE(ParseDohAddresses(google), QStringList({QStringLiteral("208.95.112.1")}));
    }

    void anAnswerWithoutUsableRecordsGivesNothing() {
        // Пусто, мусор, отказ резолвера, одни CNAME, адрес не адрес, запись AAAA.
        QVERIFY(ParseDohAddresses({}).isEmpty());
        QVERIFY(ParseDohAddresses("<html>nope</html>").isEmpty());
        QVERIFY(ParseDohAddresses(R"({"Status":3})").isEmpty());
        QVERIFY(ParseDohAddresses(R"({"Status":0,"Answer":[{"type":5,"data":"other.example.com."}]})").isEmpty());
        QVERIFY(ParseDohAddresses(R"({"Status":0,"Answer":[{"type":1,"data":"not-an-address"}]})").isEmpty());
        QVERIFY(ParseDohAddresses(R"({"Status":0,"Answer":[{"type":28,"data":"2606:4700::1111"}]})").isEmpty());
    }

    void theAddressReplacesOnlyTheName() {
        QCOMPARE(UrlWithAddress(QStringLiteral("https://api.ip2location.io/?format=json"), QStringLiteral("1.2.3.4")),
                 QStringLiteral("https://1.2.3.4/?format=json"));
        QCOMPARE(UrlWithAddress(QStringLiteral("http://ip-api.com/json/"), QStringLiteral("208.95.112.1")),
                 QStringLiteral("http://208.95.112.1/json/"));
        // Порт из URL остаётся портом URL.
        QCOMPARE(UrlWithAddress(QStringLiteral("https://ipwho.is:8443/x"), QStringLiteral("1.2.3.4")),
                 QStringLiteral("https://1.2.3.4:8443/x"));
    }

    void anIpv6AddressKeepsItsBrackets() {
        QCOMPARE(UrlWithAddress(QStringLiteral("https://ipwho.is/"), QStringLiteral("2606:4700:4700::1111")),
                 QStringLiteral("https://[2606:4700:4700::1111]/"));
    }

    void nothingToSwapIsRefused() {
        QVERIFY(UrlWithAddress(QStringLiteral("https://ipwho.is/"), QStringLiteral("example.com")).isEmpty());
        QVERIFY(UrlWithAddress(QStringLiteral("https://ipwho.is/"), QString()).isEmpty());
        QVERIFY(UrlWithAddress(QStringLiteral("not a url"), QStringLiteral("1.2.3.4")).isEmpty());
        QVERIFY(UrlWithAddress(QString(), QStringLiteral("1.2.3.4")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ExitLookupPlanTest)

#include "ExitLookupPlanTest.moc"
