#include "include/ui/fsnt/Diagnostics.hpp"

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QLocale>
#include <QNetworkInterface>
#include <QSettings>

#include "include/database/GroupsRepo.h"
#include "include/global/Configs.hpp"
#include "include/global/HTTPRequestHelper.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/fsnt/Transport.hpp"
#include "include/ui/mainwindow.h"

#ifdef Q_OS_WIN
#include "include/sys/windows/guihelper.h"
#endif

namespace {
    using Fsnt::CheckResult;

    CheckResult diagOk() { return {}; }

    CheckResult diagSkipped(const QString &why) {
        return {CheckResult::Skipped, why, {}, {}};
    }

    CheckResult diagFailed(const QString &detail, const QString &fixLabel = {},
                       std::function<void()> fix = {}) {
        return {CheckResult::Failed, detail, fixLabel, std::move(fix)};
    }

    CheckResult diagWarn(const QString &detail, const QString &fixLabel = {},
                     std::function<void()> fix = {}) {
        return {CheckResult::Warning, detail, fixLabel, std::move(fix)};
    }

    bool diagTunnelRunning() {
        const auto &s = Configs::dataManager->settingsRepo;
        return s->core_running && s->started_id >= 0;
    }

    // Страница без содержимого: нужен сам факт ответа и заголовок Date.
    const auto kDiagProbeUrl = QStringLiteral("http://www.msftconnecttest.com/connecttest.txt");

    QString diagHeaderValue(const Configs_network::HTTPResponse &response, const QByteArray &name) {
        for (const auto &[key, value] : response.header) {
            if (key.compare(name, Qt::CaseInsensitive) == 0) return QString::fromLatin1(value);
        }
        return {};
    }
}

