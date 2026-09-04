#include "include/ui/fsnt/ServerListPanel.h"

#include <QComboBox>
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
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item != nullptr) emit serverActivated(item->data(ProfileIdRole).toInt());
    });

    reloadGroups();
}

void ServerListPanel::reloadGroups() {
    const QSignalBlocker blocker(m_groups);
    m_groups->clear();

    const int current = Configs::dataManager->settingsRepo->current_group;
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

    for (const auto &profile : Configs::dataManager->profilesRepo->GetProfileBatch(group->Profiles())) {
        if (!profile || !profile->outbound) continue;
        auto *item = new QListWidgetItem(profile->outbound->DisplayName(), m_list);
        item->setData(ProfileIdRole, profile->id);
        item->setData(LatencyRole, profile->latency);
    }

    applyFilter(m_search->text());
}

void ServerListPanel::applyFilter(const QString &text) {
    for (int row = 0; row < m_list->count(); ++row) {
        auto *item = m_list->item(row);
        const bool visible = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive);
        item->setHidden(!visible);
    }
}
