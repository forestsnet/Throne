#include "include/ui/mainwindow.h"
#include "include/configs/sub/ProviderPolicy.hpp"

#include "include/global/SystemStateJournal.hpp"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/engine/connection/ConnectionManager.h"

int MainWindow::get_profile_to_start() {
    const auto ents = get_now_selected_list();
    if (ents.size() == 1) {
        return ents.first();
    }
    if (ents.isEmpty()) {
        if (connection()->lastProfileId() >= 0 && Configs::dataManager->profilesRepo->GetProfile(connection()->lastProfileId()) != nullptr) {
            return connection()->lastProfileId();
        }
        const int rememberId = Configs::dataManager->settingsRepo->remember_id;
        if (rememberId >= 0 && Configs::dataManager->profilesRepo->GetProfile(rememberId) != nullptr) {
            return rememberId;
        }
        const auto currentGroup = Configs::dataManager->groupsRepo->CurrentGroup();
        if (currentGroup) {
            const auto profiles = currentGroup->Profiles();
            if (!profiles.isEmpty()) {
                const int firstId = profiles.first();
                if (Configs::dataManager->profilesRepo->GetProfile(firstId) != nullptr) {
                    return firstId;
                }
            }
        }
    }
    return -1;
}

void MainWindow::profile_start(int _id) {
    if (Configs::dataManager->settingsRepo->prepare_exit) return;
    if (_id < 0) _id = get_profile_to_start();
    if (select_mode && _id >= 0 && Configs::dataManager->profilesRepo->GetProfile(_id) != nullptr) {
        finish_select_mode(_id);
        return;
    }

    // Ядро могли удалить в любой момент, а не только до запуска приложения.
    if (!EnsureCorePresent()) return;

#ifdef Q_OS_MACOS
    // Под туннелем ядро само подменяет системный DNS на адрес внутри tun.
    // Записываем это за него: иначе после обрыва сеанса о подмене не узнает
    // никто, а резолвер останется смотреть в исчезнувший интерфейс.
    if (Configs::dataManager->settingsRepo->spmode_vpn) {
        Configs::SystemStateJournal::Record(
            QStringLiteral("system-dns-tunnel"),
            Configs::dataManager->settingsRepo->vpn_tun_ipv4_cidr.section('/', 0, 0).trimmed());
    }
#endif

    // Диалог живёт в mainwindow_conflicts.cpp: он модальный и держит запуск,
    // поэтому спрашивает не чаще одного раза за сеанс.
    if (Configs::dataManager->settingsRepo->spmode_vpn && !ConfirmConflictingProcesses()) return;

    if (const auto policyEnt = Configs::dataManager->profilesRepo->GetProfile(_id)) {
        applyProviderPolicy(policyEnt->gid);
    }

#ifndef Q_OS_LINUX
    // Выбран полный туннель, а прав на него нет — дальше идти незачем. Ядро
    // поднялось бы, трафик пошёл бы мимо туннеля, а окно показывало бы
    // «Подключено»: ровно то, на что жаловались люди после отказа выдать права.
    // Это сторож для запуска не по кнопке — автозапуск при старте клиента.
    if (GetFacadeWindow() != nullptr && Configs::dataManager->settingsRepo->simple_transport == 0 &&
        !Configs::dataManager->settingsRepo->spmode_vpn) {
        const auto text = tr("The tunnel needs administrator rights, connection cancelled");
        MW_show_log(text);
        showFacadeNotice(text, 8000);
        return;
    }
#endif

    connection()->start(StartRequest{_id});
}

void MainWindow::applyProviderPolicy(int gid) {
    // Политика может включить туннель, а это перезапускает профиль — без
    // стража применение зацикливается.
    if (m_applyingProviderPolicy) return;

    const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
    if (group == nullptr) {
        Subscription::ClearActiveProviderPolicy();
        return;
    }

    const auto policy = Subscription::DeserializeProviderPolicy(group->provider_policy_json);
    Subscription::SetActiveProviderPolicy(policy, gid);
    if (policy.isEmpty()) return;

    m_applyingProviderPolicy = true;

    if (policy.alwaysHwid.has_value()) {
        Configs::dataManager->settingsRepo->sub_send_hwid = policy.alwaysHwid.value();
    }
    if (policy.autoUpdate.has_value()) {
        group->skip_auto_update = !policy.autoUpdate.value();
        Configs::dataManager->groupsRepo->Save(group);
    }
    if (policy.updateIntervalHours > 0) {
        Configs::dataManager->settingsRepo->sub_auto_update = policy.updateIntervalHours * 60;
    }
    Configs::dataManager->settingsRepo->Save();

    // Последним: перезапускает профиль, поэтому прочие настройки уже должны быть записаны.
    if (policy.tunEnable.has_value()
        && policy.tunEnable.value() != Configs::dataManager->settingsRepo->spmode_vpn) {
        MW_show_log(QString("[Policy] Provider requests TUN=%1").arg(policy.tunEnable.value() ? "on" : "off"));
        connection()->setTun(policy.tunEnable.value());
    }

    m_applyingProviderPolicy = false;
}

void MainWindow::profile_stop(bool crash, bool block, bool manual, bool interactive) {
    connection()->stop({crash, block, manual, interactive});
}

std::shared_ptr<Configs::Profile> MainWindow::runningSnapshot() const {
    return connection()->runningSnapshot();
}

void MainWindow::toggle_connection() {
    const auto state = connection()->connectionState();
    if (select_mode || state == ConnectionState::Connecting || state == ConnectionState::Stopping) return;
    if (runningSnapshot() != nullptr) {
        profile_stop(false, false, true);
        return;
    }
    profile_start(connection()->resolveLastProfile());
}
