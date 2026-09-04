#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;

// Карточка активной подписки: трафик, срок, сброс и ссылки поддержки.
// Данные берутся из group->info (subscription-userinfo) и политики провайдера.
class SubscriptionCard : public QWidget {
    Q_OBJECT

public:
    explicit SubscriptionCard(QWidget *parent = nullptr);

    void refresh();

private:
    QLabel *m_name = nullptr;
    QLabel *m_expiry = nullptr;
    QProgressBar *m_bar = nullptr;
    QLabel *m_traffic = nullptr;
    QLabel *m_refill = nullptr;
    QLabel *m_announce = nullptr;
    QLabel *m_links = nullptr;
};
