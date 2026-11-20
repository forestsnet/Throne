#pragma once

#include <QObject>
#include <QSsl>
#include <functional>

class QNetworkProxy;

namespace Configs_network {
    struct HTTPResponse {
        QString error;
        QByteArray data;
        QList<QPair<QByteArray, QByteArray>> header;
    };

    struct DownloadProgressReport
    {
        QString fileName;
        qint64 downloadedSize;
        qint64 totalSize;
    };

    struct HttpGetOptions {
        bool useProxy = false;
        // > 0 aborts the transfer once the body exceeds it and reports an error.
        qint64 maxBytes = 0;
        // Empty sends the global User-Agent.
        QString userAgent;
        QList<QPair<QByteArray, QByteArray>> headers;
        QSsl::SslProtocol tlsProtocol = QSsl::SecureProtocols;
        bool http2 = true;
        // Срок на передачу. Короче умолчания берут там, где за одной попыткой
        // стоит очередь следующих и ждать её полную минуту незачем.
        int timeoutMs = 10000;
        // Имя для SNI и проверки сертификата, когда в URL стоит адрес.
        // Заголовок Host вызывающий ставит сам: Qt берёт его из URL.
        QString peerVerifyName;
        // Verifies the certificate even when net_insecure is on.
        bool strictTls = false;
        // Ignores net_use_proxy and a saved system-proxy mode: the request goes
        // out the way the system routes it. For a second attempt past the core.
        bool bypassProxy = false;
    };

    // Explicit net_use_proxy/useProxy needs a running profile. A saved system
    // proxy mode alone permits bootstrap while stopped unless kill_switch is on.
    // Without the mixed inbound a running Tun Mode captures the request instead.
    // False with *error set when nothing can proxy it.
    bool RequestUsesMixedInbound(bool useProxy, QString *error);

    // The proxy to set when RequestUsesMixedInbound says yes.
    QNetworkProxy MixedInboundProxy();

    class NetworkRequestHelper : QObject {
        Q_OBJECT

        explicit NetworkRequestHelper(QObject *parent) : QObject(parent){};

        ~NetworkRequestHelper() override = default;
        ;

    public:
        static HTTPResponse HttpGet(const QString &url, bool useProxy = false);

        static HTTPResponse HttpGet(const QString &url, const HttpGetOptions &options);

        static QString GetHeader(const QList<QPair<QByteArray, QByteArray>> &header, const QString &name);

        // destinationDir пуст -> файл кладётся в Configs::GetBasePath() (прежнее поведение).
        static QString DownloadAsset(const QString &url, const QString &fileName, bool useProxy = false,
                                     bool strictTls = false, const QString &destinationDir = {});

        // Receives DownloadAsset progress on the main thread: show false ends the report. Without a sink nothing is reported.
        using DownloadProgressSink = std::function<void(const DownloadProgressReport &report, bool show)>;

        static void SetDownloadProgressSink(DownloadProgressSink sink);
    };
} // namespace Configs_network

using namespace Configs_network;