namespace Fsnt {
    QList<Check> BuildChecks() {
        QList<Check> checks;
        const auto &settings = Configs::dataManager->settingsRepo;

        checks << Check{
            QObject::tr("Core files"), [] {
                auto *mw = GetMainWindow();
                if (mw == nullptr) return diagOk();
                if (mw->EnsureCorePresent(false)) return diagOk();
                // Самый частый виновник — антивирус: ядро для него выглядит
                // подозрительно, и он уносит файл в карантин молча.
                return diagFailed(QObject::tr("The core executable is missing. Antivirus software "
                                          "often quarantines it without asking."),
                              QObject::tr("Download again"), [] {
                                  if (auto *m = GetMainWindow(); m != nullptr) {
                                      m->EnsureCorePresent(true);
                                  }
                              });
            }};

        checks << Check{
            QObject::tr("Subscription"), [] {
                const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
                if (group && !group->Profiles().isEmpty()) return diagOk();
                return diagFailed(QObject::tr("No servers in the current subscription. Add the link "
                                          "your provider gave you, or refresh the subscription."));
            }};

#ifdef Q_OS_WIN
        checks << Check{
            QObject::tr("Administrator rights"), [] {
                if (Configs::dataManager->settingsRepo->simple_transport != 0) {
                    return diagSkipped(QObject::tr("Only needed for the full tunnel."));
                }
                if (Windows_IsInAdmin()) return diagOk();
                return diagFailed(QObject::tr("The full tunnel needs administrator rights to create "
                                          "its network adapter. Restart the client as "
                                          "administrator."));
            }};
#endif

        checks << Check{
            QObject::tr("Conflicting programs"), [] {
                auto *mw = GetMainWindow();
                if (mw == nullptr) return diagOk();
                const auto found = mw->CheckConflictingProcesses();
                if (found.isEmpty()) return diagOk();
                // Предупреждение, а не ошибка: часть из них уживается мирно, и
                // объявлять поломку по одному лишь факту запуска нельзя.
                return diagWarn(QObject::tr("Running: %1. Other VPN clients and traffic filters "
                                        "intercept packets before they reach the tunnel.")
                                .arg(found.join(", ")));
            }};

        checks << Check{
            QObject::tr("Connection"), [] {
                if (!Configs::dataManager->settingsRepo->core_running) {
                    return diagFailed(QObject::tr("The core is not running. Press the power button."));
                }
                if (Configs::dataManager->settingsRepo->started_id < 0) {
                    return diagFailed(QObject::tr("Not connected. Press the power button."));
                }
                return diagOk();
            }};

        checks << Check{
            QObject::tr("Connection mode"), [] {
                const auto &s = Configs::dataManager->settingsRepo;
                if (!diagTunnelRunning()) return diagSkipped(QObject::tr("Connect first."));

                if (s->simple_transport == 0) {
                    if (s->spmode_vpn) return diagOk();
                    return diagFailed(QObject::tr("Full tunnel is selected, but the tunnel mode is "
                                              "not active."),
                                  QObject::tr("Apply"), [] { ApplyTransportMode(); });
                }
#ifdef Q_OS_WIN
                // Здесь уже ловили настоящую поломку: адрес прокси в реестре
                // был, а флаг остался нулевым, и трафик шёл мимо VPN.
                const QSettings ie(
                    QStringLiteral(
                        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"),
                    QSettings::NativeFormat);
                if (ie.value(QStringLiteral("ProxyEnable"), 0).toInt() == 0) {
                    return diagFailed(QObject::tr("System proxy is selected, but Windows has it "
                                              "switched off, so traffic bypasses the VPN."),
                                  QObject::tr("Turn on"), [] { ApplyTransportMode(); });
                }
#endif
                if (s->spmode_system_proxy) return diagOk();
                return diagFailed(QObject::tr("System proxy is selected, but it is not active."),
                              QObject::tr("Apply"), [] { ApplyTransportMode(); });
            }};

        checks << Check{
            QObject::tr("Tunnel adapter"), [] {
                const auto &s = Configs::dataManager->settingsRepo;
                if (s->simple_transport != 0) return diagSkipped(QObject::tr("Not used in proxy mode."));
                if (!diagTunnelRunning()) return diagSkipped(QObject::tr("Connect first."));

                for (const auto &iface : QNetworkInterface::allInterfaces()) {
                    if (!iface.humanReadableName().contains(QStringLiteral("throne"),
                                                            Qt::CaseInsensitive)) {
                        continue;
                    }
                    if (iface.flags().testFlag(QNetworkInterface::IsUp)) return diagOk();
                }
                return diagFailed(QObject::tr("The tunnel adapter was not created. Usually this is "
                                          "missing administrator rights or an antivirus blocking "
                                          "the driver."));
            }};

        checks << Check{
            QObject::tr("Traffic through the tunnel"), [] {
                if (!diagTunnelRunning()) return diagSkipped(QObject::tr("Connect first."));
                // Мимо своего локального прокси: запрос должен пройти тем же
                // путём, что и трафик обычной программы, иначе проверка мерит
                // не тот участок и на сломанном туннеле показывает «всё хорошо».
                const auto response = Configs_network::NetworkRequestHelper::HttpGet(kDiagProbeUrl);
                if (response.error.isEmpty()) return diagOk();

#ifdef Q_OS_WIN
                const auto &s = Configs::dataManager->settingsRepo;
                if (s->simple_transport == 0
                    && s->vpn_implementation == QLatin1String("system")) {
                    return diagFailed(
                        QObject::tr("The tunnel is up, but nothing passes through it. The fast "
                                    "tunnel mode gives packets to Windows, where another "
                                    "program's filter takes them."),
                        QObject::tr("Switch to compatible"), [] {
                            auto &st = Configs::dataManager->settingsRepo;
                            st->vpn_implementation = QStringLiteral("gvisor");
                            st->Save();
                            if (auto *m = GetMainWindow(); m != nullptr && st->started_id >= 0) {
                                m->profile_start(st->started_id);
                            }
                        });
                }
#endif
                return diagFailed(QObject::tr("The tunnel is up, but nothing passes through it: %1")
                                  .arg(response.error));
            }};

        checks << Check{
            QObject::tr("System clock"), [] {
                const auto response = Configs_network::NetworkRequestHelper::HttpGet(kDiagProbeUrl);
                const QString date = diagHeaderValue(response, "Date");
                if (date.isEmpty()) return diagSkipped(QObject::tr("No answer to compare against."));

                const auto server = QLocale::c().toDateTime(
                    date.left(date.size() - 4), QStringLiteral("ddd, dd MMM yyyy HH:mm:ss"));
                if (!server.isValid()) return diagSkipped(QObject::tr("Could not read server time."));

                const auto serverUtc = QDateTime(server.date(), server.time(), Qt::UTC);
                const qint64 skew = qAbs(serverUtc.secsTo(QDateTime::currentDateTimeUtc()));
                if (skew < 120) return diagOk();
                // Сбитые часы ломают проверку сертификатов, и соединение
                // рвётся на рукопожатии без единого внятного сообщения.
                return diagFailed(QObject::tr("The clock is off by %1 minutes. Certificate checks "
                                          "fail and connections break during the handshake. "
                                          "Turn on automatic time in Windows settings.")
                                  .arg(skew / 60));
            }};

        Q_UNUSED(settings)
        return checks;
    }
} // namespace Fsnt
