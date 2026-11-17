#include "include/sys/ConsoleText.h"

#include <QString>
#include <QStringConverter>

#include <windows.h>

#include <vector>

namespace Sys {
    QByteArray ConsoleOutputToUtf8(const QByteArray &raw) {
        if (raw.isEmpty()) return raw;

        // Решаем по самим байтам, а не по тому, о чём просили. `chcp 65001`
        // слушают не все: systeminfo и tasklist спрашивают кодовую страницу
        // консоли и переходят на UTF-8, а ipconfig и net берут OEM независимо
        // от неё. Из-за этого в отчёте 1.4.12 два файла из шести так и остались
        // в CP866 среди остальных в UTF-8.
        QStringDecoder utf8(QStringDecoder::Utf8);
        static_cast<void>(utf8.decode(raw));
        if (!utf8.hasError()) return raw;

        // Кодовую страницу спрашиваем у системы, а не зашиваем 866: у польской
        // и западных локалей OEM другая, и зашитая превратила бы читаемый
        // вывод в мусор ровно там, где сейчас всё в порядке.
        const int length = static_cast<int>(raw.size());
        const int chars = MultiByteToWideChar(CP_OEMCP, 0, raw.constData(), length, nullptr, 0);
        if (chars <= 0) return raw;
        std::vector<wchar_t> wide(static_cast<size_t>(chars));
        if (MultiByteToWideChar(CP_OEMCP, 0, raw.constData(), length, wide.data(), chars) != chars) return raw;
        return QString::fromWCharArray(wide.data(), chars).toUtf8();
    }
} // namespace Sys
