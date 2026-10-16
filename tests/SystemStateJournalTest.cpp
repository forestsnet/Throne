#include <QtTest>
#include <QTemporaryDir>

#include "include/global/SystemStateJournal.hpp"

using Configs::SystemChange;
using Configs::SystemStateJournal;

class SystemStateJournalTest : public QObject {
    Q_OBJECT

private slots:
    // Главный случай: сеанс записал изменение и не дожил до отката. На
    // следующем запуске оно должно найтись.
    void leftoversSurviveRestart() {
        QTemporaryDir dir;
        SystemStateJournal::Init(dir.path());
        SystemStateJournal::Record("system-proxy", "127.0.0.1:2080");
        SystemStateJournal::Record("system-dns", "172.19.0.1");

        // Перезапуск: журнал перечитывается с диска.
        SystemStateJournal::Init(dir.path());
        const auto left = SystemStateJournal::Leftovers();
        QCOMPARE(left.size(), 2);
        QCOMPARE(left.first().kind, QStringLiteral("system-proxy"));
        QCOMPARE(left.first().detail, QStringLiteral("127.0.0.1:2080"));
    }

    void forgettingRemovesOnlyItsOwn() {
        QTemporaryDir dir;
        SystemStateJournal::Init(dir.path());
        SystemStateJournal::Record("system-proxy", "127.0.0.1:2080");
        SystemStateJournal::Record("system-dns", "172.19.0.1");
        SystemStateJournal::Forget("system-proxy");

        SystemStateJournal::Init(dir.path());
        const auto left = SystemStateJournal::Leftovers();
        QCOMPARE(left.size(), 1);
        QCOMPARE(left.first().kind, QStringLiteral("system-dns"));
    }

    void cleanSessionLeavesNothing() {
        QTemporaryDir dir;
        SystemStateJournal::Init(dir.path());
        SystemStateJournal::Record("system-proxy", "127.0.0.1:2080");
        SystemStateJournal::Forget("system-proxy");

        SystemStateJournal::Init(dir.path());
        QVERIFY(SystemStateJournal::Leftovers().isEmpty());
    }

    // Повторная запись того же вида обновляет подробности, а не плодит строки.
    void recordingTwiceUpdates() {
        QTemporaryDir dir;
        SystemStateJournal::Init(dir.path());
        SystemStateJournal::Record("system-dns", "старое");
        SystemStateJournal::Record("system-dns", "новое");

        SystemStateJournal::Init(dir.path());
        const auto left = SystemStateJournal::Leftovers();
        QCOMPARE(left.size(), 1);
        QCOMPARE(left.first().detail, QStringLiteral("новое"));
    }

    // Битый журнал не должен мешать запуску: лучше ничего не откатить, чем
    // не стартовать.
    void brokenFileIsIgnored() {
        QTemporaryDir dir;
        SystemStateJournal::Init(dir.path());
        QFile file(SystemStateJournal::Path());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("не json вовсе");
        file.close();

        SystemStateJournal::Init(dir.path());
        QVERIFY(SystemStateJournal::Leftovers().isEmpty());
    }
};

QTEST_GUILESS_MAIN(SystemStateJournalTest)

#include "SystemStateJournalTest.moc"
