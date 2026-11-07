#include "include/global/DnsAutoSelect.hpp"

#include "include/configs/DnsAddress.hpp"
#include "include/configs/DnsCatalog.hpp"
#include "include/global/DirectDnsProbe.hpp"
#include <QThread>
#include <memory>
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
            for (const auto &result : results) {
                if (result.responded || result.ok) {
                    answered << QString("%1 %2ms").arg(result.address).arg(result.latencyMs);
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
        const auto underTunnel = QStringLiteral("port 53 or a name cannot be resolved under the tunnel");
        for (const auto &result : results) {
            if (result.address != m_configured) continue;
            // Ответил и всё равно не подошёл — это не «не ответил», и в журнале
            // должно стоять именно это, иначе причина читается как молчание.
            return result.ok ? QStringLiteral("answered, but ") + underTunnel : result.error;
        }
        if (!Fsnt::DnsUsableUnderTunnel(m_configured)) return underTunnel;
        return QStringLiteral("not probed");
    }

    void DnsAutoSelect::Start(const QString &configured, int timeoutMs, const QString &probeName) {
        m_configured = configured.trimmed();
        m_probeName = probeName.trimmed();
        m_timeoutMs = timeoutMs;

        auto candidates = DnsCandidates(m_configured, true);
        if (candidates.size() > kMaxProbes) candidates = candidates.mid(0, kMaxProbes);
        if (candidates.isEmpty()) {
            LOG_WARN("direct DNS: nothing left to try");
            emit Selected({}, true, {}, DirectDnsFailure::ProbeUnavailable);
            return;
        }

        auto round = std::make_shared<DirectDnsProbeRound>();
        auto *worker = QThread::create([round, candidates, timeout = m_timeoutMs, name = m_probeName] {
            *round = ProbeDirectDns(candidates, timeout, name);
        });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        const bool mustSurvive = PlainDnsBlockedByTunnel();
        connect(worker, &QThread::finished, this, [this, round, mustSurvive] {
            if (!round->error.isEmpty()) {
                LOG_WARN("direct DNS probe failed: " + round->error);
                emit Selected({}, true, {}, DirectDnsFailure::ProbeUnavailable);
                return;
            }
            const auto &all = round->results;
            LOG_INFO("direct DNS round outside the tunnel: " + roundSummary(all));
            const auto selection = SelectDirectDns(all, m_configured, mustSurvive, !m_probeName.isEmpty());
            const auto &chosen = selection.address;
            m_resolved = selection.serverAddresses;
            if (!m_resolved.isEmpty() && !m_probeName.isEmpty()) {
                LOG_INFO(QString("%1 resolved through direct DNS: %2").arg(m_probeName, m_resolved.join(", ")));
            }
            if (selection.failure != DirectDnsFailure::None) {
                LOG_WARN(DirectDnsFailureText(selection.failure));
                QStringList failures;
                for (const auto &result : all) {
                    if (!result.error.isEmpty()) failures << QString("%1: %2").arg(result.address, result.error);
                }
                if (!failures.isEmpty()) LOG_WARN("direct DNS probe errors: " + failures.join(" | "));
                emit Selected({}, true, m_resolved, selection.failure);
                return;
            }
            int chosenLatency = -1;
            for (const auto &result : all) {
                if (result.address == chosen) chosenLatency = result.latencyMs;
            }
            if (chosen == m_configured) {
                LOG_INFO(QString("direct DNS %1 answered in %2 ms").arg(m_configured).arg(chosenLatency));
                emit Selected(m_configured, false, m_resolved, DirectDnsFailure::None);
                return;
            }
            LOG_INFO(QString("direct DNS: %1 instead of %2 (%3)")
                         .arg(chosen, m_configured.isEmpty() ? QStringLiteral("none") : m_configured,
                              whyNotConfigured(all)));
            emit Selected(chosen, true, m_resolved, DirectDnsFailure::None);
        });
        worker->start();
    }

} // namespace Configs
