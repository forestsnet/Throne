#include "include/ui/mainwindow.h"
#include "NkrVersion.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QThread>
#include <QUrl>

#include "include/database/ProfilesRepo.h"
#include "include/database/GroupsRepo.h"
#include "include/global/Configs.hpp"
#include "include/global/Logger.hpp"
#include "include/ui/mainWindow/MainWindowInternal.h"

// Сбор отладочной информации в один архив (Ctrl+Shift+D или пункт меню).
// Действие menu_profile_debug_info объявлено в mainwindow.ui, но реализации у upstream нет.

void MainWindow::on_menu_profile_debug_info_triggered() {
    MW_show_log(tr("Collecting debug information..."));
    
    runOnNewThread([=, this] {
        QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString tempDir = QDir::temp().filePath("throne_debug_" + timestamp);
        QString zipPath = desktopPath + "/throne_debug_" + timestamp + ".zip";
        
        // Создаем временную директорию
        QDir().mkpath(tempDir);
        
        // 1. Системная информация
        QString systemInfoPath = tempDir + "/01_system_info.txt";
        QProcess systemInfo;
#ifdef Q_OS_WIN
        systemInfo.start("cmd", QStringList() << "/c" << "systeminfo");
#elif defined(Q_OS_MAC)
        systemInfo.start("sh", QStringList() << "-c" << "system_profiler SPSoftwareDataType SPHardwareDataType");
#else
        systemInfo.start("sh", QStringList() << "-c" << "uname -a && cat /proc/cpuinfo && cat /proc/meminfo");
#endif
        systemInfo.waitForFinished(10000);
        QFile systemFile(systemInfoPath);
        if (systemFile.open(QIODevice::WriteOnly)) {
            systemFile.write(systemInfo.readAllStandardOutput());
            systemFile.close();
        }
        
        // 2. Сетевая информация
        QString networkInfoPath = tempDir + "/02_network_info.txt";
        QProcess networkInfo;
#ifdef Q_OS_WIN
        networkInfo.start("cmd", QStringList() << "/c" << "ipconfig /all");
#elif defined(Q_OS_MAC)
        networkInfo.start("sh", QStringList() << "-c" << "ifconfig && networksetup -listallhardwareports");
#else
        networkInfo.start("sh", QStringList() << "-c" << "ip a && ip link");
#endif
        networkInfo.waitForFinished(5000);
        QFile networkFile(networkInfoPath);
        if (networkFile.open(QIODevice::WriteOnly)) {
            networkFile.write(networkInfo.readAllStandardOutput());
            networkFile.close();
        }
        
        // 3. Таблица маршрутизации
        QString routingInfoPath = tempDir + "/03_routing_table.txt";
        QProcess routingInfo;
#ifdef Q_OS_WIN
        routingInfo.start("cmd", QStringList() << "/c" << "route print");
#elif defined(Q_OS_MAC)
        routingInfo.start("netstat", QStringList() << "-rn");
#else
        routingInfo.start("sh", QStringList() << "-c" << "ip route && route -n");
#endif
        routingInfo.waitForFinished(5000);
        QFile routingFile(routingInfoPath);
        if (routingFile.open(QIODevice::WriteOnly)) {
            routingFile.write(routingInfo.readAllStandardOutput());
            routingFile.close();
        }
        
        // 4. Информация о версиях приложения
        QString appInfoPath = tempDir + "/04_app_version.txt";
        QFile appFile(appInfoPath);
        if (appFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&appFile);
            QString version = SubStrBefore(NKR_VERSION, "-");
            if (!version.contains(".")) version = "1.0.0";
            stream << "Throne version: " << version << "\n";
            stream << "Build date: " << __DATE__ << " " << __TIME__ << "\n";
            stream << "Qt version: " << QT_VERSION_STR << "\n";
#ifdef Q_OS_WIN
            stream << "Platform: Windows\n";
#elif defined(Q_OS_MAC)
            stream << "Platform: macOS\n";
#else
            stream << "Platform: Linux\n";
#endif
            
            // Получаем версии ядер
            QProcess singboxVersion;
            singboxVersion.start("sing-box", QStringList() << "version");
            if (singboxVersion.waitForFinished(3000)) {
                QString output = singboxVersion.readAllStandardOutput();
                stream << "\nSing-box version:\n" << output;
            } else {
                stream << "\nSing-box version: Unable to detect\n";
            }
            
            QProcess xrayVersion;
            xrayVersion.start("xray", QStringList() << "version");
            if (xrayVersion.waitForFinished(3000)) {
                QString output = xrayVersion.readAllStandardOutput();
                stream << "\nXray version:\n" << output;
            } else {
                stream << "\nXray version: Unable to detect\n";
            }
            
            appFile.close();
        }
        
        // 5. Информация о пользователе
        QString userInfoPath = tempDir + "/05_user_info.txt";
        QProcess userInfo;
#ifdef Q_OS_WIN
        QString username = qEnvironmentVariable("USERNAME");
        userInfo.start("cmd", QStringList() << "/c" << "net user " + username);
#elif defined(Q_OS_MAC)
        userInfo.start("sh", QStringList() << "-c" << "whoami && id && sw_vers");
#else
        userInfo.start("sh", QStringList() << "-c" << "whoami && id && cat /etc/os-release");
#endif
        userInfo.waitForFinished(5000);
        QFile userFile(userInfoPath);
        if (userFile.open(QIODevice::WriteOnly)) {
            userFile.write(userInfo.readAllStandardOutput());
            userFile.close();
        }
        
        // 5a. Список запущенных процессов
        QString processListPath = tempDir + "/05a_running_processes.txt";
        QProcess processList;
#ifdef Q_OS_WIN
        processList.start("cmd", QStringList() << "/c" << "tasklist /v");
#elif defined(Q_OS_MAC)
        processList.start("sh", QStringList() << "-c" << "ps aux | head -100");
#else
        processList.start("sh", QStringList() << "-c" << "ps aux | head -100");
#endif
        processList.waitForFinished(5000);
        QFile processFile(processListPath);
        if (processFile.open(QIODevice::WriteOnly)) {
            processFile.write(processList.readAllStandardOutput());
            processFile.close();
        }
        
        // 6. Логи ядра (последние 1500 строк)
        QString coreLogsPath = tempDir + "/06_core_logs.txt";
        QString logFilePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/sing-box.log";
        QFile logFile(logFilePath);
        if (logFile.exists() && logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&logFile);
            QStringList allLines;
            while (!in.atEnd()) {
                allLines.append(in.readLine());
            }
            logFile.close();
            
            // Берем последние 1500 строк
            int startLine = qMax(0, allLines.size() - 1500);
            QStringList last1500 = allLines.mid(startLine);
            
            QFile coreLogsFile(coreLogsPath);
            if (coreLogsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&coreLogsFile);
                out << last1500.join("\n");
                coreLogsFile.close();
            }
        }
        
        // 7. Логи приложения (пишутся подсистемой Logging: ротация + логи прошлых сессий)
        const QString logDir = Logging::LogDir();
        if (!logDir.isEmpty()) {
            QDir src(logDir);
            const auto entries = src.entryList(QDir::Files, QDir::Name);
            for (const QString &name : entries) {
                QFile::copy(src.filePath(name), tempDir + "/07_app_" + name);
            }
        }
        const QString prevLog = Logging::PreviousSessionLogPath();
        if (!prevLog.isEmpty() && QFile::exists(prevLog)) {
            QFile::copy(prevLog, tempDir + "/07_app_previous_session.log");
        }
        
        // 8. Информация о настройках (без паролей)
        QString settingsInfoPath = tempDir + "/08_settings_info.txt";
        QFile settingsFile(settingsInfoPath);
        if (settingsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&settingsFile);
            auto* settings = Configs::dataManager->settingsRepo.get();
            
            stream << "=== RUNTIME STATE ===\n";
            stream << "Core running: " << (settings->core_running ? "Yes" : "No") << "\n";
            stream << "Started profile ID: " << settings->started_id << "\n";
            stream << "Current group ID: " << settings->current_group << "\n";
            stream << "Core port: " << settings->core_box_api_port << "\n";
            stream << "VPN mode enabled: " << (settings->spmode_vpn ? "Yes" : "No") << "\n";
            stream << "System proxy enabled: " << (settings->spmode_system_proxy ? "Yes" : "No") << "\n";
            
            stream << "\n=== ROUTING ===\n";
            stream << "Current route ID: " << settings->current_route_id << "\n";
            stream << "Active routing: " << settings->active_routing << "\n";
            stream << "Remote DNS: " << settings->remote_dns << "\n";
            stream << "Remote DNS strategy: " << (settings->remote_dns_disable_ipv6 ? "ipv4_only" : "auto") << "\n";
            stream << "Direct DNS: " << settings->direct_dns << "\n";
            stream << "Direct DNS strategy: " << (settings->direct_dns_disable_ipv6 ? "ipv4_only" : "auto") << "\n";
            stream << "Use DNS object: " << (settings->use_dns_object ? "Yes" : "No") << "\n";
            stream << "DNS final out: " << settings->dns_final_out << "\n";
            stream << "Domain strategy: " << settings->resolve_domain_strategy << "\n";
            stream << "Outbound domain strategy: " << settings->default_domain_strategy << "\n";
            stream << "Adblock enabled: " << (settings->adblock_enable ? "Yes" : "No") << "\n";
            
            stream << "\n=== INBOUND ===\n";
            stream << "Inbound address: " << settings->inbound_address << "\n";
            stream << "Socks port: " << settings->inbound_socks_port << "\n";
            stream << "Random inbound port: " << (settings->random_inbound_port ? "Yes" : "No") << "\n";
            stream << "Proxy scheme: " << settings->proxy_scheme << "\n";
            
            stream << "\n=== VPN/TUN ===\n";
            stream << "VPN implementation: " << settings->vpn_implementation << "\n";
            stream << "VPN strict route: " << (settings->vpn_strict_route ? "Yes" : "No") << "\n";
            stream << "VPN MTU: " << settings->vpn_mtu << "\n";
            stream << "VPN IPv6: " << (settings->vpn_ipv6 ? "Yes" : "No") << "\n";
            stream << "Fake DNS: " << (settings->fake_dns ? "Yes" : "No") << "\n";
            stream << "Enable TUN routing: " << (settings->enable_tun_routing ? "Yes" : "No") << "\n";
            stream << "Disable privilege request: " << (settings->disable_privilege_req ? "Yes" : "No") << "\n";
            
            stream << "\n=== CORE ===\n";
            stream << "Log level: " << settings->log_level << "\n";
            stream << "Max log lines: " << settings->max_log_line << "\n";
            stream << "Mux protocol: " << settings->mux_protocol << "\n";
            stream << "Mux padding: " << (settings->mux_padding ? "Yes" : "No") << "\n";
            stream << "Mux concurrency: " << settings->mux_concurrency << "\n";
            stream << "Mux default on: " << (settings->mux_default_on ? "Yes" : "No") << "\n";
            stream << "Clash API port: " << settings->core_box_clash_api << "\n";
            stream << "Clash API address: " << settings->core_box_clash_listen_addr << "\n";
            
            stream << "\n=== XRAY ===\n";
            stream << "Xray log level: " << settings->xray_log_level << "\n";
            stream << "Xray mux concurrency: " << settings->xray_mux_concurrency << "\n";
            stream << "Xray mux default on: " << (settings->xray_mux_default_on ? "Yes" : "No") << "\n";
            stream << "Xray VLESS preference: " << settings->xray_vless_preference << "\n";
            
            stream << "\n=== TESTING ===\n";
            stream << "Test latency URL: " << settings->test_latency_url << "\n";
            stream << "URL test timeout: " << settings->url_test_timeout_ms << " ms\n";
            stream << "Test concurrent: " << settings->test_concurrent << "\n";
            stream << "Speed test mode: " << settings->speed_test_mode << "\n";
            stream << "Speed test timeout: " << settings->speed_test_timeout_ms << " ms\n";
            stream << "Simple download URL: " << settings->simple_dl_url << "\n";
            
            stream << "\n=== NTP ===\n";
            stream << "NTP enabled: " << (settings->enable_ntp ? "Yes" : "No") << "\n";
            stream << "NTP server address: " << settings->ntp_server_address << "\n";
            stream << "NTP server port: " << settings->ntp_server_port << "\n";
            stream << "NTP interval: " << settings->ntp_interval << "\n";
            
            stream << "\n=== DNS HIJACK ===\n";
            stream << "DNS server enabled: " << (settings->enable_dns_server ? "Yes" : "No") << "\n";
            stream << "DNS server listen LAN: " << (settings->dns_server_listen_lan ? "Yes" : "No") << "\n";
            stream << "DNS server listen port: " << settings->dns_server_listen_port << "\n";
            stream << "DNS v4 response: " << settings->dns_v4_resp << "\n";
            stream << "DNS v6 response: " << settings->dns_v6_resp << "\n";
            stream << "Enable redirect: " << (settings->enable_redirect ? "Yes" : "No") << "\n";
            
            stream << "\n=== SUBSCRIPTION ===\n";
            stream << "User agent: " << settings->GetUserAgent() << "\n";
            stream << "Auto update interval: " << settings->sub_auto_update << " min\n";
            stream << "Clear on update: " << (settings->sub_clear ? "Yes" : "No") << "\n";
            stream << "Send HWID: " << (settings->sub_send_hwid ? "Yes" : "No") << "\n";
            
            stream << "\n=== SECURITY ===\n";
            stream << "Skip cert verification: " << (settings->skip_cert ? "Yes" : "No") << "\n";
            stream << "uTLS fingerprint: " << settings->utlsFingerprint << "\n";
            stream << "Use Mozilla certs: " << (settings->use_mozilla_certs ? "Yes" : "No") << "\n";
            stream << "Network use proxy: " << (settings->net_use_proxy ? "Yes" : "No") << "\n";
            stream << "Network insecure: " << (settings->net_insecure ? "Yes" : "No") << "\n";
            
            stream << "\n=== UI ===\n";
            stream << "Theme: " << settings->theme << "\n";
            stream << "Language: " << settings->language << "\n";
            stream << "Font: " << settings->font << "\n";
            stream << "Font size: " << settings->font_size << "\n";
            stream << "Start minimal: " << (settings->start_minimal ? "Yes" : "No") << "\n";
            stream << "Disable tray: " << (settings->disable_tray ? "Yes" : "No") << "\n";
            stream << "Use custom icons: " << (settings->use_custom_icons ? "Yes" : "No") << "\n";
            stream << "Enable stats: " << (settings->enable_stats ? "Yes" : "No") << "\n";
            stream << "Disable traffic stats: " << (settings->disable_traffic_stats ? "Yes" : "No") << "\n";
            
            stream << "\n=== HOTKEYS ===\n";
            stream << "Main window: " << settings->hotkey_mainwindow << "\n";
            stream << "Group: " << settings->hotkey_group << "\n";
            stream << "Route: " << settings->hotkey_route << "\n";
            stream << "System proxy menu: " << settings->hotkey_system_proxy_menu << "\n";
            stream << "Toggle system proxy: " << settings->hotkey_toggle_system_proxy << "\n";
            
            stream << "\n=== EXTRA ===\n";
            stream << "Extra core paths: " << settings->extraCorePaths.join(", ") << "\n";
            stream << "Remember enabled: " << (settings->remember_enable ? "Yes" : "No") << "\n";
            stream << "Remembered profile ID: " << settings->remember_id << "\n";
            stream << "Allow beta updates: " << (settings->allow_beta_update ? "Yes" : "No") << "\n";
            
            settingsFile.close();
        }
        
        // 9. Информация о группах и профилях
        QString groupsInfoPath = tempDir + "/09_groups_profiles.txt";
        QFile groupsFile(groupsInfoPath);
        if (groupsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&groupsFile);
            
            auto groupIds = Configs::dataManager->groupsRepo->GetAllGroupIds();
            stream << "Total groups: " << groupIds.size() << "\n\n";
            
            for (int groupId : groupIds) {
                auto group = Configs::dataManager->groupsRepo->GetGroup(groupId);
                if (!group) continue;
                
                stream << "=== Group ID: " << group->id << " ===\n";
                stream << "Name: " << group->name << "\n";
                stream << "Archive: " << (group->archive ? "Yes" : "No") << "\n";
                stream << "Profiles count: " << group->Profiles().size() << "\n";
                
                if (!group->url.isEmpty()) {
                    stream << "Subscription URL: [PRESENT]\n";
                    stream << "Last update timestamp: " << group->sub_last_update << "\n";
                    stream << "Skip auto update: " << (group->skip_auto_update ? "Yes" : "No") << "\n";
                    stream << "Auto clear unavailable: " << (group->auto_clear_unavailable ? "Yes" : "No") << "\n";
                    // Политика содержит только настройки, учётных данных в ней нет.
                    stream << "Provider policy: "
                           << (group->provider_policy_json.isEmpty() ? QString("[none]")
                                                                     : group->provider_policy_json)
                           << "\n";
                }
                
                if (group->front_proxy_id >= 0) {
                    stream << "Front proxy ID: " << group->front_proxy_id << "\n";
                }
                if (group->landing_proxy_id >= 0) {
                    stream << "Landing proxy ID: " << group->landing_proxy_id << "\n";
                }
                
                stream << "Test sort by: " << static_cast<int>(group->test_sort_by) << "\n";
                stream << "Test items to show: " << static_cast<int>(group->test_items_to_show) << "\n";
                
                // Статистика профилей в группе
                int activeProfiles = 0;
                int availableProfiles = 0;
                QMap<QString, int> typeCount;
                
                for (int profileId : group->Profiles()) {
                    auto profile = Configs::dataManager->profilesRepo->GetProfile(profileId);
                    if (profile) {
                        typeCount[profile->type]++;
                        if (profile->latency >= 0) availableProfiles++;
                        if (profile->id == Configs::dataManager->settingsRepo->started_id) activeProfiles++;
                    }
                }
                
                stream << "Available profiles: " << availableProfiles << "\n";
                stream << "Active profile: " << (activeProfiles > 0 ? "Yes" : "No") << "\n";
                stream << "Profile types:\n";
                for (auto it = typeCount.constBegin(); it != typeCount.constEnd(); ++it) {
                    stream << "  - " << it.key() << ": " << it.value() << "\n";
                }
                stream << "\n";
            }
            
            groupsFile.close();
        }
        
        // 10. Логи из UI (текущая сессия)
        QString uiLogsPath = tempDir + "/10_ui_session_logs.txt";
        runOnUiThread([=, this] {
            QFile uiLogsFile(uiLogsPath);
            if (uiLogsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QString logContent = qvLogDocument->toPlainText();
                QTextStream stream(&uiLogsFile);
                stream << "=== UI Session Logs (from current session) ===\n\n";
                stream << logContent;
                uiLogsFile.close();
            }
        });
        
        // Даем время UI потоку записать файл
        QThread::msleep(200);
        
        // Создаем ZIP архив
        QProcess zipProcess;
