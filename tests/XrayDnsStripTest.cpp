#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTest>

#include "include/configs/XrayDnsStrip.hpp"

using namespace Configs;

class XrayDnsStripTest : public QObject {
    Q_OBJECT

private:
    // Сырые строковые литералы здесь использовать нельзя: moc на них
    // спотыкается и молча выдаёт пустой .moc, а тест потом не собирается.
    static QJsonObject parse(const char *json) {
        return QJsonDocument::fromJson(QByteArray(json)).object();
    }

    static QStringList field(const QJsonArray &items, const QString &key) {
        QStringList out;
        for (const auto &value : items) out << value.toObject()[key].toString();
        return out;
    }

    static QJsonArray rulesOf(const QJsonObject &config) {
        return config["routing"].toObject()["rules"].toArray();
    }

    // Конфиг по форме как у реальной панели: DNS в нём — конвейер из блока dns,
    // исходящего protocol "dns" и правила, гонящего в него порт 53.
    static QJsonObject providerConfig() {
        return parse(
            "{"
            "  \"dns\": { \"servers\": [ {\"tag\": \"cloudflare-dns\","
            "                            \"address\": \"https://cloudflare-dns.com/dns-query\"} ] },"
            "  \"outbounds\": ["
            "    {\"tag\": \"proxy\",   \"protocol\": \"vless\"},"
            "    {\"tag\": \"direct\",  \"protocol\": \"freedom\"},"
            "    {\"tag\": \"block\",   \"protocol\": \"blackhole\"},"
            "    {\"tag\": \"dns-out\", \"protocol\": \"dns\"}"
            "  ],"
            "  \"routing\": { \"rules\": ["
            "    {\"ruleTag\": \"dns-to-proxy\", \"outboundTag\": \"proxy\"},"
            "    {\"ruleTag\": \"dns-hijack\",   \"port\": \"53\", \"outboundTag\": \"dns-out\"},"
            "    {\"ruleTag\": \"block-ads\",    \"outboundTag\": \"block\"}"
            "  ] }"
            "}");
    }

private slots:
    void removesWholePipelineNotJustServers() {
        auto config = providerConfig();
        const auto result = StripXrayDns(config);

        QVERIFY(!config.contains("dns"));
        QCOMPARE(result.outbounds, 1);
        QCOMPARE(result.rules, 1);

        // Исходящего dns-out больше нет, остальные на месте и в том же порядке.
        QCOMPARE(field(config["outbounds"].toArray(), "tag"),
                 (QStringList{"proxy", "direct", "block"}));

        // Правило, целившееся в удалённый исходящий, ушло; чужие остались.
        // Именно из-за него Xray отвечал «failed to resolve ip > dns router
        // closed»: запрос уходил в модуль, которого уже нет.
        QCOMPARE(field(rulesOf(config), "ruleTag"),
                 (QStringList{"dns-to-proxy", "block-ads"}));
    }

    void configWithoutDnsOutboundIsLeftAlone() {
        auto config = parse(
            "{"
            "  \"dns\": { \"servers\": [\"8.8.8.8\"] },"
            "  \"outbounds\": [ {\"tag\": \"proxy\", \"protocol\": \"vless\"} ],"
            "  \"routing\": { \"rules\": [ {\"ruleTag\": \"all\", \"outboundTag\": \"proxy\"} ] }"
            "}");

        const auto result = StripXrayDns(config);

        QVERIFY(!config.contains("dns"));
        QCOMPARE(result.outbounds, 0);
        QCOMPARE(result.rules, 0);
        // Ни один исходящий и ни одно правило не пострадали.
        QCOMPARE(config["outbounds"].toArray().size(), 1);
        QCOMPARE(rulesOf(config).size(), 1);
    }

    void severalDnsOutboundsAllGo() {
        auto config = parse(
            "{"
            "  \"outbounds\": ["
            "    {\"tag\": \"dns-a\", \"protocol\": \"dns\"},"
            "    {\"tag\": \"dns-b\", \"protocol\": \"dns\"},"
            "    {\"tag\": \"proxy\", \"protocol\": \"vless\"}"
            "  ],"
            "  \"routing\": { \"rules\": ["
            "    {\"ruleTag\": \"a\", \"outboundTag\": \"dns-a\"},"
            "    {\"ruleTag\": \"b\", \"outboundTag\": \"dns-b\"},"
            "    {\"ruleTag\": \"c\", \"outboundTag\": \"proxy\"}"
            "  ] }"
            "}");

        const auto result = StripXrayDns(config);

        QCOMPARE(result.outbounds, 2);
        QCOMPARE(result.rules, 2);
        QCOMPARE(field(config["outbounds"].toArray(), "tag"), (QStringList{"proxy"}));
        QCOMPARE(field(rulesOf(config), "ruleTag"), (QStringList{"c"}));
    }

    void emptyConfigDoesNotCrash() {
        QJsonObject config;
        const auto result = StripXrayDns(config);
        QCOMPARE(result.outbounds, 0);
        QCOMPARE(result.rules, 0);
    }
};

QTEST_APPLESS_MAIN(XrayDnsStripTest)
#include "XrayDnsStripTest.moc"
