#include "include/global/NetworkFailureText.hpp"

#include <QObject>

namespace Configs_network {
    QString NetworkFailureText(const QNetworkReply::NetworkError error, const int httpStatus) {
        // A server that answered says more than the error enum does, so the status
        // goes first: a 404 arrives as ContentNotFoundError either way.
        if (httpStatus == 401 || httpStatus == 407) return QObject::tr("the server wants a login (HTTP %1)").arg(httpStatus);
        if (httpStatus == 403) return QObject::tr("the server refused access (HTTP 403)");
        if (httpStatus == 404 || httpStatus == 410) return QObject::tr("there is nothing at this address (HTTP %1)").arg(httpStatus);
        if (httpStatus == 429) return QObject::tr("the server asks to wait before the next request (HTTP 429)");
        if (httpStatus >= 500 && httpStatus < 600) return QObject::tr("the server is having trouble (HTTP %1)").arg(httpStatus);
        if (httpStatus >= 400) return QObject::tr("the server turned the request down (HTTP %1)").arg(httpStatus);
        if (httpStatus >= 300) return QObject::tr("the server answered with HTTP %1").arg(httpStatus);
        switch (error) {
            case QNetworkReply::HostNotFoundError:
                return QObject::tr("the server name does not resolve");
            case QNetworkReply::ConnectionRefusedError:
                return QObject::tr("the server refused the connection");
            case QNetworkReply::RemoteHostClosedError:
                return QObject::tr("the server closed the connection");
            case QNetworkReply::TimeoutError:
            case QNetworkReply::OperationCanceledError:
                return QObject::tr("the server did not answer in time");
            case QNetworkReply::SslHandshakeFailedError:
                return QObject::tr("the encrypted connection could not be set up");
            case QNetworkReply::ProxyConnectionRefusedError:
            case QNetworkReply::ProxyConnectionClosedError:
            case QNetworkReply::ProxyNotFoundError:
            case QNetworkReply::ProxyTimeoutError:
            case QNetworkReply::ProxyAuthenticationRequiredError:
                return QObject::tr("the proxy did not let the request through");
            case QNetworkReply::TemporaryNetworkFailureError:
            case QNetworkReply::NetworkSessionFailedError:
            case QNetworkReply::UnknownNetworkError:
                return QObject::tr("there is no network");
            case QNetworkReply::TooManyRedirectsError:
                return QObject::tr("the server redirects in circles");
            case QNetworkReply::InsecureRedirectError:
                return QObject::tr("the server redirects to an address without encryption");
            default:
                // Everything left is either a content or protocol error the server
                // reported with a status, and that was handled above, or something
                // nobody meets outside a broken build.
                return QObject::tr("the request failed");
        }
    }
} // namespace Configs_network
