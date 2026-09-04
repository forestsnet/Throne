#include "include/ui/fsnt/ConnectPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/global/Configs.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/ui/fsnt/PowerButton.h"
#include "include/ui/mainwindow.h"

namespace {
    // Сколько ждём ответа ядра, прежде чем признать попытку неудавшейся.
    constexpr int kPendingTimeoutMs = 25000;
}

ConnectPanel::ConnectPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addStretch();

    m_elapsed = new QLabel("00:00:00", this);
    m_elapsed->setObjectName("fsntElapsed");
    m_elapsed->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_elapsed);

    m_status = new QLabel(tr("Disconnected"), this);
    m_status->setObjectName("fsntStatus");
    m_status->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_status);

    layout->addSpacing(14);

    m_button = new PowerButton(this);
    connect(m_button, &PowerButton::clicked, this, &ConnectPanel::onButtonClicked);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    buttonRow->addWidget(m_button);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    layout->addSpacing(14);

    m_server = new QLabel(tr("No server selected"), this);
    m_server->setObjectName("fsntCurrentServer");
    m_server->setAlignment(Qt::AlignCenter);
    m_server->setWordWrap(true);
    layout->addWidget(m_server);

    m_transport = new QLabel(this);
    m_transport->setObjectName("fsntSubMeta");
    m_transport->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_transport);

    layout->addStretch();

    m_ticker = new QTimer(this);
    m_ticker->setInterval(1000);
    connect(m_ticker, &QTimer::timeout, this, &ConnectPanel::updateElapsed);

    m_pendingGuard = new QTimer(this);
    m_pendingGuard->setSingleShot(true);
    m_pendingGuard->setInterval(kPendingTimeoutMs);
    connect(m_pendingGuard, &QTimer::timeout, this, [this] {
        m_pending = false;
        refresh();
    });

    refresh();
}

bool ConnectPanel::isConnected() {
    const auto &settings = Configs::dataManager->settingsRepo;
    return settings->core_running && settings->started_id >= 0;
}

ConnectPanel::Choice ConnectPanel::resolveProfile() {
    const auto &settings = Configs::dataManager->settingsRepo;
    if (settings->started_id >= 0) return {settings->started_id, false};

    const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (!group) return {};
    const auto ids = group->Profiles();
    if (ids.isEmpty()) return {};

    const auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(ids);

    // Явный выбор пользователя — по имени и только в пределах этой подписки.
    // По имени, потому что при sub_clear обновление пересоздаёт все профили:
    // сохранённый id переставал существовать после первого же обновления.
    if (const QString chosen = settings->simple_selected_server; !chosen.isEmpty()) {
        for (const auto &profile : profiles) {
            if (profile && profile->outbound->DisplayName() == chosen) return {profile->id, false};
        }
    }

    // Авто-селектор сам держит лучший сервер — это и есть верное умолчание.
    for (const auto &profile : profiles) {
        if (profile && profile->AutoSelector() != nullptr) return {profile->id, true};
    }

    // Иначе самый быстрый из измеренных. Раньше здесь брался просто первый в
    // списке, и это мог оказаться сервер на другом конце света.
    int best = -1;
    int bestLatency = 0;
    for (const auto &profile : profiles) {
        if (!profile || profile->latency <= 0) continue;
        if (best < 0 || profile->latency < bestLatency) {
            best = profile->id;
            bestLatency = profile->latency;
        }
    }
    if (best >= 0) return {best, true};

    // Пинги ещё не мерили — деваться некуда, но пометим выбор автоматическим.
    return {ids.first(), true};
}

void ConnectPanel::setStatus(const QString &text, const char *tone) {
    m_status->setText(text);
    m_status->setProperty("tone", tone);
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
}

void ConnectPanel::onButtonClicked() {
    auto *mw = GetMainWindow();
    if (mw == nullptr) return;

    if (isConnected()) {
        m_pending = true;
        m_button->setState(PowerButton::State::Stopping);
        setStatus(tr("Disconnecting"), "busy");
        m_pendingGuard->start();
        mw->profile_stop(false, false, true);
        return;
    }

    const int id = resolveProfile().id;
    if (id < 0) {
        setStatus(tr("Add a subscription first"), "");
        emit subscriptionNeeded();
        return;
    }

    m_pending = true;
    m_button->setState(PowerButton::State::Connecting);
    setStatus(tr("Connecting"), "busy");
    m_pendingGuard->start();

    // Одна кнопка: режим клиент выбирает сам. По умолчанию TUN, но пользователь
    // может переключиться на системный прокси в настройках простого режима.
    // Права для TUN запросит get_elevated_permissions() внутри set_spmode_vpn.
    const bool wantTun = Configs::dataManager->settingsRepo->simple_transport == 0;
    if (wantTun && !Configs::dataManager->settingsRepo->spmode_vpn) {
        mw->set_spmode_vpn(true);
    } else if (!wantTun && Configs::dataManager->settingsRepo->spmode_vpn) {
        mw->set_spmode_vpn(false);
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

    // Ядро ответило — ожидание закончилось, чем бы оно ни кончилось.
    if (m_pending) {
        const bool stopping = m_button->state() == PowerButton::State::Stopping;
        if (connected != stopping) {
            m_pending = false;
            m_pendingGuard->stop();
        }
    }

    if (connected && !m_connectedAt.isValid()) {
        m_connectedAt = QDateTime::currentDateTime();
        m_ticker->start();
    } else if (!connected) {
        m_connectedAt = QDateTime();
        m_ticker->stop();
        m_elapsed->setText("00:00:00");
    }

    if (!m_pending) {
        m_button->setState(connected ? PowerButton::State::Connected : PowerButton::State::Off);
        setStatus(connected ? tr("Connected") : tr("Disconnected"), connected ? "ok" : "");
    }

    const Choice choice = resolveProfile();
    if (const auto profile = Configs::dataManager->profilesRepo->GetProfile(choice.id)) {
        m_server->setText(profile->outbound->DisplayName());
    } else {
        m_server->setText(tr("No server selected"));
    }

    QString line = Configs::dataManager->settingsRepo->simple_transport == 0
                       ? tr("Full tunnel (TUN)")
                       : tr("System proxy");
    // Без этой пометки непонятно, откуда взялся сервер, если его никто не выбирал.
    if (choice.id >= 0 && choice.automatic) line += " · " + tr("chosen automatically");
    m_transport->setText(line);

    // Шлём на каждом обновлении, а не только при смене: конструктор панели
    // вызывает refresh() раньше, чем окно успевает подключиться к сигналу.
    // Зацикливания нет — selectProfile выходит сразу, если строка уже текущая.
    if (choice.id >= 0) emit profileResolved(choice.id);
}
