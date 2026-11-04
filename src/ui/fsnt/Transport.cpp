#include "include/ui/fsnt/Transport.hpp"

#include "include/global/Configs.hpp"
#include "include/engine/EngineHost.h"

namespace Fsnt {
    bool ApplyTransportMode() {
        auto *engine = Engine::host();
        if (engine == nullptr) return false;

        const auto &settings = Configs::dataManager->settingsRepo;
        const bool wantTun = settings->simple_transport == 0;

        // Сначала гасим ненужный режим, потом поднимаем нужный: включить оба
        // разом означало бы завернуть трафик дважды.
        if (wantTun) {
            if (settings->spmode_system_proxy) engine->setSystemProxy(false, {});
            // Права для TUN запросит setTun() внутри.
            if (!settings->spmode_vpn) engine->setTun(true, {});
            // Без прав туннеля нет: ядро поднимется, соединения пойдут мимо
            // него, и человек будет уверен, что защищён, глядя на «Подключено».
            return settings->spmode_vpn;
        }
        if (settings->spmode_vpn) engine->setTun(false, {});
        // По возврату, как и в ветке туннеля выше. setSystemProxy отказывает в
        // headless и когда выключен смешанный вход, а здесь возврат
        // отбрасывался и режим всегда объявлялся применённым: кнопка горела
        // «Подключено» на трафике, который идёт мимо прокси.
        if (!settings->spmode_system_proxy && !engine->setSystemProxy(true, {})) return false;
        return true;
    }
} // namespace Fsnt
