#include <QtTest>

#include "include/engine/services/GeoAssetMirrors.h"

using Engine::GeoAssetMirrors;

class GeoAssetMirrorsTest : public QObject {
    Q_OBJECT

private slots:
    void configuredAddressGoesFirst() {
        const auto url = QStringLiteral("https://github.com/Loyalsoldier/v2ray-rules-dat/raw/release/geoip.dat");
        const auto mirrors = GeoAssetMirrors(url);
        QVERIFY(!mirrors.isEmpty());
        QCOMPARE(mirrors.first(), url);
    }

    void githubBlobFormGetsMirrors() {
        const auto mirrors = GeoAssetMirrors(
            QStringLiteral("https://github.com/Loyalsoldier/v2ray-rules-dat/raw/release/geosite.dat"));
        QCOMPARE(mirrors.size(), 3);
        QCOMPARE(mirrors.at(1),
                 QStringLiteral("https://cdn.jsdelivr.net/gh/Loyalsoldier/v2ray-rules-dat@release/geosite.dat"));
        QCOMPARE(mirrors.at(2),
                 QStringLiteral("https://raw.githubusercontent.com/Loyalsoldier/v2ray-rules-dat/release/geosite.dat"));
    }

    void rawFormGetsMirrorsAndNoDuplicate() {
        const auto url =
            QStringLiteral("https://raw.githubusercontent.com/Loyalsoldier/v2ray-rules-dat/release/geoip.dat");
        const auto mirrors = GeoAssetMirrors(url);
        // Сам адрес и jsDelivr: второе зеркало совпало бы с исходным.
        QCOMPARE(mirrors.size(), 2);
        QCOMPARE(mirrors.first(), url);
        QCOMPARE(mirrors.at(1),
                 QStringLiteral("https://cdn.jsdelivr.net/gh/Loyalsoldier/v2ray-rules-dat@release/geoip.dat"));
    }

    // Набор правил менять нельзя: категории у разных репозиториев свои, и подмена
    // источника тихо ломает маршрутизацию. Зеркало обязано вести в тот же репозиторий.
    void mirrorsKeepOwnerRepoAndRef() {
        const auto mirrors = GeoAssetMirrors(
            QStringLiteral("https://github.com/runetfreedom/russia-v2ray-rules-dat/raw/main/geosite.dat"));
        for (const auto &mirror : mirrors) {
            QVERIFY2(mirror.contains(QStringLiteral("runetfreedom")), qPrintable(mirror));
            QVERIFY2(mirror.contains(QStringLiteral("russia-v2ray-rules-dat")), qPrintable(mirror));
            QVERIFY2(mirror.endsWith(QStringLiteral("geosite.dat")), qPrintable(mirror));
        }
        QVERIFY(mirrors.at(1).contains(QStringLiteral("@main/")));
    }

    void nestedPathSurvives() {
        const auto mirrors =
            GeoAssetMirrors(QStringLiteral("https://github.com/owner/repo/raw/v1.2/dist/data/geoip.dat"));
        QCOMPARE(mirrors.at(1), QStringLiteral("https://cdn.jsdelivr.net/gh/owner/repo@v1.2/dist/data/geoip.dat"));
    }

    void foreignHostIsLeftAlone() {
        const auto url = QStringLiteral("https://example.com/files/geoip.dat");
        const auto mirrors = GeoAssetMirrors(url);
        QCOMPARE(mirrors, QStringList{url});
    }

    void plainHttpIsNotMirrored() {
        // Зеркалить без TLS нечего: файл правил подменяется по дороге.
        const auto url = QStringLiteral("http://github.com/owner/repo/raw/release/geoip.dat");
        QCOMPARE(GeoAssetMirrors(url), QStringList{url});
    }

    void emptyGivesNothing() {
        QVERIFY(GeoAssetMirrors({}).isEmpty());
        QVERIFY(GeoAssetMirrors(QStringLiteral("   ")).isEmpty());
    }

    void malformedGithubUrlIsLeftAlone() {
        const auto url = QStringLiteral("https://github.com/owner/repo");
        QCOMPARE(GeoAssetMirrors(url), QStringList{url});
    }
};

QTEST_MAIN(GeoAssetMirrorsTest)
#include "GeoAssetMirrorsTest.moc"
