#include <QComboBox>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTest>
#include <QVBoxLayout>

#include "include/ui/app/WheelGuard.h"

// Страница с полями — та же форма, что у любого диалога расширенного режима:
// содержимое выше окна, поля на виду, под ними ещё текст. Поле обязано быть
// видно, иначе колесо приходит мимо окна и проверять нечего.
class WheelGuardTest : public QObject {
    Q_OBJECT

private:
    QScrollArea *scroll = nullptr;
    QComboBox *combo = nullptr;
    QSpinBox *spin = nullptr;
    QWidget *host = nullptr;

    // Колесо должно приходить так, как его приносит оконная система. Посланное
    // руками QApplication родителю не передаёт, и тест на своих событиях
    // проверял бы сам себя, а не фильтр.
    void wheelOver(const QWidget *field, const int angle) {
        const QPoint centre(field->width() / 2, field->height() / 2);
        QTest::wheelEvent(scroll->windowHandle(), field->mapTo(scroll, centre),
                          QPoint(0, angle), QPoint(0, angle / 3));
    }

private slots:
    void initTestCase() {
        WheelGuard::InstallOn(*qApp);
    }

    void init() {
        scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->resize(360, 200);

        host = new QWidget(scroll);
        auto *column = new QVBoxLayout(host);

        auto *above = new QLabel("above", host);
        above->setFixedHeight(40);
        column->addWidget(above);

        combo = new QComboBox(host);
        combo->addItems({"one", "two", "three"});
        combo->setCurrentIndex(1);
        column->addWidget(combo);

        spin = new QSpinBox(host);
        spin->setRange(0, 100);
        spin->setValue(50);
        column->addWidget(spin);

        auto *below = new QLabel("below", host);
        below->setFixedHeight(600);
        column->addWidget(below);

        scroll->setWidget(host);
        scroll->show();
        QVERIFY(QTest::qWaitForWindowExposed(scroll));
        QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QCOMPARE(scroll->verticalScrollBar()->value(), 0);
    }

    void cleanup() {
        delete scroll;
        scroll = nullptr;
    }

    void wheelOverAComboScrollsThePage() {
        const int before = combo->currentIndex();
        wheelOver(combo, -120);
        QCOMPARE(combo->currentIndex(), before);
        QVERIFY(scroll->verticalScrollBar()->value() > 0);
    }

    void wheelOverASpinBoxScrollsThePage() {
        const int before = spin->value();
        wheelOver(spin, -120);
        QCOMPARE(spin->value(), before);
        QVERIFY(scroll->verticalScrollBar()->value() > 0);
    }

    void theWheelStillScrollsBothWays() {
        wheelOver(combo, -120);
        const int down = scroll->verticalScrollBar()->value();
        QVERIFY(down > 0);
        wheelOver(combo, 120);
        QVERIFY(scroll->verticalScrollBar()->value() < down);
        QCOMPARE(combo->currentIndex(), 1);
    }

    void aTrackpadGestureScrollsThePage() {
        // Трекпад шлёт не щелчки, а последовательность с фазами, и Qt на
        // ScrollBegin закрепляет её за виджетом под курсором — за полем.
        // ScrollEnd нужен, чтобы снять это закрепление: оно живёт в статике
        // QApplication и иначе достанется следующему тесту. Угол там у
        // настоящего трекпада нулевой, но QTest на пустое событие ворчит.
        const QPoint centre(combo->width() / 2, combo->height() / 2);
        const QPointF pos = combo->mapTo(scroll, centre);
        QWindow *window = scroll->windowHandle();
        QTest::wheelEvent(window, pos, QPoint(0, -120), QPoint(0, -40), Qt::NoModifier, Qt::ScrollBegin);
        QTest::wheelEvent(window, pos, QPoint(0, -120), QPoint(0, -40), Qt::NoModifier, Qt::ScrollUpdate);
        QTest::wheelEvent(window, pos, QPoint(0, -1), QPoint(0, 0), Qt::NoModifier, Qt::ScrollEnd);

        QCOMPARE(combo->currentIndex(), 1);
        QVERIFY(scroll->verticalScrollBar()->value() > 0);
    }

    void aFieldLosesWheelFocusWhenItIsPolished() {
        // Фокус раздаёт сам QApplication, до доставки события, поэтому фильтром
        // его не удержать — только политикой поля. Qt::WheelFocus поля получают
        // на Windows и Linux, macOS даёт им Qt::TabFocus и этой беды не знала,
        // так что политику ставим руками. Polish приходит один раз за жизнь
        // виджета, значит поле должно быть новым.
        auto *fresh = new QComboBox(host);
        fresh->addItems({"one", "two"});
        fresh->setFocusPolicy(Qt::WheelFocus);
        fresh->ensurePolished();
        QCOMPARE(fresh->focusPolicy(), Qt::StrongFocus);

        auto *label = new QLabel("not a field", host);
        label->setFocusPolicy(Qt::WheelFocus);
        label->ensurePolished();
        QCOMPARE(label->focusPolicy(), Qt::WheelFocus);
    }

    void theWheelDoesNotTakeTheKeyboardIntoTheField() {
        combo->clearFocus();
        spin->setFocus();
        wheelOver(combo, -120);
        QVERIFY(!combo->hasFocus());
        QVERIFY(scroll->verticalScrollBar()->value() > 0);
    }

    void theScrollBarKeepsItsWheel() {
        QScrollBar *bar = scroll->verticalScrollBar();
        if (!bar->isVisible()) QSKIP("полоса прокрутки на этой платформе не показана");
        const QPoint centre(bar->width() / 2, bar->height() / 2);
        QTest::wheelEvent(scroll->windowHandle(), bar->mapTo(scroll, centre), QPoint(0, -120), QPoint(0, -40));
        QVERIFY(bar->value() > 0);
    }
};

QTEST_MAIN(WheelGuardTest)
#include "WheelGuardTest.moc"
