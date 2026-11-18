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

    // Второй проход: остальные имена конфига, уже по выбранному резолверу.
    // Неразрешённое имя не отменяет подключения — его спросят под туннелем, как
    // было до этого всегда, и отказываться из-за него было бы хуже.
    void DnsAutoSelect::finish(const QString &chosen, const bool fellBack, const DirectDnsFailure failure) {
        if (chosen.isEmpty() || m_extraNames.isEmpty()) {
            emit Selected(chosen, fellBack, m_pins, failure);
            return;
        }
        auto round = std::make_shared<DirectDnsProbeRound>();
        auto *worker = QThread::create([round, chosen, timeout = m_timeoutMs, name = m_probeName, extra = m_extraNames] {
            *round = ProbeDirectDns({chosen}, timeout, name, extra);
        });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        connect(worker, &QThread::finished, this, [this, chosen, fellBack, failure, round] {
            if (!round->error.isEmpty()) {
                LOG_WARN("direct DNS second pass failed: " + round->error);
            } else if (!round->results.isEmpty()) {
                const auto &extra = round->results.first().extraAddresses;
                for (auto it = extra.cbegin(); it != extra.cend(); ++it) {
                    if (it.value().isEmpty()) continue;
                    m_pins.insert(it.key(), it.value());
                }
            }
            const int asked = m_extraNames.size() + (m_probeName.isEmpty() ? 0 : 1);
            LOG_INFO(QString("pinned %1 of %2 server names through %3").arg(m_pins.size()).arg(asked).arg(chosen));
            emit Selected(chosen, fellBack, m_pins, failure);
        });
        worker->start();
    }

    void DnsAutoSelect::Start(const QString &configured, int timeoutMs, const QString &probeName,
                              const QStringList &extraNames) {
        m_configured = configured.trimmed();
        m_probeName = probeName.trimmed();
        m_extraNames.clear();
        for (const auto &extra : extraNames) {
            const auto name = extra.trimmed();
            if (name.isEmpty() || name.compare(m_probeName, Qt::CaseInsensitive) == 0) continue;
            if (m_extraNames.contains(name, Qt::CaseInsensitive)) continue;
            m_extraNames.append(name);
        }
        m_timeoutMs = timeoutMs;

        auto candidates = DnsCandidates(m_configured, true);
        if (candidates.size() > kMaxProbes) candidates = candidates.mid(0, kMaxProbes);
        if (candidates.isEmpty()) {
            LOG_WARN("direct DNS: nothing left to try");
            finish({}, true, DirectDnsFailure::ProbeUnavailable);
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
                finish({}, true, DirectDnsFailure::ProbeUnavailable);
                return;
            }
            const auto &all = round->results;
            LOG_INFO("direct DNS round outside the tunnel: " + roundSummary(all));
            const auto selection = SelectDirectDns(all, m_configured, mustSurvive, !m_probeName.isEmpty());
            const auto &chosen = selection.address;
            m_pins.clear();
            if (!selection.serverAddresses.isEmpty() && !m_probeName.isEmpty()) {
                m_pins.insert(m_probeName, selection.serverAddresses);
                LOG_INFO(QString("%1 resolved through direct DNS: %2")
                             .arg(m_probeName, selection.serverAddresses.join(", ")));
            }
            if (selection.failure != DirectDnsFailure::None) {
                LOG_WARN(DirectDnsFailureText(selection.failure));
                QStringList failures;
                for (const auto &result : all) {
                    if (!result.error.isEmpty()) failures << QString("%1: %2").arg(result.address, result.error);
                }
                if (!failures.isEmpty()) LOG_WARN("direct DNS probe errors: " + failures.join(" | "));
                finish({}, true, selection.failure);
                return;
            }
            int chosenLatency = -1;
            for (const auto &result : all) {
                if (result.address == chosen) chosenLatency = result.latencyMs;
            }
            if (chosen == m_configured) {
                LOG_INFO(QString("direct DNS %1 answered in %2 ms").arg(m_configured).arg(chosenLatency));
                finish(m_configured, false, DirectDnsFailure::None);
                return;
            }
            LOG_INFO(QString("direct DNS: %1 instead of %2 (%3)")
                         .arg(chosen, m_configured.isEmpty() ? QStringLiteral("none") : m_configured,
                              whyNotConfigured(all)));
            finish(chosen, true, DirectDnsFailure::None);
        });
        worker->start();
    }

} // namespace Configs
