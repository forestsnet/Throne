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
        // Что он ответил по проверяемому имени. Это и есть адрес нашего же
        // сервера: разрешать его второй раз, уже под туннелем, незачем.
        QStringList addresses;
        // A valid negative DNS response still proves the server answered.
        bool responded = false;
    };

    // A Qt DNS protocol probe that follows OS routes. NoProxy does not bypass TUN.
    // Direct DNS selection and diagnostics must use ProbeDirectDns instead.
    class DnsProbe : public QObject {
        Q_OBJECT

    public:
        explicit DnsProbe(QObject *parent = nullptr);

        // Пробует все адреса разом и отдаёт результаты в порядке, в котором их
        // задали. Параллельно, потому что последовательно по три секунды на
        // каждого — это минута ожидания на десятке кандидатов.
        // probeName — имя, которым проверяем. По умолчанию нейтральное, но
        // вызывающий обязан передать имя того сервера, к которому собирается
        // подключаться: резолвер, бодро отвечающий на постороннее имя, вполне
        // может не знать нужного или отдавать по нему подменённый адрес, и
        // тогда «рабочий» по итогам пробы сервер нам ничем не поможет.
        void Start(const QStringList &addresses, int timeoutMs = 3000, const QString &probeName = {});

        // Самый быстрый из ответивших; при отсутствии таковых — пустая строка.
        static QString Fastest(const QList<DnsProbeResult> &results);

    signals:
        void Finished(const QList<Configs::DnsProbeResult> &results);

    private:
        void probeOne(int index, const QString &address, int timeoutMs);

        QString m_probeName;
        void finishLater();
        void complete(int index, bool ok, int latencyMs, const QString &error,
                      const QStringList &addresses = {});

        QList<DnsProbeResult> m_results;
        int m_pending = 0;
    };

    // Собрать запрос A-записи и разобрать ответ. Вынесено сюда ради теста:
    // сетевую часть в тесте не погонять, а разбор ответа ошибается молча.
    QByteArray BuildDnsQuery(const QString &name, quint16 id);
    bool IsDnsResponseFor(const QByteArray &payload, quint16 id, QString *error);

    // Адреса из секции ответов. Пустой список — законный ответ: имя может не
    // иметь записей нужного типа, и сервер при этом жив.
    QStringList ParseDnsAnswers(const QByteArray &payload);
} // namespace Configs

Q_DECLARE_METATYPE(Configs::DnsProbeResult)
