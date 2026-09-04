#include "include/configs/sub/SubscriptionUsage.hpp"

#include <QDateTime>

namespace Subscription {
    namespace {
        // Всё, что позже этого года, считаем «бессрочно», а не датой.
        constexpr int kForeverYear = 2100;
    }

    bool SubscriptionUsage::hasExpiry() const {
        if (expire <= 0) return false;
        const auto date = QDateTime::fromSecsSinceEpoch(expire);
        if (!date.isValid()) return false;
        return date.date().year() < kForeverYear;
    }

    SubscriptionUsage ParseSubscriptionUserInfo(const QString &raw) {
        SubscriptionUsage usage;
        if (raw.trimmed().isEmpty()) return usage;

        for (const QString &part : raw.split(';', Qt::SkipEmptyParts)) {
            const int eq = part.indexOf('=');
            if (eq <= 0) continue;

            const QString key = part.left(eq).trimmed().toLower();
            bool ok = false;
            const qint64 value = part.mid(eq + 1).trimmed().toLongLong(&ok);
            if (!ok) continue;

            if (key == "upload")        { usage.upload = value;   usage.valid = true; }
            else if (key == "download") { usage.download = value; usage.valid = true; }
            else if (key == "total")    { usage.total = value;    usage.valid = true; }
            else if (key == "expire")   { usage.expire = value;   usage.valid = true; }
        }
        return usage;
    }
}
