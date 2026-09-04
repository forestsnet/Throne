#pragma once

#include <QString>

namespace Fsnt {
    // Собирает QSS для окна FSNT Client из фирменной палитры (см. FsntPalette.hpp).
    QString BuildStyleSheet();

    // Радиусы и отступы в одном месте, чтобы карточки не разъезжались между виджетами.
    inline constexpr int kCardRadius = 14;
    inline constexpr int kRowRadius = 9;
    inline constexpr int kPanelPadding = 16;
}
