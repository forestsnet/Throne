#include "include/ui/fsnt/ServerListPanel.h"

#include <QComboBox>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/global/Configs.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "src/ui/fsnt/ServerItemDelegate.h"

ServerListPanel::ServerListPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_groups = new QComboBox(this);
    m_groups->setObjectName("fsntGroupSwitch");
    layout->addWidget(m_groups);

    m_search = new QLineEdit(this);
    m_search->setObjectName("fsntSearch");
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    auto *tabs = new QHBoxLayout;
    tabs->setSpacing(4);
    m_tabAll = new QPushButton(tr("All"), this);
    m_tabFav = new QPushButton(tr("Favorites"), this);
    for (auto *tab : {m_tabAll, m_tabFav}) {
        tab->setObjectName("fsntTab");
        tab->setCheckable(true);
        tab->setCursor(Qt::PointingHandCursor);
        tabs->addWidget(tab);
    }
    m_tabAll->setChecked(true);
    layout->addLayout(tabs);

    m_list = new QListWidget(this);
    m_list->setObjectName("fsntServerList");
    m_list->setItemDelegate(new ServerItemDelegate(m_list));
    m_list->setMouseTracking(true);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_list, 1);

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

    connect(m_tabAll, &QPushButton::clicked, this, [this] { setShowFavouritesOnly(false); });
    connect(m_tabFav, &QPushButton::clicked, this, [this] { setShowFavouritesOnly(true); });
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item != nullptr) emit serverActivated(item->data(ProfileIdRole).toInt());
    });

    reloadGroups();
}



bool ServerListPanel::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_list->viewport() && event->type() == QEvent::MouseButtonRelease) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (auto *item = m_list->itemAt(mouse->pos())) {
            const QRect row = m_list->visualItemRect(item);
            if (mouse->pos().x() >= row.right() - 28) {
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
    m_list->clear();

    const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (!group) return;

    const auto favs = favorites();
    for (const auto &profile : Configs::dataManager->profilesRepo->GetProfileBatch(group->Profiles())) {
        if (!profile || !profile->outbound) continue;
        auto *item = new QListWidgetItem(profile->outbound->DisplayName(), m_list);
        item->setData(ProfileIdRole, profile->id);
        item->setData(LatencyRole, profile->latency);
        item->setData(FavoriteRole, favs.contains(profile->id));
    }

    applyFilter(m_search->text());
}

void ServerListPanel::applyFilter(const QString &text) {
    for (int row = 0; row < m_list->count(); ++row) {
        auto *item = m_list->item(row);
        const bool matchesText = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive);
        const bool matchesTab = !m_favouritesOnly || item->data(FavoriteRole).toBool();
        item->setHidden(!(matchesText && matchesTab));
    }
}
