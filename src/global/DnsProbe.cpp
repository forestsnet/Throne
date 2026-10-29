#include "include/global/DnsProbe.hpp"

#include <QElapsedTimer>

#include <memory>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

#include "include/configs/DnsAddress.hpp"

namespace Configs {
    namespace {
        // Щуп обязан ходить мимо системного прокси. Ядро при резолве им не
        // пользуется — у sing-box свой стек, — поэтому проба через прокси мерит
        // не тот путь. На машине с настроенным прокси это выглядело так:
        // заданный DoT «не отвечает», клиент уходит с него без причины.
        // Проверено на стенде: с поддельным прокси tls://77.88.8.8 молчал,
        // без него ответил за 222 мс.

        // Имя для пробы: существует всегда, короткое, ни у кого не вызывает
        // вопросов в журналах и нигде не блокируется.
        const auto kProbeName = QStringLiteral("example.com");

        void appendName(QByteArray &out, const QString &name) {
            for (const auto &label : name.split('.')) {
                const auto bytes = label.toUtf8();
                out.append(static_cast<char>(bytes.size()));
                out.append(bytes);
            }
            out.append('\0');
        }
    } // namespace

    QByteArray BuildDnsQuery(const QString &name, quint16 id) {
        QByteArray query;
        query.append(static_cast<char>(id >> 8));
        query.append(static_cast<char>(id & 0xff));
        query.append('\x01'); // recursion desired
        query.append('\0');
        query.append('\0').append('\x01'); // один вопрос
        query.append('\0').append('\0');   // ответов нет
        query.append('\0').append('\0');
        query.append('\0').append('\0');
        appendName(query, name);
        query.append('\0').append('\x01'); // тип A
        query.append('\0').append('\x01'); // класс IN
        return query;
    }

    bool IsDnsResponseFor(const QByteArray &payload, quint16 id, QString *error) {
        const auto fail = [&](const QString &why) {
            if (error != nullptr) *error = why;
            return false;
        };
        if (payload.size() < 12) return fail(QStringLiteral("ответ короче заголовка"));

        const quint16 answerId = (static_cast<quint8>(payload[0]) << 8) | static_cast<quint8>(payload[1]);
        if (answerId != id) return fail(QStringLiteral("ответ от другого запроса"));
        if ((static_cast<quint8>(payload[2]) & 0x80) == 0) return fail(QStringLiteral("это не ответ"));

        const int rcode = static_cast<quint8>(payload[3]) & 0x0f;
        if (rcode == 2) return fail(QStringLiteral("сервер ответил отказом (SERVFAIL)"));
        if (rcode == 5) return fail(QStringLiteral("сервер отказался отвечать (REFUSED)"));
        // NXDOMAIN и пустой ответ нас устраивают: сервер жив и разговаривает,
        // а записи конкретного имени нам не нужны.
        return true;
    }

    QStringList ParseDnsAnswers(const QByteArray &payload) {
        QStringList addresses;
        if (payload.size() < 12) return addresses;
        const auto byteAt = [&payload](int index) { return static_cast<quint8>(payload.at(index)); };
        const int questions = (byteAt(4) << 8) | byteAt(5);
        const int answers = (byteAt(6) << 8) | byteAt(7);

        // Имя в записи либо кончается нулём, либо обрывается указателем на уже
        // встреченное имя — сжатие. Нам читать имя не нужно, нужно только мимо
        // него пройти, поэтому хватает двух этих случаев.
        const auto skipName = [&](int offset) {
            while (offset < payload.size()) {
                const auto length = byteAt(offset);
                if ((length & 0xc0) == 0xc0) return offset + 2;
                if (length == 0) return offset + 1;
                offset += length + 1;
            }
            return -1;
        };

        int offset = 12;
        for (int i = 0; i < questions; ++i) {
            offset = skipName(offset);
            if (offset < 0 || offset + 4 > payload.size()) return addresses;
            offset += 4; // тип и класс вопроса
        }

        for (int i = 0; i < answers; ++i) {
            offset = skipName(offset);
            if (offset < 0 || offset + 10 > payload.size()) return addresses;
            const int type = (byteAt(offset) << 8) | byteAt(offset + 1);
            const int dataLength = (byteAt(offset + 8) << 8) | byteAt(offset + 9);
            offset += 10;
            if (dataLength < 0 || offset + dataLength > payload.size()) return addresses;
            if (type == 1 && dataLength == 4) {
                addresses << QString("%1.%2.%3.%4")
                                 .arg(byteAt(offset))
                                 .arg(byteAt(offset + 1))
                                 .arg(byteAt(offset + 2))
                                 .arg(byteAt(offset + 3));
            } else if (type == 28 && dataLength == 16) {
                QStringList groups;
                for (int part = 0; part < 16; part += 2) {
                    groups << QString("%1").arg((byteAt(offset + part) << 8) | byteAt(offset + part + 1), 0, 16);
                }
                addresses << groups.join(':');
            }
            offset += dataLength;
        }
        return addresses;
    }

