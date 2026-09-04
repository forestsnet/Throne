#include "include/ui/fsnt/ServerListPanel.h"

#include <QComboBox>
#include <QAction>
#include <QMouseEvent>
#include <QTimer>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QScrollBar>
#include <QVBoxLayout>

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/global/Configs.hpp"
#include "include/ui/fsnt/BusyButton.h"
#include "include/ui/fsnt/FsntControls.h"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/configs/sub/ProviderPolicy.hpp"
#include "include/ui/mainwindow.h"
#include "src/ui/fsnt/ServerItemDelegate.h"

ServerListPanel::ServerListPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *groupRow = new QHBoxLayout;
    groupRow->setSpacing(6);

    m_groups = new FsntSelect(this);
    groupRow->addWidget(m_groups, 1);

    m_addSub = new QPushButton("+", this);
    m_addSub->setObjectName("fsntIconSquare");
    m_addSub->setFixedSize(36, 36);
    m_addSub->setCursor(Qt::PointingHandCursor);
    m_addSub->setToolTip(tr("Add subscription"));
    connect(m_addSub, &QPushButton::clicked, this, &ServerListPanel::addSubscriptionRequested);
    groupRow->addWidget(m_addSub);

    m_updateSub = new BusyButton("⟳", this);
    m_updateSub->setObjectName("fsntIconSquare");
    m_updateSub->setFixedSize(36, 36);
    m_updateSub->setCursor(Qt::PointingHandCursor);
    m_updateSub->setToolTip(tr("Update subscription"));
    connect(m_updateSub, &QPushButton::clicked, this, &ServerListPanel::updateSubscription);
    groupRow->addWidget(m_updateSub);

    layout->addLayout(groupRow);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(6);

    m_search = new QLineEdit(this);
    m_search->setObjectName("fsntSearch");
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    searchRow->addWidget(m_search, 1);

    m_ping = new BusyButton("⚡", this);
    m_ping->setObjectName("fsntIconSquare");
    m_ping->setFixedSize(36, 36);
    m_ping->setCursor(Qt::PointingHandCursor);
    m_ping->setToolTip(tr("Measure latency"));
    searchRow->addWidget(m_ping);

    layout->addLayout(searchRow);

    // Полоса-подложка: без неё две пилюли висят в пустоте и не читаются как переключатель.
    auto *tabStrip = new QWidget(this);
    tabStrip->setObjectName("fsntTabStrip");
    auto *tabs = new QHBoxLayout(tabStrip);
    tabs->setContentsMargins(3, 3, 3, 3);
    tabs->setSpacing(3);
    m_tabAll = new QPushButton(tr("All"), tabStrip);
    m_tabFav = new QPushButton(tr("Favorites"), tabStrip);
    for (auto *tab : {m_tabAll, m_tabFav}) {
        tab->setObjectName("fsntTab");
        tab->setCheckable(true);
        tab->setCursor(Qt::PointingHandCursor);
        tabs->addWidget(tab, 1);
    }
    m_tabAll->setChecked(true);
    layout->addWidget(tabStrip);

    m_list = new QListWidget(this);
    m_list->setObjectName("fsntServerList");
    m_list->setItemDelegate(new ServerItemDelegate(m_list));
    m_list->setMouseTracking(true);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_list, 1);

    m_empty = new QWidget(this);
    auto *emptyLayout = new QVBoxLayout(m_empty);
    emptyLayout->setContentsMargins(24, 24, 24, 24);
    emptyLayout->setSpacing(12);
    emptyLayout->addStretch();

    m_emptyText = new QLabel(m_empty);
    m_emptyText->setObjectName("fsntPlaceholder");
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setWordWrap(true);
    emptyLayout->addWidget(m_emptyText);

    auto *emptyAdd = new QPushButton(tr("Add subscription"), m_empty);
    emptyAdd->setObjectName("fsntPrimary");
    emptyAdd->setCursor(Qt::PointingHandCursor);
    connect(emptyAdd, &QPushButton::clicked, this, &ServerListPanel::addSubscriptionRequested);

    auto *emptyRow = new QHBoxLayout;
    emptyRow->addStretch();
    emptyRow->addWidget(emptyAdd);
    emptyRow->addStretch();
    emptyLayout->addLayout(emptyRow);
    emptyLayout->addStretch();

    m_empty->hide();
    layout->addWidget(m_empty, 1);

    connect(m_search, &QLineEdit::textChanged, this, &ServerListPanel::applyFilter);
    connect(m_groups, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) return;
        const int gid = m_groups->itemData(index).toInt();
        Configs::dataManager->settingsRepo->current_group = gid;
        Configs::dataManager->settingsRepo->Save();
        reloadServers();
    });
    // Клик в правой зоне строки переключает избранное, не запуская сервер.
    m_list->viewport()->installEventFilter(this);

    m_latencyPoll = new QTimer(this);
    m_latencyPoll->setInterval(2000);
    connect(m_latencyPoll, &QTimer::timeout, this, [this] {
        reloadServers();
        // Счётчик остаётся верхней границей: если часть серверов недоступна,
        // их latency так и не станет ненулевым, и по факту мы не остановимся.
        if (allMeasured() || --m_latencyPollsLeft <= 0) {
            m_latencyPoll->stop();
            m_ping->setBusy(false);
        }
    });

    connect(m_ping, &QPushButton::clicked, this, &ServerListPanel::measureLatency);
    connect(m_tabAll, &QPushButton::clicked, this, [this] { setShowFavouritesOnly(false); });
    connect(m_tabFav, &QPushButton::clicked, this, [this] { setShowFavouritesOnly(true); });
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item != nullptr) emit serverActivated(item->data(ProfileIdRole).toInt());
    });
    connect(m_list, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
        if (item == nullptr) return;
        const int id = item->data(ProfileIdRole).toInt();
        // Запоминаем именно выбор, а не запуск: remember_id управляет автостартом,
        // и писать туда по клику значило бы подключаться при следующем старте.
        Configs::dataManager->settingsRepo->simple_selected_profile = id;
        Configs::dataManager->settingsRepo->Save();
        emit serverSelected(id);
    });

    reloadGroups();

    // subscription-ping-onopen-enabled: панель просит замерить пинги при открытии.
    // Один раз за запуск и с задержкой — ядру нужно подняться.
    const auto policy = Subscription::DeserializeProviderPolicy(
        Configs::dataManager->groupsRepo->CurrentGroup()
            ? Configs::dataManager->groupsRepo->CurrentGroup()->provider_policy_json
            : QString());
    if (policy.pingOnOpen.has_value() && policy.pingOnOpen.value()) {
        QTimer::singleShot(4000, this, &ServerListPanel::measureLatency);
    }
}



