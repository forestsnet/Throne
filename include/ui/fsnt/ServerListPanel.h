#pragma once

#include <QSet>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class BusyButton;
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
    };

    // Избранное хранится списком id в настройках, а не полем профиля:
    // так не приходится трогать позиционные запросы ProfilesRepo,
    // которые правит upstream.
    static QSet<int> favorites();
    static void toggleFavorite(int profileId);

    void reloadGroups();
    void reloadServers();

    // Подсветить сервер, который пойдёт в дело. Вызывает окно, когда панель
    // подключения сообщила о своём выборе.
    void selectProfile(int profileId);

    // Запускает замер пинга по текущей подписке и обновляет список,
    // пока результаты приходят.
    void measureLatency();

signals:
    void serverActivated(int profileId);
    // Пользователь выбрал строку. Одного клика достаточно: запуск остаётся
    // за кнопкой включения и двойным щелчком.
    void serverSelected(int profileId);
    // Окно владеет диалогом добавления: он нужен ещё и панели подключения.
    void addSubscriptionRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyFilter(const QString &text);
    void setShowFavouritesOnly(bool onlyFavourites);
    // Пустой список без объяснения читается как поломка, поэтому вместо него
    // показываем призыв добавить подписку.
    void updateEmptyState();
    // Замер закончен, когда не осталось профилей с latency == 0 («не измерялся»).
    bool allMeasured() const;
    void updateSubscription();

    bool m_favouritesOnly = false;
    QPushButton *m_tabAll = nullptr;
    QPushButton *m_tabFav = nullptr;
    BusyButton *m_ping = nullptr;
    BusyButton *m_updateSub = nullptr;
    QTimer *m_latencyPoll = nullptr;
    int m_latencyPollsLeft = 0;

    QComboBox *m_groups = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_addSub = nullptr;
    QWidget *m_empty = nullptr;
    QLabel *m_emptyText = nullptr;
};
