#include "include/ui/mainwindow.h"
#include "NkrVersion.h"
#include "include/ui/fsnt/FsntControls.h"

#include <QApplication>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QThread>
#include <QDesktopServices>
#include <QSysInfo>
#include <QDir>
#include <QRegularExpression>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QThread>
#include <QUrl>
#include <array>
#include <optional>

#include "include/api/RPC.h"
#include "include/api/remote/Server.hpp"
#include "include/configs/generate.h"
#include "include/engine/EngineHost.h"
#include "include/engine/ThreadCheck.h"
#include "include/engine/app/ExitController.h"
#include "include/engine/connection/ConnectionManager.h"
#include "include/engine/services/Dashboard.h"
#include "include/engine/supervisor/CoreSupervisor.h"
#include "include/global/Configs.hpp"
#include "include/global/HTTPRequestHelper.hpp"
#include "include/global/LocalNetwork.hpp"
#include "include/global/Logger.hpp"
#include "include/global/SystemStateJournal.hpp"
#include "include/scanner/ScanManager.h"
#include "include/sys/HostPresence.hpp"
#include "include/sys/KillSwitch.hpp"
#include "include/sys/Process.hpp"
#include "include/sys/SystemProxy.hpp"
#include "include/ui/fsnt/UiMode.hpp"

#include <QProcess>
#include <QSettings>
#include "include/ui/mainWindow/MainWindowInternal.h"

#include "include/ui/app/dialog_update.h"
#include "include/ui/group/dialog_manage_groups.h"
#include "include/ui/setting/dialog_basic_settings.h"
#include "include/ui/setting/dialog_integration.h"
#include "include/ui/setting/dialog_manage_routes.h"
#include "include/ui/setting/dialog_otp_manager.h"
#include "include/ui/setting/dialog_preset_settings.h"
#include "include/ui/setting/dialog_vpn_settings.h"
#include "include/ui/setting/RawRouteItem.h"
#include "include/ui/setting/RouteItem.h"
#include "include/database/RoutesRepo.h"

#ifdef Q_OS_WIN
#include "3rdparty/WinCommander.hpp"
#include "include/sys/windows/WinVersion.h"
#endif

void MainWindow::on_menu_basic_settings_triggered() {
    USE_DIALOG(DialogBasicSettings)
}

void MainWindow::on_menu_manage_groups_triggered() {
    USE_DIALOG(DialogManageGroups)
}

void MainWindow::on_menu_routing_settings_triggered() {
    if (dialog_is_using) return;
    setDialogInUse(true);
    auto dialog = new DialogManageRoutes(this);
    connect(dialog, &QDialog::finished, this, [=,this] {
        dialog->deleteLater();
        setDialogInUse(false);
    });
    dialog->show();
}

void MainWindow::openRouteProfileEditor(int routeId) {
    // Also keeps the routing dialog from saving its stale copy over this edit.
    if (dialog_is_using) return;
    const auto profile = Configs::dataManager->routesRepo->GetRouteProfile(routeId);
    if (profile == nullptr) return;
    setDialogInUse(true);

    const auto onSaved = [this](const std::shared_ptr<Configs::RouteProfile>& chain) {
        if (!Configs::dataManager->routesRepo->Save(chain)) return;
        if (chain->id == Configs::dataManager->settingsRepo->current_route_id)
            connection()->noteRestartNeeded(Engine::RestartReason::Routing);
    };
    QDialog* editor;
    if (profile->isRaw) {
        auto* raw = new RawRouteItem(this, profile);
        connect(raw, &RawRouteItem::settingsChanged, this, onSaved);
        editor = raw;
    } else {
        auto* item = new RouteItem(this, profile);
        connect(item, &RouteItem::settingsChanged, this, onSaved);
        editor = item;
    }
    connect(editor, &QDialog::finished, this, [=, this] {
        editor->deleteLater();
        setDialogInUse(false);
    });
    editor->setWindowModality(Qt::ApplicationModal);
    editor->show();
}

void MainWindow::on_menu_vpn_settings_triggered() {
    USE_DIALOG(DialogVPNSettings)
}

void MainWindow::on_menu_preset_settings_triggered() {
    USE_DIALOG(DialogPresetSettings)
}

void MainWindow::on_menu_otp_manager_triggered() {
    USE_DIALOG(DialogOtpManager)
}

void MainWindow::on_menu_integration_settings_triggered() {
    if (dialog_is_using) return;
    setDialogInUse(true);
    auto dialog = new DialogIntegration(this, getActionsForShortcut());
    connect(dialog, &QDialog::finished, this, [=,this]
    {
        dialog->deleteLater();
        setDialogInUse(false);
    });
    dialog->show();
}

