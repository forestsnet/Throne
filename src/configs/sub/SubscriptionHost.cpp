#include "include/configs/sub/SubscriptionHost.hpp"

#include <QRegularExpression>
#include <QUrl>

namespace Subscription {
    namespace {
        // Разбор «хост» или «хост:порт» и ничего кроме. Всё остальное — чужой
        // путь, чужие учётные данные или просто мусор, и подписку туда не
        // переносим. IPv6 приезжает в скобках, setAuthority это понимает.
        QUrl bareAuthority(const QString &candidate) {
            const QString text = candidate.trimmed();
            if (text.isEmpty()) return {};
            if (text.contains('/') || text.contains('@') || text.contains('?') || text.contains('#')
                || text.contains(QChar::Space)) {
                return {};
            }

            QUrl probe;
            probe.setScheme(QStringLiteral("https"));
            probe.setAuthority(text, QUrl::StrictMode);
            if (!probe.isValid() || probe.host().isEmpty()) return {};
            // Хост без точки — это не домен подписки, а имя из локальной сети.
            if (!probe.host().contains('.') && !probe.host().contains(':')) return {};
            return probe;
        }

        QUrl httpsSubscription(const QString &url) {
            const QUrl parsed(url.trimmed(), QUrl::StrictMode);
            if (!parsed.isValid() || parsed.host().isEmpty()) return {};
            if (parsed.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) != 0) return {};
            return parsed;
        }
    } // namespace

    QString SubscriptionHost(const QString &url) {
        const QUrl parsed(url.trimmed(), QUrl::StrictMode);
        if (!parsed.isValid() || parsed.host().isEmpty()) return {};
        return parsed.port() == -1 ? parsed.host() : QStringLiteral("%1:%2").arg(parsed.host()).arg(parsed.port());
    }

    QString SwapSubscriptionHost(const QString &url, const QString &candidate) {
        const QUrl parsed = httpsSubscription(url);
        if (parsed.isEmpty()) return {};

        const QUrl authority = bareAuthority(candidate);
        if (authority.isEmpty()) return {};

        QUrl moved = parsed;
        moved.setHost(authority.host());
        if (authority.port() != -1) moved.setPort(authority.port());
        if (!moved.isValid() || moved.host().isEmpty()) return {};
        if (moved.host().compare(parsed.host(), Qt::CaseInsensitive) == 0 && moved.port() == parsed.port()) return {};
        return moved.toString(QUrl::FullyEncoded);
    }

    QStringList ParseMirrorHosts(const QString &raw) {
        static const QRegularExpression separators(QStringLiteral("[,;\\s]+"));
        QStringList hosts;
        for (const QString &piece : raw.trimmed().split(separators, Qt::SkipEmptyParts)) {
            const QUrl authority = bareAuthority(piece);
            if (authority.isEmpty()) continue;
            const QString host = authority.port() == -1
                                     ? authority.host()
                                     : QStringLiteral("%1:%2").arg(authority.host()).arg(authority.port());
            if (!hosts.contains(host, Qt::CaseInsensitive)) hosts << host;
        }
        return hosts;
    }

    QStringList MirrorCandidates(const QString &url, const QString &mirrors) {
        QStringList candidates;
        for (const QString &host : ParseMirrorHosts(mirrors)) {
            const QString moved = SwapSubscriptionHost(url, host);
            // Пусто и у того хоста, на котором подписка уже сидит: он только что не ответил.
            if (moved.isEmpty() || candidates.contains(moved)) continue;
            candidates << moved;
        }
        return candidates;
    }
}
