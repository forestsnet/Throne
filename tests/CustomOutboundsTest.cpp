#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTest>

#include "include/configs/CustomOutbounds.hpp"

using namespace Configs;

class CustomOutboundsTest : public QObject {
    Q_OBJECT

private:
    // Одинарные кавычки вместо двойных: JSON с экранированием нечитаем, а сырые
    // строковые литералы здесь использовать нельзя — moc на них спотыкается и
    // молча отдаёт пустой .moc.
    static QJsonObject parse(const char *json) {
        return QJsonDocument::fromJson(
                   QString::fromUtf8(json).replace(QLatin1Char('\''), QLatin1Char('"')).toUtf8())
            .object();
    }

private slots:
    // Профиль-балансировщик провайдера — это один конфиг с узлом на страну, и
    // разрешать до подъёма туннеля надо каждое имя, а не первое.
    void everyNodeOfABalancerIsListed() {
        const auto names = XrayConfigServerNames(parse(
            "{'outbounds':["
            "{'protocol':'vless','tag':'nl','settings':{'vnext':[{'address':'relay-nl.api.fx-srv.org','port':443}]}},"
            "{'protocol':'vless','tag':'de','settings':{'vnext':[{'address':'xh-de.srv.fx-srv.org','port':10443}]}},"
            "{'protocol':'trojan','tag':'fi','settings':{'servers':[{'address':'fsnt-1-fi.com','port':9999}]}},"
            "{'protocol':'freedom','tag':'direct'}"
            "]}"));
        QCOMPARE(names, QStringList({QStringLiteral("relay-nl.api.fx-srv.org"), QStringLiteral("xh-de.srv.fx-srv.org"),
                                     QStringLiteral("fsnt-1-fi.com")}));
    }

    void infrastructureOutboundsDialNothing() {
        for (const char *protocol : {"freedom", "direct", "blackhole", "block", "dns", "loopback"}) {
            const auto config = QStringLiteral("{'outbounds':[{'protocol':'%1','settings':{'address':'no.example'}}]}")
                                    .arg(QString::fromUtf8(protocol));
            QVERIFY2(XrayConfigServerNames(parse(config.toUtf8().constData())).isEmpty(), protocol);
        }
    }

    void repeatedNamesCollapse() {
        const auto names = XrayConfigServerNames(parse(
            "{'outbounds':["
            "{'protocol':'vless','settings':{'vnext':[{'address':'pl.srv.fx-srv.org'}]}},"
            "{'protocol':'vless','settings':{'vnext':[{'address':'PL.SRV.FX-SRV.ORG'}]}}"
            "]}"));
        QCOMPARE(names, QStringList({QStringLiteral("pl.srv.fx-srv.org")}));
    }

    // Адреса остаются в списке: это список того, куда звонит конфиг, а отбирать
    // из него имена — дело вызывающего (ConnectionModes разрешает только имена).
    void addressesAreListedToo() {
        const auto names =
            XrayConfigServerNames(parse("{'outbounds':[{'protocol':'vless','settings':{'vnext':[{'address':'203.0.113.7'}]}}]}"));
        QCOMPARE(names, QStringList({QStringLiteral("203.0.113.7")}));
    }

    // Xray хранит одно написание ключа, и какое — неизвестно. Спорящие
    // написания означают не значение, а неизвестность, и выдумывать за них
    // адрес нельзя: закрепишь не то, куда пойдёт соединение.
    void disagreeingSpellingsLeaveNoAddress() {
        QVERIFY(XrayConfigServerNames(parse("{'outbounds':[{'protocol':'vless','Protocol':'trojan',"
                                            "'settings':{'vnext':[{'address':'a.example'}]}}]}"))
                    .isEmpty());
        QVERIFY(XrayConfigServerNames(parse("{'outbounds':[{'protocol':'vless',"
                                            "'settings':{'vnext':[{'address':'a.example','Address':'b.example'}]}}]}"))
                    .isEmpty());
    }