    DnsProbe::DnsProbe(QObject *parent) : QObject(parent) {}

    QString DnsProbe::Fastest(const QList<DnsProbeResult> &results) {
        QString best;
        int bestLatency = -1;
        for (const auto &result : results) {
            if (!result.ok) continue;
            if (bestLatency < 0 || result.latencyMs < bestLatency) {
                bestLatency = result.latencyMs;
                best = result.address;
            }
        }
        return best;
    }

    void DnsProbe::Start(const QStringList &addresses, int timeoutMs, const QString &probeName) {
        m_probeName = probeName.trimmed().isEmpty() ? kProbeName : probeName.trimmed();
        m_results.clear();
        m_pending = static_cast<int>(addresses.size());
        for (const auto &address : addresses) {
            DnsProbeResult placeholder;
            placeholder.address = address;
            m_results.append(placeholder);
        }
        if (m_pending == 0) {
            finishLater();
            return;
        }
        for (int i = 0; i < addresses.size(); ++i) probeOne(i, addresses.at(i), timeoutMs);
    }

    void DnsProbe::complete(int index, bool ok, int latencyMs, const QString &error,
                            const QStringList &addresses) {
        if (index < 0 || index >= m_results.size()) return;
        auto &slot = m_results[index];
        if (slot.latencyMs != -1 || slot.ok || !slot.error.isEmpty()) return; // уже закрыт
        slot.ok = ok;
        slot.latencyMs = ok ? latencyMs : -1;
        slot.error = ok ? QString() : (error.isEmpty() ? QStringLiteral("нет ответа") : error);
        slot.addresses = addresses;
        if (--m_pending == 0) finishLater();
    }

    // Всегда через очередь событий. Разбор адреса ошибается сразу, и без этого
    // Start() на битом адресе успевал отдать результат раньше, чем вызывающий
    // вернулся из него, — подписаться на сигнал было уже поздно.
    void DnsProbe::finishLater() {
        QTimer::singleShot(0, this, [this] { emit Finished(m_results); });
    }

