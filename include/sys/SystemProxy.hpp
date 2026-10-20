#pragma once

#include <QString>

// Per-platform. Both return what failed (empty on success) and may be called from any thread.

// windowsFormat is the Proxy Format setting, e.g. "{ip}:{port}" or "socks={ip}:{port}"; other platforms ignore it.
QString SystemProxy_Apply(const QString &host, int port, const QString &windowsFormat);

// Undoes SystemProxy_Apply(); does nothing unless this process applied a proxy, so proxies set by other apps survive.
QString SystemProxy_Clear();

// Снимок настроек, которые стояли у человека до нас, одной строкой — и возврат
// по этому снимку. Нужны, чтобы оборвавшийся сеанс можно было откатить на
// следующем запуске: карта того, что мы меняли, живёт в памяти процесса и
// умирает вместе с ним. Реализованы на Windows, где системный прокси
// действительно общий для всей системы; на остальных платформах снимок пуст,
// а возврат ничего не делает.
QString SystemProxy_Capture();
void SystemProxy_Restore(const QString &saved);
