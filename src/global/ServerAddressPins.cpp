#include "include/global/ServerAddressPins.hpp"

#include <QMutex>
#include <QMutexLocker>

namespace Configs {
    namespace {
        QMutex g_mutex;
        QMap<QString, QStringList> g_pins;
    } // namespace

    void SetServerAddressPin(const QString &domain, const QStringList &addresses) {
        const auto name = domain.trimmed().toLower();
        if (name.isEmpty()) return;
        QMutexLocker lock(&g_mutex);
        if (addresses.isEmpty()) {
            g_pins.remove(name);
            return;
        }
        g_pins[name] = addresses;
    }

    void ClearServerAddressPins() {
        QMutexLocker lock(&g_mutex);
        g_pins.clear();
    }

    QMap<QString, QStringList> ServerAddressPins() {
        QMutexLocker lock(&g_mutex);
        return g_pins;
    }
} // namespace Configs
