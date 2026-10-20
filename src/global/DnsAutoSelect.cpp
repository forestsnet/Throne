#include "include/global/DnsAutoSelect.hpp"

#include "include/configs/DnsCatalog.hpp"
#include "include/global/DnsProbe.hpp"
#include "include/global/Logger.hpp"

namespace Configs {
    DnsAutoSelect::DnsAutoSelect(QObject *parent) : QObject(parent) {}

    void DnsAutoSelect::Start(const QString &configured, int timeoutMs) {
        m_configured = configured.trimmed();
        m_timeoutMs = timeoutMs;

        auto *first = new DnsProbe(this);
        connect(first, &DnsProbe::Finished, this, [this, first](const QList<DnsProbeResult> &results) {
            first->deleteLater();
            if (!results.isEmpty() && results.first().ok) {
                LOG_INFO(QString("direct DNS %1 answered in %2 ms")
                             .arg(m_configured)
                             .arg(results.first().latencyMs));
                emit Selected(m_configured, false);
                return;
            }

            const QString why = results.isEmpty() ? QStringLiteral("не задан") : results.first().error;
            LOG_WARN(QString("direct DNS %1 did not answer (%2), looking for one that does")
                         .arg(m_configured, why));

            auto candidates = DnsCandidates(m_configured, true);
            candidates.removeAll(m_configured);
            if (candidates.isEmpty()) {
                emit Selected({}, true);
                return;
            }

            auto *rest = new DnsProbe(this);
            connect(rest, &DnsProbe::Finished, this, [this, rest](const QList<DnsProbeResult> &all) {
                rest->deleteLater();
                const auto best = DnsProbe::Fastest(all);
                if (best.isEmpty()) {
                    LOG_WARN("no direct DNS answered at all");
                } else {
                    LOG_INFO("direct DNS fell back to " + best);
                }
                emit Selected(best, true);
            });
            rest->Start(candidates, m_timeoutMs);
        });

        if (m_configured.isEmpty()) {
            first->Start({}, m_timeoutMs);
            return;
        }
        first->Start({m_configured}, m_timeoutMs);
    }
} // namespace Configs
