#pragma once

#include <QWidget>

class QComboBox;
class QLineEdit;
class QListWidget;

// Левая панель: переключатель подписок, поиск и список серверов.
class ServerListPanel : public QWidget {
    Q_OBJECT

public:
    explicit ServerListPanel(QWidget *parent = nullptr);

    // Роли данных элемента списка.
    enum Roles {
        ProfileIdRole = Qt::UserRole + 1,
        LatencyRole,
    };

    void reloadGroups();
    void reloadServers();

signals:
    void serverActivated(int profileId);

private:
    void applyFilter(const QString &text);

    QComboBox *m_groups = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
};
