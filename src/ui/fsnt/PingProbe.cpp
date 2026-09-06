#include "include/ui/fsnt/PingProbe.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>

#include "include/database/ProfilesRepo.h"
#include "include/configs/common/Outbound.h"
#include "include/global/Configs.hpp"
#include "include/global/Utils.hpp"

namespace {
    // Столько ждём ответа от самого сервера. Дальше держать смысла нет: живой
    // узел для VPN отвечает за десятые доли секунды, а мёртвый не ответит и за
    // минуту.
    constexpr int kProbeTimeoutMs = 3000;

    // Windows пишет время как "время=12мс" или "time=12ms", macOS и Linux —
    // "time=12.3 ms". Берём первое число после "=" в строке с ответом.
    int parsePingOutput(const QString &output) {
        static const QRegularExpression re(QStringLiteral("(?:time|время)[=<]\\s*(\\d+(?:[.,]\\d+)?)"),
                                           QRegularExpression::CaseInsensitiveOption);
        const auto match = re.match(output);
        if (!match.hasMatch()) return -1;
        bool ok = false;
        const double value = QString(match.captured(1)).replace(',', '.').toDouble(&ok);
        if (!ok) return -1;
        return qMax(1, static_cast<int>(value + 0.5));
    }

    // Системный ping, а не свой ICMP: сырой сокет требует прав root на macOS и
    // Linux, а просить их ради замера — плохая сделка.
    int probeIcmp(const QString &host, QString &error) {
        // Ключ ожидания у всех свой: Windows и macOS считают в миллисекундах,
        // Linux — в секундах, и переданные туда миллисекунды означали бы час
        // ожидания вместо трёх секунд.
        QStringList args;
#if defined(Q_OS_WIN)
        args << "-n" << "1" << "-w" << QString::number(kProbeTimeoutMs) << host;
#elif defined(Q_OS_MACOS)
        args << "-c" << "1" << "-W" << QString::number(kProbeTimeoutMs) << host;
#else
        args << "-c" << "1" << "-W" << QString::number(qMax(1, kProbeTimeoutMs / 1000)) << host;
#endif
        QProcess ping;
        ping.start(QStringLiteral("ping"), args);
        if (!ping.waitForFinished(kProbeTimeoutMs + 2000)) {
            ping.kill();
            error = QObject::tr("no answer");
            return -1;
        }
        const QString output = QString::fromLocal8Bit(ping.readAllStandardOutput());
        const int ms = parsePingOutput(output);
        if (ms < 0) error = QObject::tr("no answer");
        return ms;
    }

    int probeTcp(const QString &host, int port, QString &error) {
        if (port <= 0) {
            error = QObject::tr("no port in the profile");
            return -1;
        }
        QTcpSocket socket;
        QElapsedTimer clock;
        clock.start();
        socket.connectToHost(host, static_cast<quint16>(port));
        if (!socket.waitForConnected(kProbeTimeoutMs)) {
            error = socket.errorString();
            return -1;
        }
        const int ms = qMax(1, static_cast<int>(clock.elapsed()));
        socket.abort();
        return ms;
    }

    // TLS-рукопожатие до того же адреса и порта. Сертификат не проверяем: у
    // Reality он заимствованный и «не тот» по определению, а меряем мы время,
    // а не доверие.
    int probeHandshake(const QString &host, int port, const QString &sni, QString &error) {
        if (port <= 0) {
            error = QObject::tr("no port in the profile");
            return -1;
        }
        QSslSocket socket;
        socket.setPeerVerifyMode(QSslSocket::VerifyNone);
        if (!sni.isEmpty()) socket.setPeerVerifyName(sni);

        QEventLoop loop;
        QElapsedTimer clock;
        int ms = -1;
        QString failure;

        QObject::connect(&socket, &QSslSocket::encrypted, &loop, [&] {
            ms = qMax(1, static_cast<int>(clock.elapsed()));
            loop.quit();
        });
        QObject::connect(&socket, &QAbstractSocket::errorOccurred, &loop, [&](QAbstractSocket::SocketError) {
            failure = socket.errorString();
            loop.quit();
        });
        QTimer::singleShot(kProbeTimeoutMs, &loop, [&] {
            if (failure.isEmpty() && ms < 0) failure = QObject::tr("no answer");
            loop.quit();
        });

        clock.start();
        socket.connectToHostEncrypted(host, static_cast<quint16>(port), sni.isEmpty() ? host : sni);
        loop.exec();
        socket.abort();

        if (ms < 0) error = failure.isEmpty() ? QObject::tr("no answer") : failure;
        return ms;
    }
} // namespace