void MainWindow::saveWindowState() {
    auto* settings = Configs::dataManager->settingsRepo.get();

    settings->mainWindowGeometry = this->saveGeometry().toBase64(QByteArray::Base64Encoding);
    if (!isMaximized()) {
        auto news = QString("%1x%2").arg(size().width()).arg(size().height());
        if (settings->mw_size != news) settings->mw_size = news;
    }
    settings->splitter_state = ui->splitter->saveState().toBase64();
}

void MainWindow::on_commitDataRequest() {
    saveWindowState();
    exitController()->persistSessionState();
}

namespace {
#ifdef Q_OS_MACOS
    // Обновлятор на маке заменяет весь .app целиком, а настройки по умолчанию
    // лежат внутри него. После обновления от них не осталось бы и следа —
    // ни подписок, ни серверов. Поэтому перед уходом переносим их туда, где
    // новая версия их найдёт: тот же путь, куда клиент уходит сам, когда папка
    // программы закрыта на запись.
    void moveConfigOutOfBundle() {
        const QDir configDir(QDir::currentPath());   // клиент работает в своём config
        const QString appDir = QApplication::applicationDirPath();
        if (!configDir.absolutePath().startsWith(appDir)) return;   // и так снаружи

        const QDir userWd(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        const QString userConfig = userWd.absoluteFilePath("config");
        QDir().mkpath(userConfig);

        for (const QFileInfo &entry : configDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString target = userConfig + "/" + entry.fileName();
            if (entry.isDir()) continue;   // логи и кэш новая версия наберёт заново
            QFile::remove(target);
            if (!QFile::copy(entry.absoluteFilePath(), target)) {
                MW_show_log("[Update] WARNING: could not copy " + entry.fileName() + " out of the bundle");
            }
        }

        // Метка говорит новой версии, что настройки этой установки живут здесь.
        QFile marker(userWd.absoluteFilePath("config/.install-dir-unwritable"));
        if (marker.open(QIODevice::WriteOnly)) {
            marker.write(QDir(appDir).absolutePath().toUtf8());
            marker.close();
        }
        MW_show_log("[Update] settings moved to " + userConfig);
    }
#endif
}

void MainWindow::on_menu_exit_triggered() {
    // A teardown already running quits on its own; a second request would cut its nested waits short.
    if (Configs::dataManager->settingsRepo->prepare_exit || exitController()->exitInProgress()) return;
    exitController()->setExitInProgress(true);
    // A request made during the teardown below may overwrite exit_reason.
    const auto reason = exit_reason;
    const bool unattended = m_exitUnattended;
    const bool restart = reason == ExitReason::Restart || reason == ExitReason::RestartWithTun ||
                         reason == ExitReason::RestartElevated;
    const auto program = QApplication::applicationFilePath();
    QStringList arguments;
    if (restart) {
        arguments = Configs::dataManager->settingsRepo->argv;
        if (arguments.length() > 0) {
            arguments.removeFirst();
            arguments.removeAll("-tray");
            arguments.removeAll("-flag_restart_tun_on");
        }
        if (reason == ExitReason::RestartWithTun) arguments << "-flag_restart_tun_on";
        if (unattended && m_exitKeepTray) arguments << "-tray";
    }

    bool relaunched = false;
#ifdef Q_OS_WIN
    if (reason == ExitReason::RestartWithTun || reason == ExitReason::RestartElevated) {
        // Asked before the teardown so a declined UAC prompt leaves this instance running; the new one waits for this pid to exit.
        const auto elevatedArguments = arguments + QStringList{QString("-wait_pid=%1").arg(QCoreApplication::applicationPid())};
        const bool launched = WinCommander::runProcessElevated(program, elevatedArguments, QApplication::applicationDirPath(), 1, false) == 0;
        if (!launched) {
            exitController()->setExitInProgress(false);
            exit_reason = ExitReason::None;
            MW_show_log(tr("Restart as administrator was cancelled, Throne keeps running"));
            return;
        }
        relaunched = true;
    }
#endif

    Engine::host()->shutdown(!unattended);
    if (reason == ExitReason::RunUpdater) {
#ifdef Q_OS_MACOS
        moveConfigOutOfBundle();
#endif
        QDir::setCurrent(QApplication::applicationDirPath());
        // Каталог обновления знает только приложение: на Windows он зависит от
        // flag_use_appdata. Передаём явно, чтобы обновлятор не угадывал.
        // Второй аргумент — папка программы. Обновлятор старых версий его не
        // знает и берёт свою собственную; для них копия и лежит рядом с
        // программой, так что обе дороги ведут в одно место.
        // Третьим аргументом — свой PID: обновлятор должен дождаться именно нас,
        // а не любого процесса с именем Throne. Рядом у людей стоят соседние
        // копии, и по имени он ждал их до самого таймаута.
        const QStringList updaterArgs{GetUpdateDirectory(), QApplication::applicationDirPath(),
                                      QString::number(QCoreApplication::applicationPid())};
        const QString runner = update_runner.isEmpty()
                                   ? QApplication::applicationDirPath() + "/updater"
                                   : update_runner;
        if (!QProcess::startDetached(runner, updaterArgs)) {
            MW_show_log("[Update] ERROR: could not start the updater: " + runner);
        }
    } else if (restart && !relaunched) {
        QDir::setCurrent(QApplication::applicationDirPath());
        // Until this process exits it holds the data dir lock and still answers on the instance socket.
        arguments << QString("-wait_pid=%1").arg(QCoreApplication::applicationPid());
        QProcess::startDetached(program, arguments);
    }
    QCoreApplication::quit();
}

void MainWindow::request_unattended_exit(ExitReason reason) {
    if (Configs::dataManager->settingsRepo->prepare_exit || exitController()->exitInProgress()) return;
    m_exitUnattended = true;
    m_exitKeepTray = isHidden();
    exit_reason = reason;
    on_menu_exit_triggered();
}

void MainWindow::toggle_system_proxy() {
    auto currentState = Configs::dataManager->settingsRepo->spmode_system_proxy;
    if (currentState) {
        connection()->setSystemProxy(false);
    } else {
        connection()->setSystemProxy(true);
    }
}

void MainWindow::showFacadeNotice(const QString &text, int milliseconds) {
    auto *facade = GetFacadeWindow();
    if (facade == nullptr) return;
    QMetaObject::invokeMethod(facade, "showNotice", Qt::QueuedConnection, Q_ARG(QString, text),
                              Q_ARG(int, milliseconds));
}


void MainWindow::ClearStaleSystemProxy() {
    auto &settings = Configs::dataManager->settingsRepo;
    if (settings->spmode_system_proxy) return;                      // держим его мы, и сейчас
    if (settings->core_running && settings->started_id >= 0) return;

    // Журнал знает точно, а не угадывает: запись осталась — значит прошлый
    // сеанс включил прокси и не дожил до отката. Работает на всех системах,
    // в отличие от разбора реестра ниже.
    for (const auto &change : Configs::SystemStateJournal::Leftovers()) {
        if (change.kind != QLatin1String("system-proxy")) continue;
        SystemProxy_Clear();
        Configs::SystemStateJournal::Forget(change.kind);
#ifdef Q_OS_WIN
        for (const auto &saved : Configs::SystemStateJournal::Leftovers()) {
            if (saved.kind == QLatin1String("system-proxy-previous")) SystemProxy_Restore(saved.detail);
        }
        Configs::SystemStateJournal::Forget(QStringLiteral("system-proxy-previous"));
#endif
        LOG_INFO("system proxy left over from the previous session was cleared: " + change.detail);
        MW_show_log(tr("Cleared the system proxy left over from the previous session"));
        return;
    }

#ifdef Q_OS_WIN
    // Запасной путь для тех, кто обновился с версии без журнала: там записи
    // неоткуда взяться, а прокси в системе вполне может висеть.

    // Снимается он только в prepare_exit. Сеанс оборвался иначе — ядро убито,
    // упало, приложение закрыли жёстко, — и в системе остаётся прокси на
    // мёртвый локальный порт. Через WinINET это бьёт не только по браузеру:
    // туда же ходят установщики, обновляторы и всё офисное.
    QSettings ie("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
                 QSettings::NativeFormat);
    if (ie.value("ProxyEnable", 0).toInt() == 0) return;

    const auto current = ie.value("ProxyServer").toString();
    // Только свой: у человека может стоять и чужой локальный прокси.
    const auto ours = QString(":%1").arg(settings->inbound_socks_port);
    if (!current.contains(ours)) return;

    SystemProxy_Clear();
    LOG_INFO("system proxy left over from the previous session was cleared: " + current);
    MW_show_log(tr("Cleared the system proxy left over from the previous session"));
#endif
}

#ifdef Q_OS_MACOS
namespace {
    // Снимает подменённый DNS у тех сетевых служб, где он указывает внутрь
    // нашего туннеля. Прав на это не нужно: networksetup проводит изменение
    // через configd, root требуется только для резервной копии plist.
    int clearDnsPointingAtTun(const QString &tunAddress) {
        QProcess list;
        list.start("/usr/sbin/networksetup", {"-listallnetworkservices"});
        list.waitForFinished(5000);
        int cleared = 0;
        const auto services = QString::fromUtf8(list.readAllStandardOutput()).split('\n');
        for (const auto &raw : services) {
            const auto service = raw.trimmed();
            // Первая строка — заголовок, звёздочкой помечены отключённые службы.
            if (service.isEmpty() || service.startsWith('*') || service.contains("denotes that")) continue;

            QProcess current;
            current.start("/usr/sbin/networksetup", {"-getdnsservers", service});
            current.waitForFinished(5000);
            if (!QString::fromUtf8(current.readAllStandardOutput()).contains(tunAddress)) continue;

            QProcess reset;
            reset.start("/usr/sbin/networksetup", {"-setdnsservers", service, "empty"});
            reset.waitForFinished(5000);
            LOG_INFO("system DNS of \"" + service + "\" pointed into a tunnel that is gone, reset");
            ++cleared;
        }
        return cleared;
    }
} // namespace
#endif

void MainWindow::ClearStaleSystemDnsTunnel() {
#ifdef Q_OS_MACOS
    // На macOS системный DNS подменяет само ядро при каждом запуске туннеля, и
    // снимает подмену только в Stop. Не дожил сеанс до Stop — резолвер смотрит
    // внутрь исчезнувшего интерфейса, и не работает ничего: ни через VPN, ни
    // мимо. В базе следа нет, флаг system_dns_set на маке никем не ставится,
    // поэтому знать об этом может только журнал.
    for (const auto &change : Configs::SystemStateJournal::Leftovers()) {
        if (change.kind != QLatin1String("system-dns-tunnel")) continue;
        const auto address = change.detail;
        if (address.isEmpty()) continue;
        if (clearDnsPointingAtTun(address) > 0) {
            MW_show_log(tr("Restored the system DNS left over from the previous session"));
        }
        Configs::SystemStateJournal::Forget(change.kind);
        return;
    }
#endif
}

void MainWindow::toggle_tun() {
    const auto state = connection()->connectionState();
    if (state == ConnectionState::Connecting || state == ConnectionState::Stopping) return;
    connection()->setTun(!Configs::dataManager->settingsRepo->spmode_vpn);
}

bool MainWindow::StopVPNProcess() {
    supervisor()->killForPrivilegeChange();
    return true;
}

void MainWindow::show_startstop_menu() {
    if (!Configs::dataManager->settingsRepo->kill_switch) return;
    QMenu menu(this);
    connect(menu.addAction(tr("Disable Kill Switch")), &QAction::triggered, this, [this] { confirm_disable_kill_switch(); });
    auto *button = ui->toolButton_startstop;
    menu.exec(button->mapToGlobal(QPoint(0, button->height())));
}

void MainWindow::confirm_disable_kill_switch() {
    auto *box = new QMessageBox(QMessageBox::Warning, tr("Disable kill switch"),
                                tr("Turn off the kill switch? Traffic that does not go through Throne will no longer be blocked."),
                                QMessageBox::NoButton, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    auto *disable = box->addButton(tr("Disable Kill Switch"), QMessageBox::DestructiveRole);
    auto *cancel = box->addButton(QMessageBox::Cancel);
    // Cancel is the default, so a stray Enter cannot complete the two-step disable.
    box->setDefaultButton(cancel);
    box->setEscapeButton(cancel);
    connect(box, &QMessageBox::finished, this, [this, box, disable] {
        if (box->clickedButton() == disable) connection()->disableKillSwitch();
    });
    box->open();
}

void MainWindow::on_menu_simple_mode_triggered() {
    const auto answer = QMessageBox::question(
        this, tr("Simple mode"),
        tr("The application will restart in the simple interface. Continue?"));
    if (answer != QMessageBox::StandardButton::Yes) return;

    Configs::dataManager->settingsRepo->ui_mode = static_cast<int>(Fsnt::UiMode::Simple);
    Configs::dataManager->settingsRepo->Save();
    this->exit_reason = ExitReason::Restart;
    on_menu_exit_triggered();
}

namespace {

// Ассеты форка называются Throne-<platform>-<arch>.zip и версии в имени не содержат,
// поэтому сравнение идёт по тегу релиза, а не по имени файла.
bool compareVersions(const QStringList& parts, const QStringList& currentParts) {
    if (parts.size() < 3 || currentParts.size() < 3) {
        MW_show_log("Version strings seem to be invalid in compareVersions");
        return false;
    }

    std::vector<int> verNums;
    std::vector<int> currNums;

    verNums.push_back(parts[0].toInt());
    verNums.push_back(parts[1].toInt());
    verNums.push_back(parts[2].toInt());
    if (parts.size() > 3) {
        if (parts[3] == "alpha") verNums.push_back(1);
        if (parts[3] == "beta") verNums.push_back(2);
        if (parts[3] == "rc") verNums.push_back(3);
        if (parts.size() > 4) verNums.push_back(parts[4].toInt());
    }

    currNums.push_back(currentParts[0].toInt());
    currNums.push_back(currentParts[1].toInt());
    currNums.push_back(currentParts[2].toInt());
    if (currentParts.size() > 3) {
        if (currentParts[3] == "alpha") currNums.push_back(1);
        if (currentParts[3] == "beta") currNums.push_back(2);
        if (currentParts[3] == "rc") currNums.push_back(3);
        if (currentParts.size() > 4) currNums.push_back(currentParts[4].toInt());
    }

    if (verNums.size() < 3 || currNums.size() < 3) {
        MW_show_log("Version parsing failed in compareVersions");
        return false;
    }

    for (int i = 0; i < 3; i++) {
        if (verNums[i] > currNums[i]) return true;
        if (verNums[i] < currNums[i]) return false;
    }

    if (verNums.size() == 5 && currNums.size() == 3) return false;
    if (verNums.size() == 3 && currNums.size() == 5) return true;
    if (verNums.size() == 5 && currNums.size() == 5) {
        for (int i = 3; i < 5; i++) {
            if (verNums[i] > currNums[i]) return true;
            if (verNums[i] < currNums[i]) return false;
        }
    }
    return false;
}

bool isNewerByTag(const QString& releaseTag) {
    if (QString(NKR_VERSION).isEmpty()) return false;

    QString version = releaseTag;
    if (version.startsWith('v') || version.startsWith('V')) {
        version = version.mid(1);
    }

    auto parts = version.replace("-", ".").split(".");
    auto currentParts = QString(NKR_VERSION).replace("-", ".").split('.');

    return compareVersions(parts, currentParts);
}

constexpr auto dashboardDownloadURL = "https://github.com/SagerNet/sing-box-dashboard/archive/refs/heads/gh-pages.zip";

} // namespace

void MainWindow::OpenDashboard() {
    const auto &settings = *Configs::dataManager->settingsRepo;
    const auto port = settings.core_box_api_port;
    if (port <= 0) {
        MessageBoxWarning(software_name, tr("The sing-box API is disabled. Set a listen port in Preferences > Basic Settings > Core."));
        return;
    }
    if (settings.started_id < 0) {
        MessageBoxWarning(software_name, tr("Start a profile first; the dashboard is served by the running core."));
        return;
    }

    const auto show = [this, port] {
        Engine::Dashboard::SeedApi();
        // Fragment, not query: browsers never send it to the server.
        QUrl url(QString("http://127.0.0.1:%1/dashboard/throne.html").arg(port));
        url.setFragment(QString("secret=%1&url=127.0.0.1:%2")
                            .arg(QString::fromUtf8(QUrl::toPercentEncoding(Configs::dataManager->settingsRepo->core_box_api_secret)))
                            .arg(port),
                        QUrl::StrictMode);
        QDesktopServices::openUrl(url);
    };

    Engine::Dashboard::SeedApi();
    if (QFile::exists(QDir(Configs::apiDashboardDir).filePath("index.html"))) {
        show();
        return;
    }

    if (QMessageBox::question(this, tr("Web dashboard"),
                              tr("The dashboard is not installed yet. Download it now?"))
        != QMessageBox::StandardButton::Yes) {
        return;
    }

    runOnNewThread([=, this] {
        if (!mu_download_dashboard.tryLock()) {
            runOnUiThread([=, this] {
                MessageBoxWarning(tr("Cannot start"), tr("A dashboard download is already running"));
            });
            return;
        }
        const auto archive = QString("throne-dashboard.zip");
        auto error = NetworkRequestHelper::DownloadAsset(dashboardDownloadURL, archive, true);
        if (error.isEmpty()) {
            bool ok = false;
            error = API::defaultClient->InstallDashboard(&ok, Configs::GetBasePath() + "/" + archive,
                                                         QDir(Configs::apiDashboardDir).absolutePath());
            if (!ok && error.isEmpty()) error = tr("The core did not answer.");
        }
        QFile::remove(Configs::GetBasePath() + "/" + archive);
        mu_download_dashboard.unlock();

        runOnUiThread([=, this] {
            if (!error.isEmpty()) {
                MessageBoxWarning(tr("Failed to install the dashboard"), error);
                return;
            }
            show();
        });
    });
}

QString MainWindow::GetUpdateDirectory() {
#ifdef Q_OS_WIN
    if (Configs::dataManager->settingsRepo->flag_use_appdata) {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/Throne_update";
    }
    return QApplication::applicationDirPath() + "/Throne_update";
#else
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/Throne_update";
#endif
}

bool MainWindow::PrepareUpdateEnvironment() {
    QString updateDir = GetUpdateDirectory();
    MW_show_log(QString("[Update] Preparing update environment: %1").arg(updateDir));

    QDir dir;
    if (!dir.exists(updateDir) && !dir.mkpath(updateDir)) {
        MW_show_log("[Update] ERROR: Failed to create update directory!");
        return false;
    }

    // Чистим остатки прошлой попытки обновления
    QDir updateFolder(updateDir);
    updateFolder.setNameFilters(QStringList() << "Throne.zip" << "extracted" << "*.log");
    updateFolder.setFilter(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : updateFolder.entryList()) {
        const QString path = updateDir + "/" + entry;
        if (QFileInfo(path).isDir()) {
            QDir(path).removeRecursively();
        } else {
            QFile::remove(path);
        }
    }

    MW_show_log("[Update] Update environment prepared");
    return true;
}

void MainWindow::InstallUpdateAndRestart(const QString &version) {
    MW_show_log("[Update] Starting update installation...");

#ifdef Q_OS_WIN
    const QString updaterPath = QApplication::applicationDirPath() + "/updater.exe";
#else
    const QString updaterPath = QApplication::applicationDirPath() + "/updater";
#endif

    if (!QFile::exists(updaterPath)) {
        MW_show_log("[Update] ERROR: Updater not found!");
        MessageBoxWarning(tr("Update"), tr("Updater not found! Please download update manually."));
        return;
    }

    // Обновлятор заменяет и сам себя, поэтому запускаем копию. Копия ложится
    // рядом с программой намеренно: обновлятор из установленной версии считает
    // своей папкой ту, из которой запущен, и разложит файлы именно в неё.
    // Заодно это и есть проверка на запись: если копия не легла, то и файлы
    // программы заменить не выйдет — там Program Files или права урезаны.
#ifdef Q_OS_WIN
    update_runner = QApplication::applicationDirPath() + "/updater.old";
    QFile::remove(update_runner);
    if (!QFile::copy(updaterPath, update_runner)) {
        update_runner.clear();
        MW_show_log("[Update] ERROR: the install folder is not writable, cannot prepare the updater");
        MessageBoxWarning(tr("Update"),
                          tr("The program folder is closed for writing, so the update cannot install "
                             "itself. Download the new version from the release page and install it "
                             "over the current one."));
        return;
    }
#else
    update_runner = updaterPath;
#endif

    // Оставляем метку с версией, которую ждём. Обновлятор работает уже без
    // окна: если он умрёт молча — антивирус съел, PowerShell закрыт политикой,
    // архив побился, — при следующем запуске метка останется, и мы об этом
    // скажем, вместо того чтобы делать вид, что всё прошло.
    if (!version.isEmpty()) {
        QFile marker(QDir(GetUpdateDirectory()).filePath("pending-version"));
        if (marker.open(QIODevice::WriteOnly | QIODevice::Text)) {
            marker.write(version.trimmed().toUtf8());
            marker.close();
        }
    }

    // Запоминаем, куда возвращаться. Обычная память о последнем профиле
    // работает только при включённом «подключаться при запуске», а обновление
    // человек затевал не для того, чтобы остаться без туннеля.
    const auto &settings = Configs::dataManager->settingsRepo;
    if (settings->started_id >= 0) {
        settings->remember_id = settings->started_id;
        settings->remember_tun = settings->spmode_vpn;
        settings->remember_system_proxy = settings->spmode_system_proxy;
        settings->resume_after_update = true;
        settings->Save();
    }

    this->exit_reason = ExitReason::RunUpdater;
    MW_show_log("[Update] Closing application to start updater...");
    on_menu_exit_triggered();
}

void MainWindow::ReportPreviousUpdate() {
    const QString markerPath = QDir(GetUpdateDirectory()).filePath("pending-version");
    QFile marker(markerPath);
    if (!marker.exists()) return;

    QString wanted;
    if (marker.open(QIODevice::ReadOnly | QIODevice::Text)) {
        wanted = QString::fromUtf8(marker.readAll()).trimmed();
        marker.close();
    }
    QFile::remove(markerPath);
    if (wanted.isEmpty()) return;

    // Сравниваем по цифрам: в метке лежит имя тега, оно может прийти с "v".
    const QString wantedDigits = QString(wanted).remove(QRegularExpression("[^0-9.]"));
    if (wantedDigits.isEmpty() || wantedDigits == QString(NKR_VERSION)) {
        MW_show_log("[Update] Updated to " + QString(NKR_VERSION));
        return;
    }

    MW_show_log("[Update] ERROR: expected " + wanted + " after the update, but this is " + QString(NKR_VERSION));
    MessageBoxWarning(tr("Update"),
                      tr("The update to %1 did not install: the program is still %2. This usually means "
                         "an antivirus stopped the updater. Download the new version from the release "
                         "page and install it over the current one.\n\nDetails: %3")
                          .arg(wanted, QString(NKR_VERSION), QDir(GetUpdateDirectory()).filePath("updater.log")));
}

void MainWindow::CheckUpdate(bool autoInstall) {
    QString search;
#ifdef Q_OS_WIN
#  ifdef Q_PROCESSOR_ARM_64
    search = "windows-arm64";
#  else
#    ifdef Q_OS_WIN64
        if (WinVersion::IsBuildNumGreaterOrEqual(BuildNumber::Windows_10_1809))
            search = "windows64";
        else
	        search = "windowslegacy64";
#    else
	    search = "windows32";
#    endif
#  endif
#endif
#ifdef Q_OS_LINUX
#  ifdef Q_PROCESSOR_X86_64
    search = "linux-amd64";
#  else
    search = "linux-arm64";
#  endif
#endif
#ifdef Q_OS_MACOS
#  ifdef Q_PROCESSOR_X86_64
	search = "macos-amd64";
#  else
	search = "macos-arm64";
#  endif
#endif
    if (search.isEmpty()) {
        runOnUiThread([=,this] {
            MessageBoxWarning(QObject::tr("Update"), QObject::tr("Not official support platform"));
        });
        return;
    }

    // Releases carry no checksum or signature, so TLS is all that vouches for the download URL and the archive.
    HttpGetOptions options;
    options.strictTls = true;
    auto resp = NetworkRequestHelper::HttpGet("https://api.github.com/repos/forestsnet/Throne/releases", options);
    if (!resp.error.isEmpty()) {
        runOnUiThread([=,this] {
            MessageBoxWarning(QObject::tr("Update"), QObject::tr("Requesting update error: %1").arg(resp.error + "\n" + resp.data));
        });
        return;
    }

    QString assets_name, release_download_url, release_url, release_note, note_pre_release, release_tag_name;
    bool exitFlag = false;
    QJsonArray array = QString2QJsonArray(resp.data);
    for (const QJsonValue value : array) {
        QJsonObject release = value.toObject();
        if (release["prerelease"].toBool() && !Configs::dataManager->settingsRepo->allow_beta_update) continue;
        for (const QJsonValue asset : release["assets"].toArray()) {
            if (asset["name"].toString().contains(search) && asset["name"].toString().section('.', -1) == QString("zip")) {
                note_pre_release = release["prerelease"].toBool() ? " (Pre-release)" : "";
                release_tag_name = release["tag_name"].toString();
                release_url = release["html_url"].toString();
                release_note = release["body"].toString();
                assets_name = asset["name"].toString();
                release_download_url = asset["browser_download_url"].toString();
                exitFlag = true;
                break;
            }
        }
        if (exitFlag) break;
    }

    if (release_download_url.isEmpty() || !isNewerByTag(release_tag_name)) {
        runOnUiThread([=,this] {
            MessageBoxInfo(QObject::tr("Update"), QObject::tr("No update"));
        });
        return;
    }

    runOnUiThread([=,this] {
        // Своя копия, работающая в фоне из этой же установки, держит файлы:
        // распаковка поверх них молча не доедет.
        const auto headless = Sys::HostPresence::OtherHeadless();
        const bool headlessSameInstall = headless && Sys::HostPresence::SameInstall(*headless, QCoreApplication::applicationDirPath());
        // Свой обновлятор умеет ставить обновление и при flag_use_appdata.
        auto allow_updater = !headlessSameInstall;
#ifdef Q_OS_LINUX
        // AppImage — один файл, и заменить его распаковкой архива поверх нельзя:
        // он примонтирован и доступен только для чтения. Отправляем человека за
        // свежим образом — это ровно одно скачивание и перезапуск.
        if (Configs::RunningFromAppImage()) {
            const QString arch = QSysInfo::currentCpuArchitecture() == QStringLiteral("arm64")
                                     ? QStringLiteral("aarch64")
                                     : QStringLiteral("x86_64");
            const QString link = QStringLiteral(
                "https://github.com/forestsnet/Throne/releases/latest/download/FSNT-Client-%1.AppImage")
                                     .arg(arch);
            if (Fsnt::Confirm(GetMessageBoxParent(), QObject::tr("Update"),
                              QObject::tr("Version %1 is out. The client runs from an AppImage, so it "
                                          "cannot replace itself: download the new file and start it "
                                          "instead of this one.")
                                  .arg(release_tag_name),
                              QObject::tr("Download"))) {
                QDesktopServices::openUrl(QUrl(link));
            }
            return;
        }
#endif
        const QString updateTitle = QObject::tr("Update") + note_pre_release;
        QString updateBody = QObject::tr("Update found: %1\nRelease note:\n%2")
                                 .arg(assets_name, release_note);
        if (headlessSameInstall) {
            updateBody += "\n\n" + tr("Update is unavailable while a background Throne (PID %1) runs from this installation; stop it first.")
                                         .arg(headless->pid);
        }
        // 0 — ставить, 1 — открыть страницу релиза, -1 — ничего.
        // Согласие уже получено карточкой обновления — второй раз спрашивать
        // то же самое незачем.
        int picked = -1;
        if (autoInstall) {
            picked = 0;
            showFacadeNotice(tr("Downloading the update…"), 120000);
        } else if (GetFacadeWindow() != nullptr) {
            picked = Fsnt::Choose(GetMessageBoxParent(), updateTitle, updateBody,
                                  {QObject::tr("Update"), QObject::tr("Open in browser"), QObject::tr("Close")});
            if (picked == 2) picked = -1;
        } else {
            QMessageBox box(QMessageBox::Question, updateTitle, updateBody);
            QAbstractButton *btn1 = box.addButton(QObject::tr("Update"), QMessageBox::AcceptRole);
            QAbstractButton *btn2 = box.addButton(QObject::tr("Open in browser"), QMessageBox::AcceptRole);
            box.addButton(QObject::tr("Close"), QMessageBox::RejectRole);
            box.exec();
            if (box.clickedButton() == btn1) picked = 0;
            else if (box.clickedButton() == btn2) picked = 1;
        }
        if (picked == 0 && allow_updater) {
            runOnNewThread([=,this] {
                if (!mu_download_update.tryLock()) {
                    runOnUiThread([=,this](){
                        MessageBoxWarning(tr("Cannot start"), tr("Last download request has not finished yet"));
                    });
                    return;
                }
                MW_show_log("[Update] Starting update download...");
                if (!PrepareUpdateEnvironment()) {
                    mu_download_update.unlock();
                    runOnUiThread([=,this](){
                        MessageBoxWarning(tr("Update"), tr("Failed to prepare update environment!"));
                    });
                    return;
                }
                const QString updateDir = GetUpdateDirectory();

                QString errors;
                if (!release_download_url.isEmpty()) {
                    MW_show_log(QString("[Update] Downloading from: %1").arg(release_download_url));
                    auto res = NetworkRequestHelper::DownloadAsset(release_download_url, "Throne.zip", false, true, updateDir);
                    if (!res.isEmpty()) {
                        MW_show_log(QString("[Update] Download error: %1").arg(res));
                        errors += res;
                    } else {
                        MW_show_log("[Update] Download completed successfully");
                    }
                }
                mu_download_update.unlock();
                runOnUiThread([=,this] {
                    if (errors.isEmpty()) {
                        const QString readyTitle = QObject::tr("Update");
                        const QString readyBody = QObject::tr("Update is ready, restart to install?");
                        const bool restartNow =
                            autoInstall
                                ? true
                                : (GetFacadeWindow() != nullptr
                                       ? Fsnt::Confirm(GetMessageBoxParent(), readyTitle, readyBody, QObject::tr("Restart"))
                                       : QMessageBox::question(nullptr, readyTitle, readyBody) == QMessageBox::StandardButton::Yes);
                        if (restartNow) {
                            InstallUpdateAndRestart(release_tag_name);
                        } else {
                            MW_show_log("[Update] Installation postponed.");
                            MessageBoxInfo(tr("Update"), tr("Update is ready. The update will be installed on next application restart."));
                        }
                    } else {
                        MessageBoxWarning(tr("Failed to download update assets"), errors);
                    }
                });
            });
        } else if (picked == 1) {
            QDesktopServices::openUrl(QUrl(release_url));
        }
    });
}
