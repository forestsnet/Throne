#pragma once

#include <QSet>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTimer;

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

    // Запускает замер пинга по текущей подписке и обновляет список,
    // пока результаты приходят.
    void measureLatency();

signals:
    void serverActivated(int profileId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyFilter(const QString &text);
    void setShowFavouritesOnly(bool onlyFavourites);

    bool m_favouritesOnly = false;
    QPushButton *m_tabAll = nullptr;
    QPushButton *m_tabFav = nullptr;
    QPushButton *m_refresh = nullptr;
    QTimer *m_latencyPoll = nullptr;
    int m_latencyPollsLeft = 0;

    QComboBox *m_groups = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
};
