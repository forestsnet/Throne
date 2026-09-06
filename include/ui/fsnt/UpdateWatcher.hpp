#pragma once

#include <QObject>
#include <QString>

class QTimer;

namespace Fsnt {
    // Тихая проверка обновлений в фоне. Ничего не показывает сама: находит и
    // говорит окну. Модальных окон здесь быть не должно — человек не просил
    // его отвлекать, он просто работает.
    class UpdateWatcher : public QObject {
        Q_OBJECT

    public:
        explicit UpdateWatcher(QObject *parent = nullptr);

        // Первая проверка идёт с задержкой: на старте клиенту есть чем заняться,
        // и лезть в сеть за релизами в этот момент незачем.
        void start();

    signals:
        void updateFound(const QString &tag, const QString &notes);

    private:
        void check();

        QTimer *m_timer = nullptr;
    };
} // namespace Fsnt
