#include "include/ui/fsnt/SubscriptionCard.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QRegularExpression>
#include <QUrl>
#include <QVBoxLayout>

#include "include/configs/sub/ProviderPolicy.hpp"
#include "include/configs/sub/SubscriptionUsage.hpp"
#include "include/database/GroupsRepo.h"
#include "include/global/Configs.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/fsnt/FsntControls.h"
#include "include/ui/fsnt/FsntTheme.hpp"

namespace {
    // Сколько места отдаём объявлению провайдера. Считаем в точках, а не в
    // строках: у одного провайдера текст сплошной, у другого разбит на короткие
    // строчки, и по их числу коробка выходила то пустой, то во весь экран.
    constexpr int kAnnounceMaxHeight = 150;
}

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

    // Ссылки провайдера — значками в той же строке, что и название подписки:
    // отдельной строкой внизу они забирали место у объявления провайдера и
    // висели в пустоте. Самолётик ведёт в Telegram, шар — на сайт.
    const auto makeLink = [this, header](const QString *url) {
        auto *button = new FsntIconButton(Fsnt::Glyph::Globe, this);
        button->setFlat(true);
        button->setFixedSize(26, 26);
        button->setGlyphInset(5.0);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &FsntIconButton::clicked, this, [url] {
            if (!url->isEmpty()) QDesktopServices::openUrl(QUrl(*url));
        });
        header->addSpacing(2);
        header->addWidget(button);
        return button;
    };
    m_supportLink = makeLink(&m_supportUrl);
    m_pageLink = makeLink(&m_pageUrl);

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

    m_announceMore = new QLabel(this);
    m_announceMore->setObjectName("fsntSubMeta");
    m_announceMore->setTextFormat(Qt::RichText);
    m_announceMore->setOpenExternalLinks(false);
    m_announceMore->setVisible(false);
    connect(m_announceMore, &QLabel::linkActivated, this, [this] {
        Fsnt::Notice(GetMessageBoxParent(), tr("Message from your provider"), m_announceFull);
    });
    layout->addWidget(m_announceMore);


    refresh();
}

void SubscriptionCard::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateAnnounce();
}

void SubscriptionCard::updateAnnounce() {
    if (m_announce == nullptr) return;
    if (m_announceFull.isEmpty()) {
        m_announce->hide();
        m_announceMore->hide();
        return;
    }
    m_announce->show();

    // Пока карточку не разложили, ширины ещё нет — берём ширину самой карточки
    // за вычетом полей, иначе первый показ обрезал бы текст наугад.
    const int width = qMax(160, m_announce->width() > 0 ? m_announce->width() : this->width() - 28);
    const QFontMetrics metrics(m_announce->font());
    const auto heightOf = [&](const QString &text) {
        return metrics.boundingRect(QRect(0, 0, width, 10000), Qt::TextWordWrap, text).height();
    };

    if (heightOf(m_announceFull) <= kAnnounceMaxHeight) {
        m_announce->setText(m_announceFull);
        m_announceMore->hide();
        return;
    }

    // Двоичным поиском — самый длинный кусок, который ещё влезает в отведённую
    // высоту. Перебор по одному символу на длинном объявлении заметен глазу.
    int low = 0;
    int high = m_announceFull.size();
    while (low < high) {
        const int mid = (low + high + 1) / 2;
        if (heightOf(m_announceFull.left(mid) + QStringLiteral("…")) <= kAnnounceMaxHeight) low = mid;
        else high = mid - 1;
    }
    QString head = m_announceFull.left(low);
    // Рвать слово посередине некрасиво, поэтому отступаем до пробела.
    const int lastSpace = head.lastIndexOf(QRegularExpression(QStringLiteral("\\s")));
    if (lastSpace > low / 2) head.truncate(lastSpace);
    m_announce->setText(head.trimmed() + QStringLiteral("…"));
    m_announceMore->show();
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

    // Объявление провайдера: помещается — показываем целиком, не помещается —
    // ровно столько, сколько влезает, остальное открывается окном.
    m_announceFull = policy.announce;
    updateAnnounce();

    // Значок выбираем по адресу, а не по назначению ссылки: у одного
    // провайдера поддержка живёт в Telegram, у другого — на сайте.
    const auto isTelegram = [](const QString &url) {
        const QString host = QUrl(url).host();
        return url.startsWith("tg://") || host.endsWith("t.me") || host.endsWith("telegram.me") ||
               host.endsWith("telegram.org") || host.endsWith("telegram.dog");
    };
    const auto applyLink = [&isTelegram](FsntIconButton *button, QString &store, const QString &url,
                                         const QString &hint) {
        store = url;
        const bool has = !url.isEmpty();
        button->setVisible(has);
        if (!has) return;
        button->setGlyph(isTelegram(url) ? Fsnt::Glyph::Telegram : Fsnt::Glyph::Globe);
        button->setToolTip(hint + "\n" + url);
    };
    applyLink(m_supportLink, m_supportUrl, policy.supportUrl, tr("Support"));
    applyLink(m_pageLink, m_pageUrl, policy.webPageUrl, tr("Subscription page"));

}