    void DnsProbe::probeOne(int index, const QString &address, int timeoutMs) {
        const auto parsed = Fsnt::ParseDnsAddress(address);
        if (!parsed.valid) {
            complete(index, false, -1, parsed.error);
            return;
        }
        if (parsed.isDhcp()) {
            // Адрес берётся у маршрутизатора в момент подключения; проверять
            // тут нечего, и объявлять его нерабочим тоже нельзя.
            complete(index, true, 0, {});
            return;
        }

        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(timeoutMs);

        // Завершиться проба может не один раз: таймаут срабатывает, а следом
        // приходит ошибка сокета или finished у уже прерванного запроса. Раньше
        // состояние лежало в сыром указателе и каждое завершение его удаляло —
        // второе освобождало уже освобождённое, и клиент падал. Падал ровно
        // там, где эти гонки и живут: на недоступном сервере.
        struct ProbeState {
            QElapsedTimer clock;
            bool finished = false;
        };
        auto state = std::make_shared<ProbeState>();
        state->clock.start();

        const auto elapsed = [state] { return static_cast<int>(state->clock.elapsed()); };
        const auto done = [this, index, timer, state](bool ok, int ms, const QString &err,
                                                      const QStringList &addresses = {}) {
            if (state->finished) return;
            state->finished = true;
            timer->stop();
            timer->deleteLater();
            complete(index, ok, ms, err, addresses);
        };

        if (parsed.isLocal()) {
            // Системный резолвер: своего сокета нет, спрашиваем через Qt.
            QHostInfo::lookupHost(m_probeName, this, [done, elapsed](const QHostInfo &info) {
                if (info.error() != QHostInfo::NoError) {
                    done(false, -1, info.errorString());
                    return;
                }
                QStringList addresses;
                for (const auto &address : info.addresses()) addresses << address.toString();
                done(true, elapsed(), {}, addresses);
            });
            connect(timer, &QTimer::timeout, this, [done] { done(false, -1, QStringLiteral("превышено ожидание")); });
            timer->start();
            return;
        }

        const quint16 id = static_cast<quint16>(QRandomGenerator::global()->bounded(1, 0xfffe));
        const QByteArray query = BuildDnsQuery(m_probeName, id);
        const quint16 port = static_cast<quint16>(parsed.port > 0 ? parsed.port : Fsnt::DefaultDnsPort(parsed.type));

        connect(timer, &QTimer::timeout, this, [done] { done(false, -1, QStringLiteral("превышено ожидание")); });
        timer->start();

        if (parsed.type == QLatin1String("udp")) {
            auto *socket = new QUdpSocket(this);
            socket->setProxy(QNetworkProxy::NoProxy);
            connect(timer, &QTimer::timeout, socket, &QObject::deleteLater);
            connect(socket, &QUdpSocket::readyRead, this, [socket, done, elapsed, id] {
                QByteArray payload(static_cast<int>(socket->pendingDatagramSize()), '\0');
                socket->readDatagram(payload.data(), payload.size());
                QString error;
                const bool ok = IsDnsResponseFor(payload, id, &error);
                socket->deleteLater();
                done(ok, elapsed(), error, ok ? ParseDnsAnswers(payload) : QStringList{});
            });
            socket->connectToHost(parsed.server, port);
            socket->write(query);
            return;
        }

        if (parsed.type == QLatin1String("https") || parsed.type == QLatin1String("h3")) {
            // RFC 8484: тело — тот же пакет, что и по UDP.
            auto *manager = new QNetworkAccessManager(this);
            manager->setProxy(QNetworkProxy::NoProxy);
            QUrl url;
            url.setScheme("https");
            url.setHost(parsed.server);
            if (parsed.port > 0) url.setPort(parsed.port);
            url.setPath(parsed.path.isEmpty() ? QStringLiteral("/dns-query") : parsed.path);
            QNetworkRequest request(url);
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/dns-message");
            request.setRawHeader("Accept", "application/dns-message");
            auto *reply = manager->post(request, query);
            connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
            connect(reply, &QNetworkReply::finished, this, [reply, manager, done, elapsed, id] {
                const auto networkError = reply->error();
                const auto errorText = reply->errorString();
                if (networkError != QNetworkReply::NoError) {
                    // Читать прерванный ответ нельзя: Qt пишет в журнал
                    // «device not open», а полезного там всё равно ничего нет.
                    reply->deleteLater();
                    manager->deleteLater();
                    done(false, -1, errorText);
                    return;
                }
                const auto payload = reply->readAll();
                reply->deleteLater();
                manager->deleteLater();
                QString error;
                const bool ok = IsDnsResponseFor(payload, id, &error);
                done(ok, elapsed(), error, ok ? ParseDnsAnswers(payload) : QStringList{});
            });
            return;
        }

        // tcp, tls и quic: поверх потока к сообщению приписывается его длина.
        // quic своего транспорта тут не получает — проверяем его как tls,
        // потому что отвечает тот же сервер и тот же порт.
        QByteArray framed;
        framed.append(static_cast<char>(query.size() >> 8));
        framed.append(static_cast<char>(query.size() & 0xff));
        framed.append(query);

        const bool encrypted = parsed.type != QLatin1String("tcp");
        QTcpSocket *socket = encrypted ? new QSslSocket(this) : new QTcpSocket(this);
        socket->setProxy(QNetworkProxy::NoProxy);
        connect(timer, &QTimer::timeout, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [socket, buffer, done, elapsed, id] {
            buffer->append(socket->readAll());
            if (buffer->size() < 2) return;
            const int expected = (static_cast<quint8>(buffer->at(0)) << 8) | static_cast<quint8>(buffer->at(1));
            if (buffer->size() < expected + 2) return;
            const auto message = buffer->mid(2, expected);
            QString error;
            const bool ok = IsDnsResponseFor(message, id, &error);
            socket->deleteLater();
            done(ok, elapsed(), error, ok ? ParseDnsAnswers(message) : QStringList{});
        });
        connect(socket, &QAbstractSocket::errorOccurred, this, [socket, done](QAbstractSocket::SocketError) {
            const auto text = socket->errorString();
            socket->deleteLater();
            done(false, -1, text);
        });

        if (encrypted) {
            auto *ssl = qobject_cast<QSslSocket *>(socket);
            connect(ssl, &QSslSocket::encrypted, this, [ssl, framed] { ssl->write(framed); });
            ssl->connectToHostEncrypted(parsed.server, port);
        } else {
            connect(socket, &QTcpSocket::connected, this, [socket, framed] { socket->write(framed); });
            socket->connectToHost(parsed.server, port);
        }
    }
} // namespace Configs