namespace Fsnt {
    PingKind PingKindFromSetting(const int value) {
        switch (value) {
            case 0: return PingKind::Icmp;
            case 1: return PingKind::Tcp;
            case 2: return PingKind::Handshake;
            case 4: return PingKind::RequestHead;
            default: return PingKind::RequestGet;
        }
    }

    int PingKindToSetting(const PingKind kind) { return static_cast<int>(kind); }

    QString PingKindTitle(const PingKind kind) {
        switch (kind) {
            case PingKind::Icmp: return QObject::tr("Ping (ICMP)");
            case PingKind::Tcp: return QObject::tr("Connection to the port");
            case PingKind::Handshake: return QObject::tr("Handshake");
            case PingKind::RequestHead: return QObject::tr("Request (HEAD)");
            case PingKind::RequestGet: break;
        }
        return QObject::tr("Request (GET)");
    }

    QString PingKindHint(const PingKind kind) {
        switch (kind) {
            case PingKind::Icmp:
                return QObject::tr("Straight to the server, past the tunnel. Many servers drop these, "
                                   "so silence here does not mean the server is down.");
            case PingKind::Tcp:
                return QObject::tr("Straight to the server port, past the tunnel. Shows whether the "
                                   "port answers at all.");
            case PingKind::Handshake:
                return QObject::tr("Connection plus TLS handshake with the server. The closest thing "
                                   "to what starting a session costs.");
            case PingKind::RequestHead:
            case PingKind::RequestGet:
                break;
        }
        return QObject::tr("A real request through the tunnel: measures the whole path, the one you "
                           "feel when a page opens.");
    }

    void ProbeProfile(const int profileId, const PingKind kind,
                      const std::function<void(int, const QString &)> &done) {
        const auto profile = Configs::dataManager->profilesRepo->GetProfile(profileId);
        if (profile == nullptr || profile->outbound == nullptr) {
            if (done) done(-1, QObject::tr("profile not found"));
            return;
        }

        // Запрос умеет только ядро: ему нужно поднять этот выход и сходить через
        // него наружу. Результат оно само запишет в профиль.
        if (kind == PingKind::RequestGet || kind == PingKind::RequestHead) {
            if (MW_url_test_one) {
                MW_url_test_one(profileId, kind == PingKind::RequestHead ? QStringLiteral("HEAD")
                                                                        : QStringLiteral("GET"));
            }
            if (done) done(0, {});
            return;
        }

        const QString host = profile->outbound->GetAddress();
        const int port = profile->outbound->GetPort().toInt();
        // SNI берём из TLS профиля: для Reality он и есть тот домен, которым
        // сервер прикрывается, и рукопожатие идёт именно к нему.
        QString sni;
        if (profile->outbound->HasTLS()) {
            if (const auto tls = profile->outbound->GetTLS(); tls != nullptr && tls->enabled) {
                sni = tls->server_name;
            }
        }

        runOnNewThread([=] {
            QString error;
            int ms = -1;
            switch (kind) {
                case PingKind::Icmp: ms = probeIcmp(host, error); break;
                case PingKind::Tcp: ms = probeTcp(host, port, error); break;
                case PingKind::Handshake: ms = probeHandshake(host, port, sni, error); break;
                default: break;
            }

            runOnUiThread([=] {
                const auto ent = Configs::dataManager->profilesRepo->GetProfile(profileId);
                if (ent != nullptr) {
                    // Прочерк храним как -1: так его показывает и обычный замер.
                    ent->SetLatency(ms > 0 ? ms : -1);
                    Configs::dataManager->profilesRepo->Save(ent);
                }
                if (done) done(ms, error);
            });
        });
    }
} // namespace Fsnt
