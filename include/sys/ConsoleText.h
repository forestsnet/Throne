#pragma once

#include <QByteArray>

namespace Sys {
    // Вывод консольной утилиты Windows в UTF-8. Правильный UTF-8 возвращается
    // как есть, остальное переводится из текущей кодовой страницы OEM — той
    // самой, в которой утилиты отвечают на русской системе.
    QByteArray ConsoleOutputToUtf8(const QByteArray &raw);
} // namespace Sys