    void keySpellingDoesNotMatter() {
        const auto names = XrayConfigServerNames(
            parse("{'Outbounds':[{'PROTOCOL':'VLess','SeTTings':{'VNext':[{'ADDRESS':'mixed.example'}]}}]}"));
        QCOMPARE(names, QStringList({QStringLiteral("mixed.example")}));
    }

    // vnext читают только VMess и VLESS. Остальные протоколы читают servers и
    // vnext не видят вовсе, так что брать адрес оттуда значило бы назвать тот,
    // к которому ядро не пойдёт.
    void vnextIsOnlyForVmessAndVless() {
        QVERIFY(XrayConfigServerNames(parse("{'outbounds':[{'protocol':'trojan','settings':{'vnext':[{'address':'a.example'}]}}]}"))
                    .isEmpty());
        QVERIFY(XrayConfigServerNames(parse("{'outbounds':[{'protocol':'vless','settings':{'servers':[{'address':'a.example'}]}}]}"))
                    .isEmpty());
        QCOMPARE(XrayConfigServerNames(parse("{'outbounds':[{'protocol':'vmess','settings':{'vnext':[{'address':'v.example'}]}}]}")),
                 QStringList({QStringLiteral("v.example")}));
    }

    void inlineAddressReplacesTheList() {
        const auto names = XrayConfigServerNames(parse("{'outbounds':[{'protocol':'vless','settings':"
                                                       "{'address':'inline.example','vnext':[{'address':'ignored.example'}]}}]}"));
        QCOMPARE(names, QStringList({QStringLiteral("inline.example")}));
    }

    // selector и urltest — это группы, своего адреса у них нет, а участники
    // лежат в том же списке и просматриваются сами.
    void singBoxGroupsHaveNoAddressOfTheirOwn() {
        const auto names = SingBoxConfigServerNames(parse(
            "{'outbounds':["
            "{'type':'selector','tag':'auto','outbounds':['a','b']},"
            "{'type':'urltest','tag':'fast','outbounds':['a','b']},"
            "{'type':'vless','tag':'a','server':'a.srv.example'},"
            "{'type':'shadowsocks','tag':'b','server':'b.srv.example'}"
            "]}"));
        QCOMPARE(names, QStringList({QStringLiteral("a.srv.example"), QStringLiteral("b.srv.example")}));
    }

    void singBoxInfrastructureIsSkipped() {
        const auto names = SingBoxConfigServerNames(parse("{'outbounds':["
                                                          "{'type':'direct','server':'no.example'},"
                                                          "{'type':'block'},{'type':'dns'},"
                                                          "{'type':'vless','server':'yes.example'}"
                                                          "]}"));
        QCOMPARE(names, QStringList({QStringLiteral("yes.example")}));
    }

    void nothingUsableGivesNothing() {
        for (const char *config : {"{}", "{'outbounds':[]}", "{'outbounds':'nonsense'}", "{'outbounds':[{}]}",
                                   "{'outbounds':[{'protocol':'vless'}]}"}) {
            QVERIFY2(XrayConfigServerNames(parse(config)).isEmpty(), config);
            QVERIFY2(SingBoxConfigServerNames(parse(config)).isEmpty(), config);
        }
    }

    // На этом стоит адрес профиля и его SNI: Xray маршрутизирует по умолчанию в
    // первый исходящий, который куда-то звонит.
    void theFirstEgressSkipsInfrastructure() {
        const auto root = parse("{'outbounds':["
                                "{'protocol':'freedom','tag':'direct'},"
                                "{'protocol':'blackhole','tag':'block'},"
                                "{'protocol':'vless','tag':'proxy','settings':{'vnext':[{'address':'first.example'}]}}"
                                "]}");
        const auto egress = firstXrayEgress(root["outbounds"].toArray());
        QCOMPARE(egress["tag"].toString(), QStringLiteral("proxy"));
        QCOMPARE(xrayOutboundAddress(egress), QStringLiteral("first.example"));
    }
};

QTEST_APPLESS_MAIN(CustomOutboundsTest)
#include "CustomOutboundsTest.moc"
