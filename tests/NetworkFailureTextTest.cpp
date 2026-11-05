#include <QNetworkReply>
#include <QTest>

#include "include/global/NetworkFailureText.hpp"

using namespace Configs_network;

class NetworkFailureTextTest : public QObject {
    Q_OBJECT

private:
    // Каждая ветка и вызов: проверять их по одному — значит переписать
    // тот же switch второй раз, а так падает ровно то, что разъехалось.
    struct Case {
        QNetworkReply::NetworkError error;
        int status;
    };

    static QList<Case> everyBranch() {
        return {
            {QNetworkReply::ContentAccessDenied, 401},
            {QNetworkReply::AuthenticationRequiredError, 407},
            {QNetworkReply::ContentAccessDenied, 403},
            {QNetworkReply::ContentNotFoundError, 404},
            {QNetworkReply::ContentGoneError, 410},
            {QNetworkReply::UnknownContentError, 429},
            {QNetworkReply::InternalServerError, 503},
            {QNetworkReply::UnknownContentError, 418},
            {QNetworkReply::ProtocolUnknownError, 304},
            {QNetworkReply::HostNotFoundError, 0},
            {QNetworkReply::ConnectionRefusedError, 0},
            {QNetworkReply::RemoteHostClosedError, 0},
            {QNetworkReply::TimeoutError, 0},
            {QNetworkReply::OperationCanceledError, 0},
            {QNetworkReply::SslHandshakeFailedError, 0},
            {QNetworkReply::ProxyConnectionRefusedError, 0},
            {QNetworkReply::ProxyAuthenticationRequiredError, 0},
            {QNetworkReply::TemporaryNetworkFailureError, 0},
            {QNetworkReply::UnknownNetworkError, 0},
            {QNetworkReply::TooManyRedirectsError, 0},
            {QNetworkReply::InsecureRedirectError, 0},
            {QNetworkReply::UnknownServerError, 0},
        };
    }

private slots:
    // Ради этого всё и написано: ответ сервера называет причину, а не
    // пересказывает enum, и код состояния виден.
    void theStatusNamesTheReason() {
        QCOMPARE(NetworkFailureText(QNetworkReply::ContentNotFoundError, 404),
                 QStringLiteral("there is nothing at this address (HTTP 404)"));
        QCOMPARE(NetworkFailureText(QNetworkReply::ContentAccessDenied, 403),
                 QStringLiteral("the server refused access (HTTP 403)"));
        QCOMPARE(NetworkFailureText(QNetworkReply::InternalServerError, 502),
                 QStringLiteral("the server is having trouble (HTTP 502)"));
        QCOMPARE(NetworkFailureText(QNetworkReply::UnknownContentError, 418),
                 QStringLiteral("the server turned the request down (HTTP 418)"));
    }

    // Один и тот же ContentNotFoundError приходит и с ответом сервера, и без
    // него. Ответ важнее: 404 человеку говорит больше, чем «запрос не удался».
    void theStatusWinsOverTheError() {
        QCOMPARE(NetworkFailureText(QNetworkReply::HostNotFoundError, 503),
                 QStringLiteral("the server is having trouble (HTTP 503)"));
        QVERIFY(NetworkFailureText(QNetworkReply::ContentNotFoundError, 0)
                != NetworkFailureText(QNetworkReply::ContentNotFoundError, 404));
    }

    // Сервер не ответил — причину называет enum.
    void aSilentServerStillGetsAReason() {
        QCOMPARE(NetworkFailureText(QNetworkReply::HostNotFoundError, 0),
                 QStringLiteral("the server name does not resolve"));
        QCOMPARE(NetworkFailureText(QNetworkReply::TimeoutError, 0),
                 QStringLiteral("the server did not answer in time"));
        QCOMPARE(NetworkFailureText(QNetworkReply::SslHandshakeFailedError, 0),
                 QStringLiteral("the encrypted connection could not be set up"));
        QCOMPARE(NetworkFailureText(QNetworkReply::ProxyTimeoutError, 0),
                 QStringLiteral("the proxy did not let the request through"));
    }

    // Незнакомый отказ тоже получает текст: пустая строка дошла бы до окна как
    // «Не удалось загрузить подписку: » с пустым хвостом.
    void anUnknownFailureStillGetsAText() {
        QVERIFY(!NetworkFailureText(QNetworkReply::UnknownServerError, 0).isEmpty());
        QVERIFY(!NetworkFailureText(static_cast<QNetworkReply::NetworkError>(9999), 0).isEmpty());
    }

    // Текст подставляется после двоеточия («Не удалось загрузить подписку: %1»),
    // поэтому он со строчной буквы и без точки на конце.
    void everyTextFitsAfterAColon() {
        for (const auto &[error, status] : everyBranch()) {
            const QString text = NetworkFailureText(error, status);
            QVERIFY2(!text.isEmpty(), qPrintable(QString::number(status)));
            QVERIFY2(text.at(0).isLower(), qPrintable(text));
            QVERIFY2(!text.endsWith('.'), qPrintable(text));
        }
    }

    // Код состояния подставляется, а не остаётся заготовкой.
    void theStatusIsSubstituted() {
        for (const auto &[error, status] : everyBranch()) {
            QVERIFY2(!NetworkFailureText(error, status).contains(QStringLiteral("%1")),
                     qPrintable(QString::number(status)));
        }
    }

    // Строка Qt сюда попасть не может: с ней и приехал английский хвост в
    // русское окно, и вместе с ним полный адрес подписки с токеном.
    void noTextRepeatsQt() {
        for (const auto &[error, status] : everyBranch()) {
            const QString text = NetworkFailureText(error, status);
            QVERIFY2(!text.contains(QStringLiteral("Error transferring")), qPrintable(text));
            QVERIFY2(!text.contains(QStringLiteral("server replied")), qPrintable(text));
            QVERIFY2(!text.contains(QStringLiteral("://")), qPrintable(text));
        }
    }
};

QTEST_APPLESS_MAIN(NetworkFailureTextTest)
#include "NetworkFailureTextTest.moc"
