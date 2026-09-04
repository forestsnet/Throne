#include <QTest>

#include "include/ui/fsnt/UiMode.hpp"

using namespace Fsnt;

class UiModeTest : public QObject {
    Q_OBJECT
private slots:
    void freshInstallGetsSimple() {
        QCOMPARE(ResolveInitialUiMode(-1), UiMode::Simple);
    }

    void existingInstallAlsoGetsSimple() {
        // Переустановка сохраняет каталог с конфигом. Раньше здесь смотрели,
        // есть ли в базе профили, и пользователь снова попадал в расширенный
        // режим; теперь наличие данных на выбор не влияет.
        QCOMPARE(ResolveInitialUiMode(-1), UiMode::Simple);
    }

    void storedChoiceWins() {
        QCOMPARE(ResolveInitialUiMode(0), UiMode::Advanced);
        QCOMPARE(ResolveInitialUiMode(1), UiMode::Simple);
    }

    void garbageValueIsTreatedAsUnset() {
        // Значение вне диапазона трактуем как незаданное, а не как режим 0.
        QCOMPARE(ResolveInitialUiMode(42), UiMode::Simple);
        QCOMPARE(ResolveInitialUiMode(-7), UiMode::Simple);
    }
};

QTEST_APPLESS_MAIN(UiModeTest)
#include "UiModeTest.moc"
