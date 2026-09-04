#pragma once

#include <QString>

namespace Subscription {
    // Разбор заголовка subscription-userinfo:
    //   upload=0; download=266338304; total=1099511627776; expire=0
    struct SubscriptionUsage {
        qint64 upload = 0;
        qint64 download = 0;
        qint64 total = 0;    // 0 = безлимит
        qint64 expire = 0;   // 0 = бессрочно
        bool valid = false;

        qint64 used() const { return upload + download; }
        bool unlimited() const { return total <= 0; }

        // Срок прячем в двух случаях: ноль и заведомо «вечная» дата.
        // Провайдеры пользуются и тем, и другим, чтобы сказать «бессрочно».
        bool hasExpiry() const;
    };

    SubscriptionUsage ParseSubscriptionUserInfo(const QString &raw);
}
