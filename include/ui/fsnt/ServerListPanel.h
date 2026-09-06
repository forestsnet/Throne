#pragma once

#include <QHash>
#include <QSet>

#include <memory>

namespace Configs { class Profile; }
#include <QWidget>

class FsntSelect;
class QLabel;
class QLineEdit;
class QListWidget;
class BusyButton;
class FsntIconButton;
class QPushButton;
class QTimer;
class QWidget;

// Левая панель: переключатель подписок, поиск и список серверов.
class ServerListPanel : public QWidget {
    Q_OBJECT

public:
    explicit ServerListPanel(QWidget *parent = nullptr);

    // Роли данных элемента списка.
    enum Roles {
        ProfileIdRole = Qt::UserRole + 1,
        LatencyRole,
        FavoriteRole,
        // Замер запущен, а этот сервер ещё не ответил.
        MeasuringRole,
        // Техническая подпись: протокол, транспорт, шифрование, полный JSON.
        SubtitleRole,
        // Соединение без шифрования — подпись красится тревожным цветом.
        InsecureRole,
    };

    // Избранное хранится списком имён в настройках, а не полем профиля:
    // так не приходится трогать позиционные запросы ProfilesRepo, которые
    // правит upstream. Имена, а не id: при sub_clear обновление подписки
    // пересоздаёт все профили, и id живут только до следующего обновления.
    static QSet<QString> favorites();
    static void toggleFavorite(const QString &serverName);

    void reloadGroups();
    void reloadServers();

    // Подсветить сервер, который пойдёт в дело. Вызывает окно, когда панель
    // подключения сообщила о своём выборе.
    void selectProfile(int profileId);

    // Запускает замер пинга по текущей подписке и обновляет список,
    // пока результаты приходят.
    void measureLatency();

    // Останавливает начатый замер. Список из сотни серверов идёт долго, и
    // человеку нужен способ передумать, не закрывая клиент.
    void stopMeasurement();

signals:
    void serverActivated(int profileId);
    // Пользователь выбрал строку. Одного клика достаточно: запуск остаётся
    // за кнопкой включения и двойным щелчком.
    void serverSelected(int profileId);
    // Короткая строка для всплывающего уведомления окна.
    void notice(const QString &text);
    // Окно владеет диалогом добавления: он нужен ещё и панели подключения.
    void addSubscriptionRequested();
    // Список подписок перестроен. Окно смотрит по этому сигналу, не пора ли
    // рассказать про переключение: подписка приходит пустой и наполняется
    // позже, поэтому одного сообщения о новой группе мало.
    void groupsReloaded();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyFilter(const QString &text);
    void setShowFavouritesOnly(bool onlyFavourites);
    // Пустой список без объяснения читается как поломка, поэтому вместо него
    // показываем призыв добавить подписку.
    void updateEmptyState();
    // Замер закончен, когда не осталось профилей с latency == 0 («не измерялся»).
    // Имена серверов группы — для сравнения состава до и после обновления.
    static QSet<QString> serverNames(const QList<int> &ids);

    // Подпись под именем сервера: описание от провайдера, если оно есть,
    // иначе техническая строка. Результат кешируется: список пересобирается
    // каждые две секунды во время замера, а разбор конфига стоит дорого.
    QString subtitleFor(const std::shared_ptr<Configs::Profile> &profile);
    bool allMeasured() const;
    int answeredCount() const;
    void finishMeasurement();
    // Сколько опросов подряд не принесли ни одного ответа. Пока ядро ждёт
    // ответа в модальном окне (например требует гео-файлы), замер не идёт
    // вовсе, и крутить точки минуту бессмысленно.
    int m_measureIdlePolls = 0;
    void updateSubscription();
    // Меню по правой кнопке на строке сервера: проверить его одного, и на
    // выбор — чем именно проверять.
    void showServerMenu(const QPoint &where);
    void probeOne(int profileId, int kind);

public:
    // Цели для экскурсии. Возвращаем QWidget*, потому что поднимать тип до
    // базового в заголовке нельзя: FsntIconButton здесь только объявлен.
    QWidget *addSubscriptionButton() const;
    // Не сам QListWidget: на чистой установке он скрыт, а вместо него показана
    // заглушка. Экскурсия должна указывать на то место, где серверы появятся,
    // а скрытый виджет подсветить нельзя.
    QWidget *listArea() const;
    // Переключатель подписок: на него показывает подсказка, когда подписок
    // стало больше одной.
    QWidget *subscriptionSelector() const;

private:

    bool m_favouritesOnly = false;
    QPushButton *m_tabAll = nullptr;
    QPushButton *m_tabFav = nullptr;
    BusyButton *m_ping = nullptr;
    BusyButton *m_updateSub = nullptr;
    QTimer *m_latencyPoll = nullptr;
    int m_latencyPollsLeft = 0;
    // Момент запуска замера в секундах. Строка считается ждущей, пока её
    // latency_at старше этой отметки.
    qint64 m_measureStartedAt = 0;
    // Проверка одного сервера: думающие точки положены только ему, а итог —
    // это его собственный ответ, а не «столько-то из двадцати».
    int m_probeTarget = -1;
    // Перерисовка «думающих» точек: делегат берёт фазу из часов, состояние
    // хранить не нужно, но кто-то должен будить viewport.
    QTimer *m_measureRepaint = nullptr;
    QHash<int, QString> m_subtitleCache;

    FsntSelect *m_groups = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    FsntIconButton *m_addSub = nullptr;
    QWidget *m_empty = nullptr;
    QLabel *m_emptyText = nullptr;
};
