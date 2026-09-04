#pragma once

namespace Fsnt {
    enum class UiMode {
        Advanced = 0,   // прежнее окно Throne
        Simple = 1,     // FSNT Client
    };

    // storedValue: значение settingsRepo->ui_mode; -1 означает "никогда не задавалось".
    // databaseHasContent: в базе уже есть группы с профилями, то есть установка не новая.
    //
    // Новая установка получает простой режим. Существующая остаётся в расширенном:
    // интерфейс не должен смениться под ногами после обновления.
    UiMode ResolveInitialUiMode(int storedValue, bool databaseHasContent);
}
