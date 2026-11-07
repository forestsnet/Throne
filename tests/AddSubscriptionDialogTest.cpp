#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/ui/fsnt/AddSubscriptionDialog.h"

// Exercise the real dialog and queued callback. The updater substitutes only
// the background job boundary, delivering the outcome a failed import emits.
class AddSubscriptionDialogTest : public QObject {
    Q_OBJECT
private slots:
    void fetchFailure_data() {
        QTest::addColumn<QString>("error");
        QTest::addColumn<QStringList>("reportReasons");
        QTest::addColumn<QString>("expected");
        const auto guard = QStringLiteral("Request with proxy but no profile started.");
        QTest::newRow("local proxy guard") << guard << QStringList() << guard;
        const auto http = QStringLiteral("Could not download the subscription: the server refused access (HTTP 403)");
        QTest::newRow("server failure") << http << QStringList() << http;
        const QStringList translated = {QStringLiteral("Не удалось загрузить подписку: сервер отказал в доступе (HTTP 403)"),
                                        QStringLiteral("Повторите обновление.")};
        QTest::newRow("localized report wins") << http << translated << translated.join('\n');
        QTest::newRow("missing reason") << QString() << QStringList() << QStringLiteral("Could not open the subscription. Check the link and your connection.");
    }

    void fetchFailure() {
        QFETCH(QString, error);
        QFETCH(QStringList, reportReasons);
        QFETCH(QString, expected);
        AddSubscriptionDialog dialog;
        auto *input = dialog.findChild<QLineEdit *>("fsntInput");
        auto *add = dialog.findChild<QPushButton *>("fsntPrimary");
        QVERIFY(input);
        QVERIFY(add);
        input->setText("https://subscription.invalid/test");
        add->click();
        QVERIFY(!add->isEnabled());
        auto *updater = Subscription::updater();
        QCOMPARE(updater->requestedUrl, input->text());
        QVERIFY(updater->pending);
        updater->pending({Subscription::UpdateOutcome::Failure::Fetch, error, {reportReasons}});
        QTRY_VERIFY(add->isEnabled());
        bool found = false;
        for (auto *label : dialog.findChildren<QLabel *>()) {
            found |= label->text() == expected;
            QVERIFY(!label->text().contains("Unrelated VPN"));
            QVERIFY(!label->text().contains("antivirus"));
        }
        QVERIFY2(found, qPrintable(expected));
        QCOMPARE(dialog.result(), 0);
    }

    void successfulImportStillAcceptsDialog() {
        AddSubscriptionDialog dialog;
        dialog.findChild<QLineEdit *>("fsntInput")->setText("https://subscription.invalid/test");
        dialog.findChild<QPushButton *>("fsntPrimary")->click();
        Subscription::updater()->pending({});
        QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
    }

    void lateFailureAfterClosingDialogIsSafe() {
        auto *dialog = new AddSubscriptionDialog;
        dialog->findChild<QLineEdit *>("fsntInput")->setText("https://subscription.invalid/test");
        dialog->findChild<QPushButton *>("fsntPrimary")->click();
        delete dialog;
        Subscription::updater()->pending({Subscription::UpdateOutcome::Failure::Fetch, "failure"});
        QCoreApplication::processEvents();
    }
};

QTEST_MAIN(AddSubscriptionDialogTest)
#include "AddSubscriptionDialogTest.moc"
