#include <QtTest>

#include "include/global/DnsProbe.hpp"

using Configs::BuildDnsQuery;
using Configs::DnsProbe;
using Configs::DnsProbeResult;
using Configs::IsDnsResponseFor;
using Configs::ParseDnsAnswers;

class DnsProbeTest : public QObject {
    Q_OBJECT

    static QByteArray reply(quint16 id, quint8 flagsHigh, quint8 flagsLow) {
        QByteArray out;
        out.append(static_cast<char>(id >> 8));
        out.append(static_cast<char>(id & 0xff));
        out.append(static_cast<char>(flagsHigh));
        out.append(static_cast<char>(flagsLow));
        out.append(8, '\0');
        return out;
    }

    // Ответ с вопросом и записями: имя в записи сжато указателем на вопрос,
    // как его и пишут настоящие резолверы.
    static QByteArray answerFor(quint16 id, const QList<QByteArray> &records) {
        QByteArray out;
        out.append(static_cast<char>(id >> 8));
        out.append(static_cast<char>(id & 0xff));
        out.append('\x81').append('\x80');          // ответ, рекурсия
        out.append('\0').append('\x01');            // один вопрос
        out.append('\0').append(static_cast<char>(records.size()));
        out.append('\0').append('\0');
        out.append('\0').append('\0');
        out.append(QByteArray("\7example\3com", 12));
        out.append('\0');
        out.append('\0').append('\x01');            // тип A
        out.append('\0').append('\x01');            // класс IN
        for (const auto &record : records) out.append(record);
        return out;
    }

    static QByteArray rrA(quint8 a, quint8 b, quint8 c, quint8 d) {
        QByteArray out;
        out.append('\xc0').append('\x0c');          // указатель на имя из вопроса
        out.append('\0').append('\x01');            // тип A
        out.append('\0').append('\x01');            // класс IN
        out.append(4, '\0');                        // ttl
        out.append('\0').append('\x04');            // длина данных
        out.append(static_cast<char>(a)).append(static_cast<char>(b));
        out.append(static_cast<char>(c)).append(static_cast<char>(d));
        return out;
    }

    static QByteArray rrCname() {
        QByteArray out;
        out.append('\xc0').append('\x0c');
        out.append('\0').append('\x05');            // CNAME
        out.append('\0').append('\x01');
        out.append(4, '\0');
        out.append('\0').append('\x02');            // длина данных
        out.append('\xc0').append('\x0c');
        return out;
    }

private slots:
    void queryHasHeaderAndQuestion() {
        const auto query = BuildDnsQuery("example.com", 0x1234);
        QCOMPARE(static_cast<quint8>(query[0]), quint8(0x12));
        QCOMPARE(static_cast<quint8>(query[1]), quint8(0x34));
        QCOMPARE(static_cast<quint8>(query[5]), quint8(1)); // ровно один вопрос
        QVERIFY(query.contains(QByteArray("\7example\3com", 12)));
        QCOMPARE(static_cast<quint8>(query[query.size() - 1]), quint8(1)); // класс IN
        QCOMPARE(static_cast<quint8>(query[query.size() - 3]), quint8(1)); // тип A
    }

    // Проверять резолвер надо по имени того сервера, к которому подключаемся:
    // отвечающий на постороннее имя вполне может не знать нужного или отдать по
    // нему подменённый адрес, и тогда «рабочим» окажется бесполезный сервер.
    void queryCarriesTheNameItWasGiven() {
        const auto query = BuildDnsQuery("de1.example-vpn.net", 0x4242);
        QVERIFY(query.contains(QByteArray("\3de1\13example-vpn\3net\0", 21)));
        QVERIFY(!query.contains(QByteArray("\7example\3com", 12)));
    }

    void probeNameSurvivesSubdomainsAndHyphens() {
        const auto query = BuildDnsQuery("a-b.c-d.ru", 1);
        QVERIFY(query.contains(QByteArray("\3a-b\3c-d\2ru\0", 12)));
    }

    void acceptsAnswer() {
        QString error;
        QVERIFY(IsDnsResponseFor(reply(0x1234, 0x81, 0x80), 0x1234, &error));
        QVERIFY(error.isEmpty());
    }

    // Пустой ответ и несуществующее имя нас устраивают: сервер жив и отвечает,
    // а записи конкретного имени проба не требует.
    void acceptsNxdomain() {
        QVERIFY(IsDnsResponseFor(reply(0x1234, 0x81, 0x83), 0x1234, nullptr));
    }

