#include <QTest>

#include "include/sys/TunDiagnostics.h"

using namespace Sys::WinTunDiag;

namespace {
    // Все приговоры, кроме Ok. Список руками: добавив новый и забыв про него
    // здесь, видишь падение, а не молчаливо пустое окно у человека.
    const QList<Verdict> kBad = {Verdict::DeviceLeftover, Verdict::DeviceUnloading, Verdict::DeviceDisabled, Verdict::NeedsRestart,
                                 Verdict::ClassConfigBroken, Verdict::DriverMissing, Verdict::DriverDamaged};
}

class TunDiagnosticsTextTest : public QObject {
    Q_OBJECT

private slots:
    // Ради этого всё и написано: окно показывает ровно HeadlineFor, и пустой
    // текст означал бы окно без объяснения — то самое, из-за которого ночь на
    // 2026-11-16 ушла на выяснение того, что Windows знала с первой секунды.
    void everyVerdictSaysSomething() {
        for (const Verdict verdict : kBad) {
            const QString text = HeadlineFor(verdict, QStringLiteral("56"), QStringLiteral("failed, 0x80096010"));
            QVERIFY2(!text.isEmpty(), qPrintable(QString::number(static_cast<int>(verdict))));
            // Подстановки не должны остаться видимыми.
            QVERIFY2(!text.contains(QStringLiteral("%1")), qPrintable(text));
            QVERIFY2(!text.contains(QStringLiteral("%2")), qPrintable(text));
        }
    }

    void nothingToSayWhenNothingIsWrong() {
        QVERIFY(HeadlineFor(Verdict::Ok).isEmpty());
        QVERIFY(!BlocksStart(Verdict::Ok));
    }

    // Запуск отменяем только там, где он заведомо не состоится. Оставшийся
    // адаптер Wintun забирает себе сам, а пропавший файл драйвера ставит из
    // своего ресурса: отменить такой запуск значило бы отнять рабочий случай.
    void onlyHopelessCasesStopTheStart() {
        QVERIFY(BlocksStart(Verdict::ClassConfigBroken));
        QVERIFY(BlocksStart(Verdict::NeedsRestart));
        QVERIFY(BlocksStart(Verdict::DeviceDisabled));
        QVERIFY(BlocksStart(Verdict::DeviceUnloading));
        QVERIFY(!BlocksStart(Verdict::DeviceLeftover));
        QVERIFY(!BlocksStart(Verdict::DriverMissing));
        QVERIFY(!BlocksStart(Verdict::DriverDamaged));
    }

    // Код проблемы попадает в текст: без него строка в журнале не отличает
    // «ещё выгружается» от «отключён», а по коду это видно сразу.
    void theProblemCodeReachesTheText() {
        QVERIFY(HeadlineFor(Verdict::DeviceUnloading, QStringLiteral("38")).contains(QStringLiteral("38")));
        QVERIFY(HeadlineFor(Verdict::DeviceLeftover, QStringLiteral("24")).contains(QStringLiteral("24")));
        QVERIFY(HeadlineFor(Verdict::DriverMissing, QStringLiteral("C:\\Windows\\System32\\drivers\\wintun.sys"))
                    .contains(QStringLiteral("wintun.sys")));
    }

    // Эти строки — текст окна, а не строка журнала: с заглавной и с точкой.
    void everyLineReadsAsASentence() {
        for (const Verdict verdict : kBad) {
            const QString text = HeadlineFor(verdict, QStringLiteral("56"), QStringLiteral("valid"));
            QVERIFY2(text.at(0).isUpper(), qPrintable(text));
            QVERIFY2(text.endsWith('.'), qPrintable(text));
        }
    }

    // Коды, из-за которых всё затевалось, должны называться своими словами, а
    // не падать в «see device manager code N».
    void theCodesFromThatNightAreNamed() {
        QCOMPARE(ProblemText(56), QStringLiteral("the network class configuration never finishes"));
        QCOMPARE(ProblemText(14), QStringLiteral("needs a restart to finish installing"));
        QCOMPARE(ProblemText(0), QStringLiteral("none"));
        QVERIFY(ProblemText(99).contains(QStringLiteral("99")));
    }
};

QTEST_APPLESS_MAIN(TunDiagnosticsTextTest)
#include "TunDiagnosticsTextTest.moc"
