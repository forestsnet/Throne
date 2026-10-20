#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace Configs {
    struct DnsProbeResult {
        QString address;   // ровно в том виде, в каком был задан
        bool ok = false;
        int latencyMs = -1;
        QString error;
    };

    // Настоящий запрос к DNS-серверу, а не проверка «порт открыт». Открытый порт
    // ничего не доказывает: у человека, с которого всё началось, 853 именно
    // отвечал молчанием до таймаута.
    //
    // Нужен до поднятия туннеля: именно прямой DNS разрешает имя нашего же
    // сервера, и если он недоступен, подключаться некуда, а туннель к этому
    // моменту уже забрал маршруты и оставил машину без сети.
    class DnsProbe : public QObject {
        Q_OBJECT

    public:
        explicit DnsProbe(QObject *parent = nullptr);

        // Пробует все адреса разом и отдаёт результаты в порядке, в котором их
        // задали. Параллельно, потому что последовательно по три секунды на
        // каждого — это минута ожидания на десятке кандидатов.
        void Start(const QStringList &addresses, int timeoutMs = 3000);

        // Самый быстрый из ответивших; при отсутствии таковых — пустая строка.
        static QString Fastest(const QList<DnsProbeResult> &results);

    signals:
        void Finished(const QList<Configs::DnsProbeResult> &results);

    private:
        void probeOne(int index, const QString &address, int timeoutMs);
        void finishLater();
        void complete(int index, bool ok, int latencyMs, const QString &error);

        QList<DnsProbeResult> m_results;
        int m_pending = 0;
    };

    // Собрать запрос A-записи и разобрать ответ. Вынесено сюда ради теста:
    // сетевую часть в тесте не погонять, а разбор ответа ошибается молча.
    QByteArray BuildDnsQuery(const QString &name, quint16 id);
    bool IsDnsResponseFor(const QByteArray &payload, quint16 id, QString *error);
} // namespace Configs

Q_DECLARE_METATYPE(Configs::DnsProbeResult)
