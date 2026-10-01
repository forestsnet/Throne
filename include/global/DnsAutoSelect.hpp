#pragma once

#include <QObject>
#include <QString>

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

        // configured — то, что стоит в настройках. Сначала проверяется он один:
        // в обычном случае это ответ за доли секунды и никакой задержки при
        // подключении. Вся цепочка опрашивается, только если он не ответил.
        void Start(const QString &configured, int timeoutMs = 2500);

    signals:
        // address пуст, если не ответил никто: подключаться в этом случае нельзя.
        // fellBack — выбрали не то, что задано, и об этом стоит сказать.
        void Selected(const QString &address, bool fellBack);

    private:
        QString m_configured;
        int m_timeoutMs = 2500;
    };
} // namespace Configs
