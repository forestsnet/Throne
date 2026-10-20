#include "include/ui/fsnt/TrayMenu.h"

#include <algorithm>

#include <QAction>
#include <QMenu>
#include <QSystemTrayIcon>

#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/global/Configs.hpp"
#include "include/stats/traffic/TrafficLooper.hpp"
#include "include/ui/fsnt/FsntSettingsDialog.h"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/ui/mainwindow.h"

namespace {
    // Сколько серверов показывать в подменю. Список подписки бывает на сотню
    // строк, и меню на весь экран выбирать неудобнее, чем окно.
    constexpr int kTrayServerLimit = 12;

    bool tunnelRunning() {
        const auto &settings = Configs::dataManager->settingsRepo;
        return settings->core_running && settings->started_id >= 0;
    }

    QString currentServerName() {
        const auto &settings = Configs::dataManager->settingsRepo;
        if (settings->started_id < 0) return {};
        const auto profile = Configs::dataManager->profilesRepo->GetProfile(settings->started_id);
        return profile ? profile->outbound->DisplayName() : QString();
    }
}

namespace Fsnt {
    TrayMenu::TrayMenu(QSystemTrayIcon *tray, QObject *parent) : QObject(parent), m_tray(tray) {
        if (m_tray == nullptr) return;

        m_menu = new QMenu();
        m_menu->setStyleSheet(BuildStyleSheet());
        connect(m_menu, &QMenu::aboutToShow, this, &TrayMenu::rebuild);
        m_tray->setContextMenu(m_menu);
        rebuild();
    }

    void TrayMenu::rebuild() {
        if (m_menu == nullptr) return;
        m_menu->clear();

        addStatusSection();
        m_menu->addSeparator();

        const bool running = tunnelRunning();
        auto *toggle = m_menu->addAction(running ? tr("Disconnect") : tr("Connect"));
        connect(toggle, &QAction::triggered, this, [running] {
            if (Fsnt_RequestConnection) Fsnt_RequestConnection(!running);
        });

        addServerSection();
        m_menu->addSeparator();

        connect(m_menu->addAction(tr("Open FSNT Client")), &QAction::triggered, this,
                [] { ActivateUiWindow(); });

        connect(m_menu->addAction(tr("Settings")), &QAction::triggered, this, [] {
            // Настройки модальны и должны принадлежать видимому окну, иначе
            // уедут за него и заблокируют интерфейс молча.
            ActivateUiWindow();
            auto *dialog = new FsntSettingsDialog(GetFacadeWindow());
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->exec();
        });

        m_menu->addSeparator();
        connect(m_menu->addAction(tr("Quit")), &QAction::triggered, this, [] {
            // Через действие движка, а не qApp->quit(): по дороге туннель надо
            // опустить и системный прокси снять, и всё это умеет MainWindow.
            if (auto *mw = GetMainWindow(); mw != nullptr) {
                if (auto *action = mw->findChild<QAction *>(QStringLiteral("menu_exit"))) {
                    action->trigger();
                }
            }
        });
    }

    void TrayMenu::addStatusSection() {
        const bool running = tunnelRunning();

        auto *state = m_menu->addAction(running ? tr("Connected") : tr("Not connected"));
        state->setEnabled(false);

        if (const QString server = currentServerName(); !server.isEmpty()) {
            auto *where = m_menu->addAction(server);
            where->setEnabled(false);
        }

        // Скорость показываем только на живом туннеле: на отключённом это
        // всегда нули, а строка с нулями выглядит как поломка.
        if (running && Stats::trafficLooper != nullptr && Stats::trafficLooper->proxy != nullptr) {
            auto *speed = m_menu->addAction(Stats::DisplaySpeed(Stats::trafficLooper->proxy));
            speed->setEnabled(false);
        }

        m_tray->setToolTip(running && !currentServerName().isEmpty()
                               ? tr("FSNT Client — %1").arg(currentServerName())
                               : QStringLiteral("FSNT Client"));
    }

    void TrayMenu::addServerSection() {
        const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
        if (!group) return;
        const auto ids = group->Profiles();
        if (ids.isEmpty()) return;

        auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(ids);
        // Сначала измеренные и быстрые: в меню помещается десяток строк, и
        // это должны быть те, что человек и выбрал бы.
        std::stable_sort(profiles.begin(), profiles.end(),
                         [](const auto &a, const auto &b) {
                             if (!a || !b) return static_cast<bool>(a);
                             const bool aMeasured = a->latency > 0;
                             const bool bMeasured = b->latency > 0;
                             if (aMeasured != bMeasured) return aMeasured;
                             return aMeasured ? a->latency < b->latency : false;
                         });

        auto *servers = m_menu->addMenu(tr("Server"));
        servers->setStyleSheet(BuildStyleSheet());

        const QString chosen = Configs::dataManager->settingsRepo->simple_selected_server;
        const int startedId = Configs::dataManager->settingsRepo->started_id;

        int shown = 0;
        for (const auto &profile : profiles) {
            if (!profile || shown >= kTrayServerLimit) break;
            const QString name = profile->outbound->DisplayName();
            const QString label = profile->latency > 0
                                      ? tr("%1 — %2 ms").arg(name).arg(profile->latency)
                                      : name;
            auto *item = servers->addAction(label);
            item->setCheckable(true);
            item->setChecked(profile->id == startedId || (startedId < 0 && name == chosen));
            const int id = profile->id;
            connect(item, &QAction::triggered, this, [this, id, name] { chooseServer(id, name); });
            ++shown;
        }
    }

    void TrayMenu::chooseServer(const int profileId, const QString &name) {
        auto &settings = Configs::dataManager->settingsRepo;
        settings->simple_selected_server = name;
        settings->Save();

        // На поднятом туннеле ядро переключается на новый профиль; на
        // опущенном выбор просто запомнится до нажатия «Подключить».
        if (tunnelRunning()) {
            if (auto *mw = GetMainWindow(); mw != nullptr) mw->profile_start(profileId);
        }
    }
} // namespace Fsnt
