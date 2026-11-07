#include <QFile>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include "include/global/Configs.hpp"
#include "include/global/HTTPRequestHelper.hpp"

// Only the database and application services are replaced. Requests use the
// production helper, real Qt networking and a local HTTP server.
namespace LocalNetwork {
QString InboundConnectHost() { return QStringLiteral("127.0.0.1"); }
}

class HTTPRequestHelperTest : public QObject {
    Q_OBJECT

    static void serve(QTcpServer &server, int &requests) {
        connect(&server, &QTcpServer::newConnection, &server, [&server, &requests] {
            while (auto *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [socket, &requests] {
                    const auto request = socket->readAll();
                    if (request.isEmpty()) return;
                    ++requests;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 7\r\nConnection: close\r\n\r\nprofile");
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

private slots:
    void init() {
        *Configs::dataManager->settingsRepo = {};
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    }

    void requestPolicy_data() {
        QTest::addColumn<bool>("systemProxy");
        QTest::addColumn<bool>("globalForce");
        QTest::addColumn<bool>("requestForce");
        QTest::addColumn<bool>("running");
        QTest::addColumn<bool>("killSwitch");
        QTest::addColumn<bool>("mixedDisabled");
        QTest::addColumn<bool>("tun");
        QTest::addColumn<bool>("mixedExpected");
        QTest::addColumn<bool>("errorExpected");
        QTest::newRow("first import") << true << false << false << false << false << false << true << false << false;
        QTest::newRow("stopped import") << true << false << false << false << false << false << false << false << false;
        QTest::newRow("explicit global proxy") << true << true << false << false << false << false << true << false << true;
        QTest::newRow("explicit request proxy") << true << false << true << false << false << false << true << false << true;
        QTest::newRow("kill switch stopped") << true << false << false << false << true << false << true << false << true;
        QTest::newRow("active proxy") << true << false << false << true << false << false << true << true << false;
        QTest::newRow("active force proxy") << false << true << false << true << false << false << false << true << false;
        QTest::newRow("active kill switch") << true << false << false << true << true << false << true << true << false;
        QTest::newRow("tun fallback") << true << false << false << true << false << true << true << false << false;
        QTest::newRow("no mixed or tun") << true << false << false << true << false << true << false << false << true;
        QTest::newRow("ordinary direct") << false << false << false << false << false << false << false << false << false;
    }

    void requestPolicy() {
        QFETCH(bool, systemProxy);
        QFETCH(bool, globalForce);
        QFETCH(bool, requestForce);
        QFETCH(bool, running);
        QFETCH(bool, killSwitch);
        QFETCH(bool, mixedDisabled);
        QFETCH(bool, tun);
        QFETCH(bool, mixedExpected);
        QFETCH(bool, errorExpected);
        auto &settings = *Configs::dataManager->settingsRepo;
        settings.spmode_system_proxy = systemProxy;
        settings.net_use_proxy = globalForce;
        settings.started_id = running ? 1 : -1919;
        settings.kill_switch = killSwitch;
        settings.disable_mixed_inbound = mixedDisabled;
        settings.spmode_vpn = tun;
        QString error;
        QCOMPARE(RequestUsesMixedInbound(requestForce, &error), mixedExpected);
        QCOMPARE(!error.isEmpty(), errorExpected);
    }

    void bootstrapFetchAndAssetReachServer() {
        auto &settings = *Configs::dataManager->settingsRepo;
        settings.spmode_system_proxy = true;
        settings.spmode_vpn = true;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int requests = 0;
        serve(server, requests);
        const auto url = QStringLiteral("http://127.0.0.1:%1/sub").arg(server.serverPort());
        const auto response = NetworkRequestHelper::HttpGet(url);
        QVERIFY2(response.error.isEmpty(), qPrintable(response.error));
        QCOMPARE(response.data, QByteArray("profile"));
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        const auto error = NetworkRequestHelper::DownloadAsset(url, "asset.dat", false, false, destination.path());
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QFile file(destination.filePath("asset.dat"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("profile"));
        QCOMPARE(requests, 2);
    }

    void explicitProxyAndKillSwitchNeverReachDirectServer() {
        auto &settings = *Configs::dataManager->settingsRepo;
        settings.spmode_system_proxy = true;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int requests = 0;
        serve(server, requests);
        const auto url = QStringLiteral("http://127.0.0.1:%1/sub").arg(server.serverPort());
        QVERIFY(!NetworkRequestHelper::HttpGet(url, true).error.isEmpty());
        settings.net_use_proxy = true;
        QVERIFY(!NetworkRequestHelper::HttpGet(url).error.isEmpty());
        settings.net_use_proxy = false;
        settings.kill_switch = true;
        QVERIFY(!NetworkRequestHelper::HttpGet(url).error.isEmpty());
        QCoreApplication::processEvents();
        QCOMPARE(requests, 0);
    }

    void activeProfileUsesMixedInboundAndNeverRetriesDirect() {
        auto &settings = *Configs::dataManager->settingsRepo;
        settings.spmode_system_proxy = true;
        settings.started_id = 1;
        QTcpServer proxy;
        QVERIFY(proxy.listen(QHostAddress::LocalHost));
        int proxyRequests = 0;
        serve(proxy, proxyRequests);
        settings.inbound_socks_port = proxy.serverPort();
        const auto response = NetworkRequestHelper::HttpGet("http://does-not-resolve.invalid/sub");
        QVERIFY2(response.error.isEmpty(), qPrintable(response.error));
        QCOMPARE(response.data, QByteArray("profile"));
        QCOMPARE(proxyRequests, 1);
        QTcpServer origin;
        QVERIFY(origin.listen(QHostAddress::LocalHost));
        // Bind the origin before releasing the proxy port, so the OS cannot
        // reuse that port and turn a refused proxy into a successful origin.
        proxy.close();
        int directRequests = 0;
        serve(origin, directRequests);
        const auto failed = NetworkRequestHelper::HttpGet(QStringLiteral("http://127.0.0.1:%1/sub").arg(origin.serverPort()));
        QVERIFY(!failed.error.isEmpty());
        QCOMPARE(directRequests, 0);
    }
};

QTEST_GUILESS_MAIN(HTTPRequestHelperTest)
#include "HTTPRequestHelperTest.moc"
