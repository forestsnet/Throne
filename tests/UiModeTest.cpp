#include <QTest>

#include "include/ui/fsnt/UiMode.hpp"

using namespace Fsnt;

class UiModeTest : public QObject {
    Q_OBJECT
private slots:
    void freshInstallGetsSimple() {
        QCOMPARE(ResolveInitialUiMode(-1, false), UiMode::Simple);
    }

    void existingInstallStaysAdvanced() {
        QCOMPARE(ResolveInitialUiMode(-1, true), UiMode::Advanced);
    }

    void storedChoiceWins() {
        QCOMPARE(ResolveInitialUiMode(0, false), UiMode::Advanced);
        QCOMPARE(ResolveInitialUiMode(1, true), UiMode::Simple);
    }

    void garbageValueFallsBackToStoredRules() {
        // Значение вне диапазона трактуем как незаданное, а не как режим 0.
        QCOMPARE(ResolveInitialUiMode(42, false), UiMode::Simple);
        QCOMPARE(ResolveInitialUiMode(42, true), UiMode::Advanced);
        QCOMPARE(ResolveInitialUiMode(-7, false), UiMode::Simple);
    }
};

QTEST_APPLESS_MAIN(UiModeTest)
#include "UiModeTest.moc"
