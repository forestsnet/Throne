#include "include/ui/fsnt/SubscriptionCard.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QUrl>
#include <QVBoxLayout>

#include "include/configs/sub/ProviderPolicy.hpp"
#include "include/configs/sub/SubscriptionUsage.hpp"
#include "include/database/GroupsRepo.h"
#include "include/global/Configs.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"

SubscriptionCard::SubscriptionCard(QWidget *parent) : QWidget(parent) {
    setObjectName("fsntSubscriptionCard");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 13, 14, 13);
    layout->setSpacing(8);

    auto *header = new QHBoxLayout;
    m_name = new QLabel(this);
    m_name->setObjectName("fsntSubName");
    header->addWidget(m_name);
    header->addStretch();
    m_expiry = new QLabel(this);
    m_expiry->setObjectName("fsntSubMeta");
    header->addWidget(m_expiry);

    layout->addLayout(header);

    m_bar = new QProgressBar(this);
    m_bar->setObjectName("fsntTrafficBar");
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(7);
    m_bar->setRange(0, 1000);
    layout->addWidget(m_bar);

    m_traffic = new QLabel(this);
    m_traffic->setObjectName("fsntSubStrong");
    layout->addWidget(m_traffic);

    m_refill = new QLabel(this);
    m_refill->setObjectName("fsntSubMeta");
    layout->addWidget(m_refill);

    m_announce = new QLabel(this);
    m_announce->setObjectName("fsntAnnounce");
    m_announce->setWordWrap(true);
    layout->addWidget(m_announce);

    m_links = new QLabel(this);
    m_links->setObjectName("fsntSubMeta");
    m_links->setTextFormat(Qt::RichText);
    m_links->setOpenExternalLinks(false);
    connect(m_links, &QLabel::linkActivated, this,
            [](const QString &url) { QDesktopServices::openUrl(QUrl(url)); });
    layout->addWidget(m_links);

    refresh();
}

void SubscriptionCard::refresh() {
    const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (!group) {
        setVisible(false);
        return;
    }
    setVisible(true);

    const auto usage = Subscription::ParseSubscriptionUserInfo(group->info);
    const auto policy = Subscription::DeserializeProviderPolicy(group->provider_policy_json);

    m_name->setText(policy.title.isEmpty() ? group->name : policy.title);

    // Срок прячем, если провайдер объявил подписку бессрочной.
    if (usage.hasExpiry()) {
        m_expiry->setText(tr("until %1").arg(
            QDateTime::fromSecsSinceEpoch(usage.expire).date().toString("dd.MM.yyyy")));
        m_expiry->setVisible(true);
    } else {
        m_expiry->setVisible(false);
    }

    if (!usage.valid) {
        m_bar->setVisible(false);
        m_traffic->setText(tr("No traffic data yet"));
    } else if (usage.unlimited()) {
        m_bar->setVisible(false);
        m_traffic->setText(tr("%1 used, unlimited").arg(ReadableSize(usage.used())));
    } else {
        m_bar->setVisible(true);
        const double share = static_cast<double>(usage.used()) / static_cast<double>(usage.total);
        m_bar->setValue(qBound(0, static_cast<int>(share * 1000.0), 1000));
        m_traffic->setText(tr("%1 of %2").arg(ReadableSize(usage.used()), ReadableSize(usage.total)));
    }

    // Периода сброса в протоколе нет — только дата. Показываем её и остаток дней.
    if (policy.refillDate > 0) {
        const auto date = QDateTime::fromSecsSinceEpoch(policy.refillDate);
        const qint64 days = QDateTime::currentDateTime().daysTo(date);
        m_refill->setText(days >= 0
            ? tr("Traffic resets on %1, in %2 day(s)").arg(date.date().toString("dd.MM.yyyy")).arg(days)
            : tr("Traffic resets on %1").arg(date.date().toString("dd.MM.yyyy")));
        m_refill->setVisible(true);
    } else {
        m_refill->setVisible(false);
    }

    // Объявление провайдера: показываем как есть, это его текст для клиента.
    m_announce->setText(policy.announce);
    m_announce->setVisible(!policy.announce.isEmpty());

    QStringList links;
    if (!policy.supportUrl.isEmpty()) {
        links << QString("<a href=\"%1\">%2</a>").arg(policy.supportUrl, tr("Support"));
    }
    if (!policy.webPageUrl.isEmpty()) {
        links << QString("<a href=\"%1\">%2</a>").arg(policy.webPageUrl, tr("Subscription page"));
    }
    m_links->setText(links.join(" · "));
    m_links->setVisible(!links.isEmpty());
}