    void rejectsBadAnswers() {
        QString error;
        QVERIFY(!IsDnsResponseFor(QByteArray("short"), 0x1234, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!IsDnsResponseFor(reply(0x4321, 0x81, 0x80), 0x1234, nullptr)); // чужой запрос
        QVERIFY(!IsDnsResponseFor(reply(0x1234, 0x01, 0x80), 0x1234, nullptr)); // не ответ
        QVERIFY(!IsDnsResponseFor(reply(0x1234, 0x81, 0x82), 0x1234, nullptr)); // SERVFAIL
        QVERIFY(!IsDnsResponseFor(reply(0x1234, 0x81, 0x85), 0x1234, nullptr)); // REFUSED
    }

    void fastestPicksTheQuickestWorkingOne() {
        QList<DnsProbeResult> results;
        results.append({"tls://77.88.8.8", false, -1, "превышено ожидание"});
        results.append({"8.8.8.8", true, 42, {}});
        results.append({"1.1.1.1", true, 17, {}});
        QCOMPARE(DnsProbe::Fastest(results), QStringLiteral("1.1.1.1"));

        QList<DnsProbeResult> allDead;
        allDead.append({"tls://77.88.8.8", false, -1, "превышено ожидание"});
        QVERIFY(DnsProbe::Fastest(allDead).isEmpty());
    }

    // Проба завершается по таймауту, а следом приходит ошибка сокета. Раньше
    // на этом клиент падал: состояние удалялось дважды. Сигнал должен прийти
    // ровно один раз.
    void unreachableServerFinishesExactlyOnce() {
        DnsProbe probe;
        QSignalSpy spy(&probe, &DnsProbe::Finished);
        // TEST-NET-1 по RFC 5737: не маршрутизируется ниоткуда.
        probe.Start({"tls://192.0.2.1", "https://192.0.2.1/dns-query", "udp://192.0.2.1"}, 300);
        QVERIFY(spy.wait(5000));
        QTest::qWait(1500); // доживаем до запоздалых ошибок сокетов
        QCOMPARE(spy.count(), 1);
        const auto results = spy.first().first().value<QList<DnsProbeResult>>();
        QCOMPARE(results.size(), 3);
        for (const auto &result : results) QVERIFY(!result.ok);
    }

    void badAddressFailsWithoutNetwork() {
        DnsProbe probe;
        QSignalSpy spy(&probe, &DnsProbe::Finished);
        probe.Start({"sdns://nonsense"}, 100);
        QVERIFY(spy.wait(2000));
        const auto results = spy.first().first().value<QList<DnsProbeResult>>();
        QCOMPARE(results.size(), 1);
        QVERIFY(!results.first().ok);
        QVERIFY(!results.first().error.isEmpty());
    }
    // Ответ на пробу — это и есть адрес нашего сервера. Разбирать его отдельно
    // не нужно: имя уже спрошено, и второй раз, под туннелем, спросить может
    // быть негде.
    void answersAreParsed() {
        const auto payload = answerFor(0x1234, {rrA(207, 89, 20, 34), rrA(207, 89, 20, 53)});
        QVERIFY(IsDnsResponseFor(payload, 0x1234, nullptr));
        QCOMPARE(ParseDnsAnswers(payload), QStringList({"207.89.20.34", "207.89.20.53"}));
    }

    // Резолверы охотно присылают CNAME перед адресом. Пропускаем по длине
    // данных, а не на глаз, иначе разбор съедет и выдаст мусор за адрес.
    void recordsOfOtherTypesAreSkipped() {
        const auto payload = answerFor(0x4321, {rrCname(), rrA(1, 2, 3, 4)});
        QCOMPARE(ParseDnsAnswers(payload), QStringList({"1.2.3.4"}));
    }

    // Обрезанный или чужой ответ не должен ни падать, ни выдумывать адреса.
    void brokenAnswersGiveNothing() {
        QVERIFY(ParseDnsAnswers({}).isEmpty());
        QVERIFY(ParseDnsAnswers(QByteArray(8, '\0')).isEmpty());
        auto truncated = answerFor(0x1111, {rrA(8, 8, 8, 8)});
        truncated.chop(3);
        QVERIFY(ParseDnsAnswers(truncated).isEmpty());
        const auto noAnswers = answerFor(0x2222, {});
        QVERIFY(ParseDnsAnswers(noAnswers).isEmpty());
    }
};

QTEST_MAIN(DnsProbeTest)

#include "DnsProbeTest.moc"
