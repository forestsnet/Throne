#pragma once

#include <QDateTime>
#include <QWidget>

class PowerButton;
class QLabel;
class QTimer;

// Правая панель: состояние подключения, таймер, кнопка и текущий сервер.
// Действия выполняет MainWindow — он движок; панель только показывает и просит.
class ConnectPanel : public QWidget {
    Q_OBJECT

public:
    explicit ConnectPanel(QWidget *parent = nullptr);

    // Вызывается окном, когда ядро сообщило о смене состояния.
    void refresh();

    // Ядро упало или не поднялось. Без этого попытка висела бы «Подключаемся»
    // до сторожевого таймера, хотя ответ уже пришёл.
    void reportFailure();

signals:
    // Нечего запускать: окно предложит добавить подписку.
    void subscriptionNeeded();
    // Какой сервер пойдёт в дело. Список подсвечивает его, чтобы подпись под
    // кнопкой и выделенная строка всегда показывали одно и то же.
    void profileResolved(int profileId);

private:
    // Какой сервер запускать и выбран ли он пользователем.
    struct Choice {
        int id = -1;
        bool automatic = true;
    };

    static bool isConnected();
    static Choice resolveProfile();

    void onButtonClicked();
    void updateElapsed();
    void setStatus(const QString &text, const char *tone);

    QLabel *m_elapsed = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_server = nullptr;
    QLabel *m_transport = nullptr;
    PowerButton *m_button = nullptr;
    QTimer *m_ticker = nullptr;
    // Сторожевой таймер: ядро может не ответить, и кольцо крутилось бы вечно.
    QTimer *m_pendingGuard = nullptr;
    QDateTime m_connectedAt;
    bool m_pending = false;
};
