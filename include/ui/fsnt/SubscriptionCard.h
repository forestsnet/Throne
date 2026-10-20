#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class FsntIconButton;
class QProgressBar;

// Карточка активной подписки: трафик, срок, сброс и ссылки поддержки.
// Данные берутся из group->info (subscription-userinfo) и политики провайдера.
class SubscriptionCard : public QWidget {
    Q_OBJECT

public:
    explicit SubscriptionCard(QWidget *parent = nullptr);

    void refresh();

protected:
    // Ширина карточки меняется вместе с окном, а от неё зависит, сколько строк
    // объявления влезает. Пересчитываем по месту.
    void resizeEvent(QResizeEvent *event) override;

private:
    // Показать объявление ровно на ту высоту, что ему отведена: не короче, если
    // текст помещается, и не длиннее, если нет.
    void updateAnnounce();

    QLabel *m_name = nullptr;
    QLabel *m_expiry = nullptr;
    QProgressBar *m_bar = nullptr;
    QLabel *m_traffic = nullptr;
    QLabel *m_refill = nullptr;
    QLabel *m_announce = nullptr;
    QLabel *m_announceMore = nullptr;
    QString m_announceFull;
    // Ссылки провайдера — значками: подпись «Поддержка · Страница подписки»
    // занимала строку и всё равно не говорила, куда именно ведёт ссылка.
    FsntIconButton *m_supportLink = nullptr;
    FsntIconButton *m_pageLink = nullptr;
    QString m_supportUrl;
    QString m_pageUrl;
};
