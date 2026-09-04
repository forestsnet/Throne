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

signals:
    // Нечего запускать: окно предложит добавить подписку.
    void subscriptionNeeded();

private:
    static bool isConnected();
    static int profileToStart();

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
