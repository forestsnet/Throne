#pragma once

#include <QDateTime>
#include <QWidget>

#include "include/engine/EngineTypes.h"

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

    // Ответы движка о конце операции. До них панель ждала, пока настоящее
    // состояние само станет желаемым, а неудавшийся запуск такого состояния не
    // даёт никогда: кольцо крутилось все 25 секунд сторожевого таймера, хотя
    // отказ был известен сразу — иногда вместе с окном об ошибке поверх него.
    void onStartFinished(int profileId, Engine::StartOutcome outcome, const QString &error);
    void onStopFinished();
    // Туннель поднимает не только эта кнопка: лоток, автозапуск при старте,
    // повтор после догрузки гео-файлов, внешнее управление. Кольцо должно
    // показывать работу, кто бы её ни начал.
    void onConnectionState(Engine::ConnectionState state);

signals:
    // Нечего запускать: окно предложит добавить подписку.
    void subscriptionNeeded();
    // Какой сервер пойдёт в дело. Список подсвечивает его, чтобы подпись под
    // кнопкой и выделенная строка всегда показывали одно и то же.
    void profileResolved(int profileId);
    // Туннель поднялся или лёг. Панель и так опрашивает состояние; окну нужен
    // сам переход, а не текущее значение.
    void connectionChanged(bool connected, const QString &server);

private:
    // Какой сервер запускать и выбран ли он пользователем.
    struct Choice {
        int id = -1;
        bool automatic = true;
    };

    static bool isConnected();
    static Engine::ConnectionState tunnelState();
    static Choice resolveProfile();

    void onButtonClicked();
    // Что делать, когда прав на туннель не дали.
    void offerWayWithoutRights(int profileId);
    void updateElapsed();
    void setStatus(const QString &text, const char *tone);
    // Начало и конец ожидания ответа ядра — в одном месте, чтобы сторожевой
    // таймер и опрос состояния не забывались по отдельности.
    void beginPending(bool stopping, const QString &status, int profileId = -1);
    void endPending();
    // Короткая причина отказа под кнопкой. Подробности, где они есть, клиент
    // показывает своим окном, здесь нужна одна строка.
    static QString startFailureText(Engine::StartOutcome outcome);

    QLabel *m_elapsed = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_server = nullptr;
    QLabel *m_transport = nullptr;
    PowerButton *m_button = nullptr;
    QTimer *m_ticker = nullptr;
    // -1 — состояние ещё не знаем: на старте уведомлять не о чем.
    int m_lastConnected = -1;
    // Сторожевой таймер: ядро может не ответить, и кольцо крутилось бы вечно.
    QTimer *m_pendingGuard = nullptr;
    // Опрос настоящего состояния, пока идёт операция. Панель узнаёт о ядре
    // только из MW_dialog_message, а на остановку сообщение приходит не всегда —
    // без опроса «Отключаемся» висело до сторожевого таймера.
    QTimer *m_statePoll = nullptr;
    QDateTime m_connectedAt;
    bool m_pending = false;
    // Причина, по которой не удалось подключиться. Держим отдельно: состояние
    // «не подключено» приходит и само по себе, и refresh затирал бы причину
    // словом «Отключено» через долю секунды после того, как её показали.
    // Снимается следующей попыткой или удавшимся подключением.
    QString m_failure;
    // Чей запуск ждём. -1 — ждём остановки или не знаем.
    int m_pendingProfileId = -1;
};
