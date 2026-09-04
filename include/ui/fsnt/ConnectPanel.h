#pragma once

#include <QDateTime>
#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;

// Правая панель: состояние подключения, таймер, кнопка и текущий сервер.
// Действия выполняет MainWindow — он движок; панель только показывает и просит.
class ConnectPanel : public QWidget {
    Q_OBJECT

public:
    explicit ConnectPanel(QWidget *parent = nullptr);

    // Вызывается окном, когда ядро сообщило о смене состояния.
    void refresh();

private:
    static bool isConnected();
    static int profileToStart();

    void onButtonClicked();
    void updateElapsed();

    QLabel *m_elapsed = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_server = nullptr;
    QPushButton *m_button = nullptr;
    QTimer *m_ticker = nullptr;
    QDateTime m_connectedAt;
};
