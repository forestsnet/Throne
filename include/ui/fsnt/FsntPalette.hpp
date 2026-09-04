#pragma once

#include <QColor>

namespace Fsnt {
    // Собственная палитра простого режима.
    //
    // Раньше окно брало ThemeTokens расширенного режима. Это была ошибка: qdarkstyle
    // задуман под плотный инженерный интерфейс, у него намеренно низкий контраст и
    // серые акценты. В клиентском окне от этого всё выглядит выцветшим.
    // Здесь свои цвета; от темы приложения берётся только одно — светлая она или тёмная.
    struct Palette {
        QColor bg;         // фон окна
        QColor surface;    // панели
        QColor card;       // карточки и поля ввода
        QColor cardHover;
        QColor border;
        QColor text;
        QColor textMuted;
        QColor accent;     // интерактив: фокус, ссылки, активная вкладка
        QColor accentSoft; // подложка под акцентом
        QColor success;    // подключено
        QColor danger;     // ошибка, избранное
        QColor warn;       // медленный пинг
        bool dark = true;
    };

    // Тёмная или светлая — решается по светлоте surface текущей темы приложения,
    // чтобы простой режим не оказался чёрным окном в светлой системе.
    Palette CurrentPalette();

    Palette PaletteFor(bool dark);
} // namespace Fsnt
