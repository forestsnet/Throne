#pragma once

namespace Fsnt {
    enum class UiMode {
        Advanced = 0,   // прежнее окно Throne
        Simple = 1,     // FSNT Client
    };

    // storedValue: значение settingsRepo->ui_mode; -1 означает "никогда не задавалось".
    //
    // Пока выбор не сделан — простой режим. Явно выбранный режим сохраняется и
    // переживает обновления: переключение живёт в меню окна.
    UiMode ResolveInitialUiMode(int storedValue);
}
