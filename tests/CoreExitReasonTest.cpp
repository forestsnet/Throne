#include <QTest>

#include "include/engine/supervisor/CoreExitReason.h"

using namespace Engine;

class CoreExitReasonTest : public QObject {
    Q_OBJECT

private slots:
    // Ради этого всё и написано: ядро, которое не удалось запустить заново,
    // делает обрыв неустранимым, и окно обязано это сказать. На стенде причина
    // была только в логе — «Process failed to start: The system cannot find the
    // file specified», — а в окне стояло одно «Связь оборвалась».
    void aCoreThatWillNotStartNamesItself() {
        QCOMPARE(CoreExitReasonText(true, QStringLiteral("Process failed to start: The system cannot find the file specified")),
                 QStringLiteral("the core could not be started"));
        // Причина берётся из самого выхода, а не из строки: она бывает пустой.
        QCOMPARE(CoreExitReasonText(true, QString()), QStringLiteral("the core could not be started"));
    }

    void aPanicIsNamedToo() {
        QCOMPARE(CoreExitReasonText(false, QStringLiteral("panic: runtime error: invalid memory address")),
                 QStringLiteral("the core stopped with an error"));
        QCOMPARE(CoreExitReasonText(false, QStringLiteral("Core panicked: bad config")),
                 QStringLiteral("the core stopped with an error"));
        QCOMPARE(CoreExitReasonText(false, QStringLiteral("fatal error: all goroutines are asleep - deadlock!")),
                 QStringLiteral("the core stopped with an error"));
    }

    // Снятый провайдером сеанс, смена сети, убитое снаружи ядро — супервизор их
    // не различает. Пустая причина оставляет окну прежнее «Связь оборвалась»:
    // придуманная причина хуже отсутствия причины.
    void anOrdinaryExitInventsNothing() {
        QVERIFY(CoreExitReasonText(false, QString()).isEmpty());
        QVERIFY(CoreExitReasonText(false, QStringLiteral("inbound/tun: closed")).isEmpty());
        QVERIFY(CoreExitReasonText(false, QStringLiteral("did not connect within 15 s")).isEmpty());
    }

    // Строка ядра не подпадает под panic только потому, что слово встретилось
    // где-то внутри: признак — начало строки.
    void onlyTheStartOfTheLineCounts() {
        QVERIFY(!CoreExitLooksLikePanic(QStringLiteral("recovered from panic: nothing to see")));
        QVERIFY(!CoreExitLooksLikePanic(QStringLiteral(" panic: indented")));
        QVERIFY(CoreExitLooksLikePanic(QStringLiteral("panic: at the start")));
    }

    // Текст подставляется после двоеточия («Связь оборвалась: %1»), поэтому он
    // со строчной буквы и без точки на конце.
    void everyReasonFitsAfterAColon() {
        for (const QString &reason : {CoreExitReasonText(true, QString()),
                                      CoreExitReasonText(false, QStringLiteral("panic: x"))}) {
            QVERIFY2(!reason.isEmpty(), qPrintable(reason));
            QVERIFY2(reason.at(0).isLower(), qPrintable(reason));
            QVERIFY2(!reason.endsWith('.'), qPrintable(reason));
        }
    }
};

QTEST_APPLESS_MAIN(CoreExitReasonTest)
#include "CoreExitReasonTest.moc"
