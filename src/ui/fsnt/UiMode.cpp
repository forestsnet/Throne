#include "include/ui/fsnt/UiMode.hpp"

namespace Fsnt {
    UiMode ResolveInitialUiMode(int storedValue, bool databaseHasContent) {
        if (storedValue == static_cast<int>(UiMode::Advanced)) return UiMode::Advanced;
        if (storedValue == static_cast<int>(UiMode::Simple)) return UiMode::Simple;

        // Не задано или мусор: новая установка получает простой режим,
        // существующая остаётся в расширенном.
        return databaseHasContent ? UiMode::Advanced : UiMode::Simple;
    }
}
