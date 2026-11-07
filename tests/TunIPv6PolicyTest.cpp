#include <QJsonArray>
#include <QTest>

#include "include/configs/TunIPv6Policy.hpp"

class TunIPv6PolicyTest : public QObject {
    Q_OBJECT

private slots:
    void refusesBeforeSniffAndUserRoutes_data() {
        QTest::addColumn<QString>("action");
        QTest::newRow("generated-sniff") << QString("sniff");
        QTest::newRow("raw-direct") << QString("route");
    }

    void refusesBeforeSniffAndUserRoutes() {
        QFETCH(QString, action);
        const QJsonObject original{{"action", action}, {"outbound", "direct"}};
        QJsonObject config{{"route", QJsonObject{{"rules", QJsonArray{original}}, {"final", "proxy"}}}};
        Configs::ApplyTunIPv6Policy(config, "tun-in", "dns-system");
        const auto route = config["route"].toObject();
        const auto rules = route["rules"].toArray();
        QCOMPARE(rules.size(), 3);
        const auto dns = rules[0].toObject();
        QCOMPARE(dns["inbound"].toString(), QString("tun-in"));
        QCOMPARE(dns["ip_version"].toInt(), 6);
        QCOMPARE(dns["port"].toInt(), 53);
        QCOMPARE(dns["action"].toString(), QString("hijack-dns"));
        const auto reject = rules[1].toObject();
        QCOMPARE(reject["inbound"].toString(), QString("tun-in"));
        QCOMPARE(reject["ip_version"].toInt(), 6);
        QCOMPARE(reject["action"].toString(), QString("reject"));
        QVERIFY(reject["no_drop"].toBool());
        QCOMPARE(rules[2].toObject(), original);
        QCOMPARE(route["final"].toString(), QString("proxy"));
    }

    void suppressesAAAAAheadOfCustomDnsAndKeepsServers() {
        const QJsonArray servers{QJsonObject{{"tag", "custom"}, {"type", "hosts"}}};
        const QJsonObject original{{"action", "route"}, {"server", "custom"}};
        QJsonObject config{{"dns", QJsonObject{{"servers", servers}, {"rules", QJsonArray{original}}}}};
        Configs::ApplyTunIPv6Policy(config, "tun-in", "dns-system");
        const auto dns = config["dns"].toObject();
        QCOMPARE(dns["servers"].toArray(), servers);
        const auto rules = dns["rules"].toArray();
        QCOMPARE(rules.size(), 2);
        const auto guard = rules[0].toObject();
        QCOMPARE(guard["inbound"].toArray(), (QJsonArray{"tun-in", "dns-system"}));
        QCOMPARE(guard["query_type"].toString(), QString("AAAA"));
        QCOMPARE(guard["action"].toString(), QString("predefined"));
        QCOMPARE(guard["rcode"].toString(), QString("NOERROR"));
        QVERIFY(guard["answer"].toArray().isEmpty());
        QCOMPARE(rules[1].toObject(), original);
    }
};

QTEST_GUILESS_MAIN(TunIPv6PolicyTest)
#include "TunIPv6PolicyTest.moc"
