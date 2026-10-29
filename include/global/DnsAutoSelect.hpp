#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "include/global/DnsProbe.hpp"

namespace Configs {
    // Подбирает рабочий прямой DNS перед подключением.
    //
    // Прямой DNS разрешает имя нашего же сервера. Пока он молчит, подключаться
    // некуда, а туннель к этому моменту уже забрал маршруты — машина остаётся
    // без сети вообще, и клиент бесконечно ретраит вместо того, чтобы сказать
    // правду. Поэтому подбор идёт до поднятия туннеля.
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
        void Start(const QString &configured, int timeoutMs = 2500, const QString &probeName = {});

    signals:
        // address пуст, если не ответил никто из тех, кто переживёт туннель:
        // подключаться в этом случае нельзя.
        // fellBack — выбрали не то, что задано, и об этом стоит сказать.
        // serverAddresses — чем ответили по имени сервера, к которому идём.
        // Их прибивают в конфиг, и тогда под туннелем это имя разрешать уже не
        // надо. Пусты, если имя не задавали или не ответил вообще никто.
        void Selected(const QString &address, bool fellBack, const QStringList &serverAddresses);

    private:
        // Для журнала: почему заданный сервер не подошёл.
        [[nodiscard]] QString whyNotConfigured(const QList<DnsProbeResult> &results) const;

        QString m_configured;
        QString m_probeName;
        int m_timeoutMs = 2500;
        QStringList m_resolved;
    };
} // namespace Configs
