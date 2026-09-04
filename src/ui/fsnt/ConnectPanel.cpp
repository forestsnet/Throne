#include "include/ui/fsnt/ConnectPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "include/database/ProfilesRepo.h"
#include "include/database/GroupsRepo.h"
#include "include/global/Configs.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/ui/mainwindow.h"

namespace {
    constexpr int kButtonSize = 120;
}

ConnectPanel::ConnectPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addStretch();

    m_elapsed = new QLabel("00:00:00", this);
    m_elapsed->setObjectName("fsntElapsed");
    m_elapsed->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_elapsed);

    m_status = new QLabel(tr("Disconnected"), this);
    m_status->setObjectName("fsntStatus");
    m_status->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_status);

    layout->addSpacing(12);

    m_button = new QPushButton(this);
    m_button->setObjectName("fsntPowerButton");
    m_button->setFixedSize(kButtonSize, kButtonSize);
    m_button->setCursor(Qt::PointingHandCursor);
    m_button->setText("⏻");
    connect(m_button, &QPushButton::clicked, this, &ConnectPanel::onButtonClicked);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    buttonRow->addWidget(m_button);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    layout->addSpacing(12);

    m_server = new QLabel(tr("No server selected"), this);
    m_server->setObjectName("fsntCurrentServer");
    m_server->setAlignment(Qt::AlignCenter);
    m_server->setWordWrap(true);
    layout->addWidget(m_server);

    layout->addStretch();

    m_ticker = new QTimer(this);
    m_ticker->setInterval(1000);
    connect(m_ticker, &QTimer::timeout, this, &ConnectPanel::updateElapsed);

    refresh();
}

bool ConnectPanel::isConnected() {
    const auto &settings = Configs::dataManager->settingsRepo;
    return settings->core_running && settings->started_id >= 0;
}

int ConnectPanel::profileToStart() {
    const auto &settings = Configs::dataManager->settingsRepo;
    if (settings->started_id >= 0) return settings->started_id;

    // Ни один профиль не запущен: берём первый из текущей подписки.
    if (const auto group = Configs::dataManager->groupsRepo->CurrentGroup()) {
        const auto profiles = group->Profiles();
        if (!profiles.isEmpty()) return profiles.first();
    }
    return -1;
}

void ConnectPanel::onButtonClicked() {
    auto *mw = GetMainWindow();
    if (mw == nullptr) return;

    if (isConnected()) {
        mw->profile_stop(false, false, true);
        return;
    }

    const int id = profileToStart();
    if (id < 0) {
        m_status->setText(tr("Add a subscription first"));
        return;
    }

    // Одна кнопка: клиент сам включает TUN, пользователя про режим не спрашиваем.
    // Права запросит get_elevated_permissions() внутри set_spmode_vpn.
    if (!Configs::dataManager->settingsRepo->spmode_vpn) {
        mw->set_spmode_vpn(true);
    }
    mw->profile_start(id);
}

void ConnectPanel::updateElapsed() {
    if (!m_connectedAt.isValid()) {
        m_elapsed->setText("00:00:00");
        return;
    }
    const qint64 secs = m_connectedAt.secsTo(QDateTime::currentDateTime());
    m_elapsed->setText(QString("%1:%2:%3")
                           .arg(secs / 3600, 2, 10, QChar('0'))
                           .arg((secs % 3600) / 60, 2, 10, QChar('0'))
                           .arg(secs % 60, 2, 10, QChar('0')));
}

void ConnectPanel::refresh() {
    const bool connected = isConnected();

    if (connected && !m_connectedAt.isValid()) {
        m_connectedAt = QDateTime::currentDateTime();
        m_ticker->start();
    } else if (!connected) {
        m_connectedAt = QDateTime();
        m_ticker->stop();
        m_elapsed->setText("00:00:00");
    }

    m_status->setText(connected ? tr("Connected") : tr("Disconnected"));
    m_button->setProperty("connected", connected);
    m_button->style()->unpolish(m_button);
    m_button->style()->polish(m_button);

    const int id = Configs::dataManager->settingsRepo->started_id >= 0
                       ? Configs::dataManager->settingsRepo->started_id
                       : profileToStart();
    if (const auto profile = Configs::dataManager->profilesRepo->GetProfile(id)) {
        m_server->setText(profile->outbound->DisplayName());
    } else {
        m_server->setText(tr("No server selected"));
    }
}
