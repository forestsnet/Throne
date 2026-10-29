#include "include/global/DnsAutoSelect.hpp"

#include "include/configs/DnsAddress.hpp"
#include "include/configs/DnsCatalog.hpp"
#include "include/global/DnsProbe.hpp"
#include "include/global/Logger.hpp"

#include <algorithm>

namespace Configs {
    namespace {
        // Круг идёт параллельно и укладывается в один таймаут, так что цена
        // лишнего кандидата — сокет, а не ожидание. Запас взят под весь каталог
        // целиком: под туннелем из него остаётся больше двадцати адресов, и
        // обрезать хвост значило бы выкинуть как раз тех, до кого доходит дело,
        // когда ближние не отвечают.
        constexpr int kMaxProbes = 32;

        // Что ответило и за сколько — одной строкой. Это первое, что хочется
        // увидеть в диагностике с чужой машины: у человека в РФ отвечает не то,
        // что у нас, и гадать об этом бессмысленно.
        QString roundSummary(const QList<DnsProbeResult> &results) {
            QStringList answered;
            int silent = 0;
            for (const auto &result : results) {
                if (result.ok) {
                    answered << QString("%1 %2ms").arg(result.address).arg(result.latencyMs);
                } else {
                    ++silent;
                }
            }
            std::sort(answered.begin(), answered.end(), [](const QString &a, const QString &b) {
                return a.section(' ', -1).remove("ms").toInt() < b.section(' ', -1).remove("ms").toInt();
            });
            return QString("%1 answered of %2: %3")
                .arg(answered.size())
                .arg(results.size())
                .arg(answered.isEmpty() ? QStringLiteral("none") : answered.join(", "));
        }
    } // namespace

    DnsAutoSelect::DnsAutoSelect(QObject *parent) : QObject(parent) {}

    // Почему заданный сервер не взяли: он либо не попал в круг вовсе (не
    // переживёт туннель), либо ответил отказом. В журнал, а не человеку.
    QString DnsAutoSelect::whyNotConfigured(const QList<DnsProbeResult> &results) const {
        if (m_configured.isEmpty()) return QStringLiteral("not set");
        for (const auto &result : results) {
            if (result.address == m_configured) return result.error;
        }
        if (!Fsnt::DnsUsableUnderTunnel(m_configured)) {
            return QStringLiteral("port 53 or a name cannot be resolved under the tunnel");
        }
        return QStringLiteral("not probed");
    }

    void DnsAutoSelect::Start(const QString &configured, int timeoutMs, const QString &probeName) {
        m_configured = configured.trimmed();
        m_probeName = probeName.trimmed();
        m_timeoutMs = timeoutMs;

        // Кандидаты уже отсортированы по предпочтению и очищены от того, что под
        // туннелем не живёт, — заданный сервер в том числе, если он на 53-м
        // порту. Поэтому «ответил» и «подойдёт» здесь одно и то же.
        auto candidates = DnsCandidates(m_configured, true);
        if (candidates.size() > kMaxProbes) candidates = candidates.mid(0, kMaxProbes);
        if (candidates.isEmpty()) {
            LOG_WARN("direct DNS: nothing left to try");
            emit Selected({}, true, {});
            return;
        }

        auto *probe = new DnsProbe(this);
        connect(probe, &DnsProbe::Finished, this, [this, probe](const QList<DnsProbeResult> &all) {
            probe->deleteLater();
            LOG_INFO("direct DNS round: " + roundSummary(all));

            // Два разных вопроса к одному кругу. Первый: чем резолвить дальше —
            // тут годится только то, что переживёт туннель. Второй: какой адрес
            // у нашего сервера — тут годится ответ любого, кто ответил, потому
            // что отвечали все до того, как туннель что-либо закрыл.
            const bool mustSurvive = PlainDnsBlockedByTunnel();
            const auto survives = [mustSurvive](const DnsProbeResult &result) {
                return result.ok && (!mustSurvive || Fsnt::DnsUsableUnderTunnel(result.address));
            };

            QString chosen;
            int chosenLatency = -1;
            for (const auto &result : all) {
                if (result.address == m_configured && survives(result)) {
                    chosen = m_configured;
                    chosenLatency = result.latencyMs;
                    break;
                }
            }
            if (chosen.isEmpty()) {
                for (const auto &result : all) {
                    if (!survives(result)) continue;
                    if (chosenLatency >= 0 && result.latencyMs >= chosenLatency) continue;
                    chosen = result.address;
                    chosenLatency = result.latencyMs;
                }
            }

            m_resolved.clear();
            for (const auto &result : all) {
                if (result.address == chosen && !result.addresses.isEmpty()) {
                    m_resolved = result.addresses;
                    break;
                }
            }
            if (m_resolved.isEmpty()) {
                int best = -1;
                for (const auto &result : all) {
                    if (!result.ok || result.addresses.isEmpty()) continue;
                    if (best >= 0 && result.latencyMs >= best) continue;
                    best = result.latencyMs;
                    m_resolved = result.addresses;
                }
            }
            if (!m_resolved.isEmpty() && !m_probeName.isEmpty()) {
                LOG_INFO(QString("%1 resolved before the tunnel: %2")
                             .arg(m_probeName, m_resolved.join(", ")));
            }

            if (chosen.isEmpty()) {
                LOG_WARN("no usable direct DNS answered");
                emit Selected({}, true, m_resolved);
                return;
            }
            if (chosen == m_configured) {
                LOG_INFO(QString("direct DNS %1 answered in %2 ms").arg(m_configured).arg(chosenLatency));
                emit Selected(m_configured, false, m_resolved);
                return;
            }
            LOG_INFO(QString("direct DNS: %1 instead of %2 (%3)")
                         .arg(chosen, m_configured.isEmpty() ? QStringLiteral("none") : m_configured,
                              whyNotConfigured(all)));
            emit Selected(chosen, true, m_resolved);
        });
        probe->Start(candidates, m_timeoutMs, m_probeName);
    }

} // namespace Configs
