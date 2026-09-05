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
    // Привести туннель в нужное состояние. Уже в нём — ничего не делаем:
    // повторное нажатие кнопки переключило бы его в обратную сторону.
    void requestConnection(bool wantConnect);
    // Цель для экскурсии.
    QWidget *powerButton() const;
    // Переключить туннель на другой сервер. На отключённом — ничего: выбор
    // запомнится и применится при следующем подключении.
    void switchServer(int profileId);

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
    // Начало и конец ожидания ответа ядра — в одном месте, чтобы сторожевой
    // таймер и опрос состояния не забывались по отдельности.
    void beginPending(bool stopping, const QString &status);
    void endPending();

    QLabel *m_elapsed = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_server = nullptr;
    QLabel *m_transport = nullptr;
    PowerButton *m_button = nullptr;
    QTimer *m_ticker = nullptr;
    // Сторожевой таймер: ядро может не ответить, и кольцо крутилось бы вечно.
    QTimer *m_pendingGuard = nullptr;
    // Опрос настоящего состояния, пока идёт операция. Панель узнаёт о ядре
    // только из MW_dialog_message, а на остановку сообщение приходит не всегда —
    // без опроса «Отключаемся» висело до сторожевого таймера.
    QTimer *m_statePoll = nullptr;
    QDateTime m_connectedAt;
    bool m_pending = false;
};
