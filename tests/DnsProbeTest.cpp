#include <QtTest>

#include "include/global/DnsProbe.hpp"

using Configs::BuildDnsQuery;
using Configs::DnsProbe;
using Configs::DnsProbeResult;
using Configs::IsDnsResponseFor;

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
};

QTEST_MAIN(DnsProbeTest)

#include "DnsProbeTest.moc"
