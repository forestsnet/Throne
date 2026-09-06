#include "include/ui/fsnt/Notifier.hpp"

#include <QCoreApplication>
#include <QSystemTrayIcon>

#include "include/global/Configs.hpp"
#include "include/ui/fsnt/DesktopNotice.hpp"
#include "include/ui/mainwindow.h"

#ifdef Q_OS_MACOS
#include "include/sys/macos/MacNotify.hpp"
#endif

namespace Fsnt {
    bool NotifyEnabled(NotifyKind kind) {
        const auto &settings = Configs::dataManager->settingsRepo;
        switch (kind) {
            case NotifyKind::Update:
                return settings->notify_update_system;
            case NotifyKind::Subscription:
                return settings->notify_subscription;
            case NotifyKind::Connection:
                return settings->notify_connection;
        }
        return false;
    }

    void PrimeNotifications() {
#ifdef Q_OS_MACOS
        if (!NotifyEnabled(NotifyKind::Update) && !NotifyEnabled(NotifyKind::Subscription) &&
            !NotifyEnabled(NotifyKind::Connection)) {
            return;
        }
        MacNotify::Prime();
#endif
    }

    void Notify(NotifyKind kind, const QString &title, const QString &body,
                const std::function<void()> &onActivated) {
        if (!NotifyEnabled(kind)) return;

#ifdef Q_OS_MACOS
        MacNotify::Post(title, body, onActivated, [title, body, onActivated] {
            // Система отказала — своя карточка поверх окон.
            auto *notice = DesktopNotice::Show(title, body);
            if (onActivated) {
                QObject::connect(notice, &DesktopNotice::activated, qApp,
                                 [onActivated] { onActivated(); });
            }
        });
#else
        Q_UNUSED(onActivated)
        auto *mw = GetMainWindow();
        if (mw == nullptr || mw->trayIcon() == nullptr) return;
        mw->trayIcon()->showMessage(title, body, QSystemTrayIcon::Information, 8000);
#endif
    }
} // namespace Fsnt
