#pragma once

#include <QString>

#include <functional>

namespace Fsnt {
    // Про что уведомление. Категории видны человеку в настройках, поэтому их
    // ровно столько, сколько он способен различить: версия клиента, подписка,
    // состояние туннеля.
    enum class NotifyKind {
        Update,
        Subscription,
        Connection,
    };

    // Заранее спросить у системы разрешение на уведомления, если хоть одна
    // категория включена. Там, где разрешение не нужно, ничего не делает.
    void PrimeNotifications();

    // Включена ли категория в настройках.
    bool NotifyEnabled(NotifyKind kind);

    // Показать уведомление средствами системы. Одна дорога на все площадки:
    // на маке идём в UserNotifications и падаем на свою карточку, если система
    // отказала, на Windows и Linux — родное сообщение из трея.
    //
    // onActivated зовём, если по уведомлению щёлкнули.
    void Notify(NotifyKind kind, const QString &title, const QString &body,
                const std::function<void()> &onActivated = {});
} // namespace Fsnt
