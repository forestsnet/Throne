#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include "include/global/DirectDnsSelection.hpp"

namespace Configs {
    // Подбирает рабочий прямой DNS перед подключением.
    //
    // Прямой DNS разрешает имя нашего же сервера. Пока он молчит, подключаться
    // некуда, а туннель к этому моменту уже забрал маршруты — машина остаётся
    // без сети вообще, и клиент бесконечно ретраит вместо того, чтобы сказать
    // правду. Поэтому подбор идёт до поднятия туннеля, через физический путь ядра
    // даже при переключении с уже работающего профиля.
    class DnsAutoSelect : public QObject {
        Q_OBJECT

    public:
        explicit DnsAutoSelect(QObject *parent = nullptr);

        // configured — то, что стоит в настройках. Опрашивается не он один, а
        // сразу вся цепочка кандидатов, одним кругом и параллельно: круг всё
        // равно занимает один таймаут, а проверка заданного сервера в одиночку
        // ровно то и давала, что сервер отвечал на единственную пробу и
        // рассыпался в работе — подбор при этом не запускался никогда.
        // Заданный сервер остаётся в приоритете: ответил — берём его.
        // probeName — имя сервера, к которому идёт подключение. Проверять надо
        // именно его: чужое имя резолвер может знать, а нужное — нет.
        // extraNames — остальные имена того же конфига. Круг проб идёт по
        // одному имени, им он резолвер и проверяет, а закрепить надо все:
        // полный конфиг набирает по узлу на страну, и незакреплённые уйдут
        // спрашивать имя уже под туннелем.
        void Start(const QString &configured, int timeoutMs = 2500, const QString &probeName = {},
                   const QStringList &extraNames = {});

    signals:
        // address is empty if no eligible resolver returned an address.
        // failure distinguishes no response from an unsuitable or negative response.
        // fellBack — выбрали не то, что задано, и об этом стоит сказать.
        // serverAddresses — имя сервера и чем по нему ответили, по записи на
        // каждое разрешённое имя конфига. Их прибивают в конфиг, и тогда под
        // туннелем разрешать их уже не надо. Пусто, если ни один ответ не
        // содержал адреса.
        void Selected(const QString &address, bool fellBack, const QMap<QString, QStringList> &serverAddresses,
                      Configs::DirectDnsFailure failure);

    private:
        // Для журнала: почему заданный сервер не подошёл.
        [[nodiscard]] QString whyNotConfigured(const QList<DnsProbeResult> &results) const;

        // Второй проход по остальным именам, если они есть, и отправка итога.
        // Отправляет ровно один раз, каким бы путём сюда ни пришли.
        void finish(const QString &chosen, bool fellBack, DirectDnsFailure failure);

        QString m_configured;
        QString m_probeName;
        QStringList m_extraNames;
        int m_timeoutMs = 2500;
        QMap<QString, QStringList> m_pins;
    };
} // namespace Configs
