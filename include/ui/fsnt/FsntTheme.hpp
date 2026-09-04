#pragma once

#include <QString>

struct ThemeTokens;

namespace Fsnt {
    // Собирает QSS для окна FSNT Client из палитры приложения.
    // Своей палитры не заводим: цвета обязаны совпадать с расширенным режимом.
    QString BuildStyleSheet(const ThemeTokens &tokens);

    // Радиусы и отступы в одном месте, чтобы карточки не разъезжались между виджетами.
    inline constexpr int kCardRadius = 10;
    inline constexpr int kRowRadius = 8;
    inline constexpr int kPanelPadding = 12;
}
