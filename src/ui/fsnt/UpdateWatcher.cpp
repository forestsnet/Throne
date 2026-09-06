#include "include/ui/fsnt/UpdateWatcher.hpp"

#include <QJsonArray>
#include <QRegularExpression>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>

#include "NkrVersion.h"

#include "include/global/Configs.hpp"
#include "include/global/HTTPRequestHelper.hpp"
#include "include/global/Utils.hpp"


namespace {
    // Сравнение по числам: 1.3.10 новее 1.3.9, и никакой лексикографики.
    // Разбор суффиксов вроде rc и beta здесь не нужен — предрелизы отсекаются
    // раньше, по флагу настроек.
    bool isNewerTag(const QString &tag) {
        const QString current = QStringLiteral(NKR_VERSION);
        if (current.isEmpty()) return false;

        const auto numbers = [](QString version) {
            if (version.startsWith('v') || version.startsWith('V')) version = version.mid(1);
            QList<int> parts;
            for (const auto &piece : version.split(QRegularExpression("[.\\-+]"))) {
                bool ok = false;
                const int value = piece.toInt(&ok);
                if (!ok) break;   // дальше пошли буквы — сравнивать нечего
                parts << value;
            }
            return parts;
        };

        const auto fresh = numbers(tag);
        const auto mine = numbers(current);
        if (fresh.isEmpty() || mine.isEmpty()) return false;
        for (int i = 0; i < qMax(fresh.size(), mine.size()); ++i) {
            const int a = i < fresh.size() ? fresh[i] : 0;
            const int b = i < mine.size() ? mine[i] : 0;
            if (a != b) return a > b;
        }
        return false;
    }

    // Первая проверка — через три минуты после запуска, дальше раз в шесть
    // часов. Чаще незачем: релизы выходят не ежечасно, а каждый запрос это
    // след в чужих логах.
    constexpr int kFirstCheckMs = 3 * 60 * 1000;
    constexpr int kIntervalMs = 6 * 60 * 60 * 1000;

    const auto kReleasesUrl = QStringLiteral("https://api.github.com/repos/forestsnet/Throne/releases");
}

namespace Fsnt {
    UpdateWatcher::UpdateWatcher(QObject *parent) : QObject(parent) {
        m_timer = new QTimer(this);
        m_timer->setInterval(kIntervalMs);
        connect(m_timer, &QTimer::timeout, this, &UpdateWatcher::check);
    }

    void UpdateWatcher::start() {
        QTimer::singleShot(kFirstCheckMs, this, [this] {
            check();
            m_timer->start();
        });
    }

    void UpdateWatcher::check() {
        QPointer<UpdateWatcher> self = this;
        runOnNewThread([self] {
            const auto response = Configs_network::NetworkRequestHelper::HttpGet(kReleasesUrl);
            if (!response.error.isEmpty()) return;   // сеть недоступна — молчим

            const auto releases = QString2QJsonArray(response.data);
            QString tag;
            QString notes;
            const bool allowBeta = Configs::dataManager->settingsRepo->allow_beta_update;
            for (const auto &value : releases) {
                const auto release = value.toObject();
                if (release["prerelease"].toBool() && !allowBeta) continue;
                if (release["draft"].toBool()) continue;
                tag = release["tag_name"].toString();
                notes = release["body"].toString();
                break;   // список отдаётся от свежих к старым
            }
            if (tag.isEmpty() || !isNewerTag(tag)) return;

            runOnUiThread([self, tag, notes] {
                if (!self.isNull()) emit self->updateFound(tag, notes);
            });
        });
    }
} // namespace Fsnt
