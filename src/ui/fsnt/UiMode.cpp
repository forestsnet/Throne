#include "include/ui/fsnt/UiMode.hpp"

namespace Fsnt {
    UiMode ResolveInitialUiMode(int storedValue) {
        if (storedValue == static_cast<int>(UiMode::Advanced)) return UiMode::Advanced;
        if (storedValue == static_cast<int>(UiMode::Simple)) return UiMode::Simple;

        // Не задано или мусор — простой режим. Раньше здесь смотрели, есть ли в
        // базе профили, и существующая установка оставалась в расширенном; но
        // переустановка сохраняет каталог с конфигом, и пользователь снова
        // попадал в старый интерфейс. Клиентский режим — умолчание продукта,
        // а кому нужен расширенный, тот выберет его в меню и выбор сохранится.
        return UiMode::Simple;
    }
}