bool ServerListPanel::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_list->viewport() && event->type() == QEvent::MouseButtonRelease) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (auto *item = m_list->itemAt(mouse->pos())) {
            const QRect row = m_list->visualItemRect(item);
            if (mouse->pos().x() >= row.right() - ServerItemDelegate::kHeartZone) {
                toggleFavorite(item->data(ProfileIdRole).toInt());
                item->setData(FavoriteRole, !item->data(FavoriteRole).toBool());
                applyFilter(m_search->text());
                m_list->viewport()->update();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

QSet<int> ServerListPanel::favorites() {
    QSet<int> ids;
    const auto doc = QJsonDocument::fromJson(
        Configs::dataManager->settingsRepo->favorite_profiles.toUtf8());
    if (!doc.isArray()) return ids;
    for (const auto &value : doc.array()) ids.insert(value.toInt());
    return ids;
}

void ServerListPanel::toggleFavorite(int profileId) {
    auto ids = favorites();
    if (ids.contains(profileId)) ids.remove(profileId);
    else ids.insert(profileId);

    QJsonArray array;
    for (int id : ids) array.append(id);
    Configs::dataManager->settingsRepo->favorite_profiles =
        QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    Configs::dataManager->settingsRepo->Save();
}

void ServerListPanel::setShowFavouritesOnly(bool onlyFavourites) {
    m_favouritesOnly = onlyFavourites;
    m_tabAll->setChecked(!onlyFavourites);
    m_tabFav->setChecked(onlyFavourites);
    applyFilter(m_search->text());
}


void ServerListPanel::measureLatency() {
    auto *mw = GetMainWindow();
    if (mw == nullptr) return;

    // testRunner приватный, но действие меню — именованный дочерний объект окна.
    // Дёргаем его, чтобы не править код, который правит upstream.
    if (auto *action = mw->findChild<QAction *>("actionUrl_Test_Group")) {
        action->trigger();
        // Результаты приходят порциями и signals у TestRunner нет: обновляем список
        // несколько раз, пока идёт замер, и останавливаемся сами.
        m_latencyPollsLeft = 30;
        m_ping->setBusy(true);
        m_latencyPoll->start();
    }
}

void ServerListPanel::reloadGroups() {
    const QSignalBlocker blocker(m_groups);
    m_groups->clear();

    int current = Configs::dataManager->settingsRepo->current_group;

    // Если текущая группа пуста, а рядом есть подписка с серверами — открываем её:
    // пустой список при импортированной подписке выглядит как поломка.
    if (const auto currentGroup = Configs::dataManager->groupsRepo->GetGroup(current);
        !currentGroup || currentGroup->Profiles().isEmpty()) {
        for (int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
            const auto candidate = Configs::dataManager->groupsRepo->GetGroup(gid);
            if (candidate && !candidate->Profiles().isEmpty()) {
                current = gid;
                Configs::dataManager->settingsRepo->current_group = gid;
                Configs::dataManager->settingsRepo->Save();
                break;
            }
        }
    }

    int currentIndex = 0;
    for (int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (!group) continue;
        m_groups->addItem(group->name, gid);
        if (gid == current) currentIndex = m_groups->count() - 1;
    }
    if (m_groups->count() > 0) m_groups->setCurrentIndex(currentIndex);

    reloadServers();
}

void ServerListPanel::reloadServers() {
    // Замер обновляет список каждые две секунды — не теряем выбор и прокрутку.
    const int keepId = m_list->currentItem()
                           ? m_list->currentItem()->data(ProfileIdRole).toInt() : -1;
    const int scroll = m_list->verticalScrollBar()->value();

    m_list->clear();

    const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (!group) {
        updateEmptyState();
        return;
    }

    const auto favs = favorites();
    for (const auto &profile : Configs::dataManager->profilesRepo->GetProfileBatch(group->Profiles())) {
        if (!profile || !profile->outbound) continue;
        auto *item = new QListWidgetItem(profile->outbound->DisplayName(), m_list);
        item->setData(ProfileIdRole, profile->id);
        item->setData(LatencyRole, profile->latency);
        item->setData(FavoriteRole, favs.contains(profile->id));
    }

    applyFilter(m_search->text());
    updateEmptyState();

    if (keepId >= 0) {
        // Блокируем сигнал: восстановление прежней строки после перезагрузки
        // списка — не выбор пользователя. Без этого любой пересбор списка
        // записывался бы в simple_selected_profile и глушил автоподбор.
        const QSignalBlocker blocker(m_list);
        for (int row = 0; row < m_list->count(); ++row) {
            if (m_list->item(row)->data(ProfileIdRole).toInt() == keepId) {
                m_list->setCurrentRow(row);
                break;
            }
        }
    }
    m_list->verticalScrollBar()->setValue(scroll);
}

void ServerListPanel::applyFilter(const QString &text) {
    for (int row = 0; row < m_list->count(); ++row) {
        auto *item = m_list->item(row);
        const bool matchesText = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive);
        const bool matchesTab = !m_favouritesOnly || item->data(FavoriteRole).toBool();
        item->setHidden(!(matchesText && matchesTab));
    }
}

void ServerListPanel::updateEmptyState() {
    const bool hasServers = m_list->count() > 0;
    m_list->setVisible(hasServers);
    m_empty->setVisible(!hasServers);
    if (hasServers) return;

    // Различаем два разных «пусто»: подписки вообще нет и подписка есть, но пустая.
    const bool hasGroups = m_groups->count() > 0;
    m_emptyText->setText(hasGroups
        ? tr("This subscription has no servers yet. Refresh it or add another one.")
        : tr("No subscription yet.\nPaste the link your provider gave you and the servers will appear here."));
}

bool ServerListPanel::allMeasured() const {
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(LatencyRole).toInt() == 0) return false;
    }
    return m_list->count() > 0;
}

void ServerListPanel::updateSubscription() {
    const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (!group || group->url.isEmpty()) return;

    m_updateSub->setBusy(true);
    // Колбэк приходит из рабочего потока обновлятора — возвращаемся в UI.
    Subscription::updater()->RefreshGroup(group->id, [this] {
        QMetaObject::invokeMethod(this, [this] {
            m_updateSub->setBusy(false);
            reloadServers();
        }, Qt::QueuedConnection);
    }, true);
}

void ServerListPanel::selectProfile(const int profileId) {
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(ProfileIdRole).toInt() != profileId) continue;
        if (m_list->currentRow() == row) return;
        // Блокируем сигнал: это не выбор пользователя, и записывать его
        // в simple_selected_profile нельзя — иначе автоподбор станет ручным.
        const QSignalBlocker blocker(m_list);
        m_list->setCurrentRow(row);
        return;
    }
}