#ifdef Q_OS_WIN
        // Используем PowerShell для создания ZIP на Windows
        QString psCommand = QString("Compress-Archive -Path '%1\\*' -DestinationPath '%2' -Force")
                                .arg(QDir::toNativeSeparators(tempDir))
                                .arg(QDir::toNativeSeparators(zipPath));
        zipProcess.start("powershell", QStringList() << "-Command" << psCommand);
        zipProcess.waitForFinished(30000);
#else
        // На macOS и Linux используем zip
        zipProcess.setWorkingDirectory(tempDir);
        zipProcess.start("zip", QStringList() << "-r" << zipPath << ".");
        zipProcess.waitForFinished(30000);
#endif
        
        // Удаляем временную директорию
        QDir(tempDir).removeRecursively();
        
        runOnUiThread([=, this] {
            if (QFile::exists(zipPath)) {
                MW_show_log(tr("Debug information saved to: %1").arg(zipPath));
                QMessageBox::information(this, tr("Debug Info"), 
                                       tr("Debug information collected successfully!\n\nSaved to:\n%1").arg(zipPath));
            } else {
                MW_show_log(tr("Failed to create debug archive"));
                QMessageBox::warning(this, tr("Error"), tr("Failed to create debug information archive"));
            }
        });
    });
}

