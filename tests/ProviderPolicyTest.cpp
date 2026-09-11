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

    void appListAcceptsWhateverProvidersSend() {
        // Запятые с пробелами — самый частый вид.
        QCOMPARE(ParseAppList("qbittorrent.exe, utorrent.exe"),
                 QStringList({"qbittorrent.exe", "utorrent.exe"}));
        // Точка с запятой и переводы строк — тоже встречаются.
        QCOMPARE(ParseAppList("a.exe;b.exe\nc.exe"), QStringList({"a.exe", "b.exe", "c.exe"}));
        // Строку могли скопировать прямо из окна правил маршрутизации.
        QCOMPARE(ParseAppList("processName:qbittorrent.exe"), QStringList({"qbittorrent.exe"}));
        // JSON-массивом.
        QCOMPARE(ParseAppList("[\"a.exe\", \"b.exe\"]"), QStringList({"a.exe", "b.exe"}));
        // Повторы не нужны: правило маршрутизации одно.
        QCOMPARE(ParseAppList("a.exe, A.EXE"), QStringList({"a.exe"}));
        QVERIFY(ParseAppList("   ").isEmpty());
    }

    void appListSurvivesBase64() {
        const auto encoded = QString::fromUtf8(QByteArray("qbittorrent.exe,utorrent.exe").toBase64());
        QCOMPARE(ParseAppList(encoded), QStringList({"qbittorrent.exe", "utorrent.exe"}));
    }

    void appListsRideAlongWithThePolicy() {
        const auto p = ParseProviderPolicy(H({
            {"per-app-bypass-list", "qbittorrent.exe,utorrent.exe"},
            {"per-app-proxy-list", "chrome.exe"},
        }));
        QVERIFY(!p.isEmpty());
        QCOMPARE(ParseAppList(p.perAppBypassList).size(), 2);
        QCOMPARE(ParseAppList(p.perAppProxyList), QStringList({"chrome.exe"}));

        // Политика переживает сохранение в базу и обратное чтение.
        const auto restored = DeserializeProviderPolicy(SerializeProviderPolicy(p));
        QCOMPARE(restored.perAppBypassList, p.perAppBypassList);
        QCOMPARE(restored.perAppProxyList, p.perAppProxyList);
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

    void gatesAreOpenWithoutPolicy() {
        ClearActiveProviderPolicy();
        QVERIFY(!PolicyHidesSettings());
        QVERIFY(!PolicyHidesUrl());
        QVERIFY(!PolicyBlocksDeletion(7));
        QCOMPARE(ActiveProviderPolicyGroup(), -1);
    }

    void restrictionsApplyOnlyWhenSentTrue() {
        ProviderPolicy p;
        p.hideSettings = false;      // прислано, но false
        // p.hideUrl не прислано вовсе
        SetActiveProviderPolicy(p, 7);
        QVERIFY(!PolicyHidesSettings());
        QVERIFY(!PolicyHidesUrl());

        p.hideSettings = true;
        p.hideUrl = true;
        SetActiveProviderPolicy(p, 7);
        QVERIFY(PolicyHidesSettings());
        QVERIFY(PolicyHidesUrl());
        ClearActiveProviderPolicy();
    }

    void pinBlocksOnlyItsOwnGroup() {
        ProviderPolicy p;
        p.pin = true;
        SetActiveProviderPolicy(p, 7);
        QVERIFY(PolicyBlocksDeletion(7));
        QVERIFY(!PolicyBlocksDeletion(8));   // чужую подписку удалять можно
        QVERIFY(!PolicyBlocksDeletion(-1));
        ClearActiveProviderPolicy();
    }

    void hiddenConfigFollowsSettingsOrUrl() {
        // Ссылка vless:// несёт те же учётные данные, что и URL подписки,
        // поэтому конфигурацию закрывает любой из двух заголовков.
        ProviderPolicy onlySettings;
        onlySettings.hideSettings = true;
        SetActiveProviderPolicy(onlySettings, 7);
        QVERIFY(PolicyHidesConfig(7));
        QVERIFY(!PolicyHidesConfig(8));   // чужая подписка остаётся открытой
        QVERIFY(!PolicyHidesConfig(-1));

        ProviderPolicy onlyUrl;
        onlyUrl.hideUrl = true;
        SetActiveProviderPolicy(onlyUrl, 7);
        QVERIFY(PolicyHidesConfig(7));

        ProviderPolicy neither;
        neither.pin = true;
        SetActiveProviderPolicy(neither, 7);
        QVERIFY(!PolicyHidesConfig(7));

        ClearActiveProviderPolicy();
        QVERIFY(!PolicyHidesConfig(7));
    }

    void stoppingProfileLiftsEveryRestriction() {
        ProviderPolicy p;
        p.hideSettings = true;
        p.hideUrl = true;
        p.pin = true;
        SetActiveProviderPolicy(p, 7);
        QVERIFY(PolicyHidesSettings() && PolicyHidesUrl() && PolicyBlocksDeletion(7));

        ClearActiveProviderPolicy();   // так делает profile_stop
        QVERIFY(!PolicyHidesSettings());
        QVERIFY(!PolicyHidesUrl());
        QVERIFY(!PolicyBlocksDeletion(7));
    }

    void roundTripPreservesEverything() {
        const auto original = ParseProviderPolicy(H({
            {"profile-title", "base64:VmxleFZQTiB8IERFVg=="},
            {"tun-enable", "true"},
            {"hide-settings", "false"},
            {"profile-update-interval", "3"},
            {"subscription-refill-date", "1788566400"},
            {"providerid", "g7PI7IhM"},
            {"x-brand-new-flag", "yes"},
        }));

        const auto restored = DeserializeProviderPolicy(SerializeProviderPolicy(original));

        QCOMPARE(restored.title, original.title);
        QCOMPARE(restored.tunEnable.value(), true);
        QCOMPARE(restored.hideSettings.value(), false);
        QCOMPARE(restored.updateIntervalHours, 3);
        QCOMPARE(restored.refillDate, static_cast<qint64>(1788566400));
        QCOMPARE(restored.providerId, QStringLiteral("g7PI7IhM"));
        QCOMPARE(restored.unknown.value("x-brand-new-flag").toString(), QStringLiteral("yes"));
    }

    void roundTripKeepsAbsentDistinctFromFalse() {
        ProviderPolicy p;
        p.hideSettings = false;   // прислано и false
        // p.tunEnable не задано вовсе
        const auto restored = DeserializeProviderPolicy(SerializeProviderPolicy(p));
        QVERIFY(restored.hideSettings.has_value());
        QCOMPARE(restored.hideSettings.value(), false);
        QVERIFY(!restored.tunEnable.has_value());
    }

    void brokenJsonGivesEmptyPolicy() {
        const auto p = DeserializeProviderPolicy(QStringLiteral("{ not json"));
        QVERIFY(p.isEmpty());
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
