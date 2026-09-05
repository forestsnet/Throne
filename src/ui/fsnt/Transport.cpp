#include "include/ui/fsnt/Transport.hpp"

#include "include/global/Configs.hpp"
#include "include/ui/mainwindow.h"

namespace Fsnt {
    void ApplyTransportMode() {
        auto *mw = GetMainWindow();
        if (mw == nullptr) return;

        const auto &settings = Configs::dataManager->settingsRepo;
        const bool wantTun = settings->simple_transport == 0;

        // Сначала гасим ненужный режим, потом поднимаем нужный: включить оба
        // разом означало бы завернуть трафик дважды.
        if (wantTun) {
            if (settings->spmode_system_proxy) mw->set_spmode_system_proxy(false);
            // Права для TUN запросит get_elevated_permissions() внутри.
            if (!settings->spmode_vpn) mw->set_spmode_vpn(true);
            return;
        }
        if (settings->spmode_vpn) mw->set_spmode_vpn(false);
        if (!settings->spmode_system_proxy) mw->set_spmode_system_proxy(true);
    }
} // namespace Fsnt
