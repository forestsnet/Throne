#include "include/ui/mainwindow.h"

#include <QCheckBox>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QString>
#include <QStringList>

#include "include/database/SettingsRepo.h"
#include "include/global/Configs.hpp"
#include "include/ui/mainWindow/MainWindowInternal.h"

// Детект программ, мешающих работе TUN-режима (VPN-клиенты, антивирусы с фильтрацией трафика).
// Список форка: проверяется перед стартом профиля в режиме VPN.
QStringList MainWindow::CheckConflictingProcesses() {
    QStringList conflictingProcesses;
    
    // Белый список системных процессов (не проверять их)
    QStringList systemWhitelist = {
        "cfprefsd",  // macOS Core Foundation Preferences Daemon
        "systemd",   // Linux init system
        "launchd",   // macOS init system
        "kernel_task", // macOS kernel
        "com.apple", // Apple system services
        "svchost",   // Windows Service Host
        "lsass",     // Windows Local Security Authority Subsystem Service
        "wininit",   // Windows Initialization Process
        "csrss",     // Windows Client/Server Runtime Subsystem
        "services",  // Windows Services
        "lsm",       // Windows Local Session Manager
        "windowspackagemanagerserver", // Windows Package Manager Server
    };
    
    // Список конфликтующих процессов (имена в нижнем регистре)
    QStringList conflictingNames = {
        // VPN клиенты
        "radmin", "rserver", "r_server", "rvpn",
        "hamachi", "logmein",
        "nordvpn",
        "expressvpn",
        "protonvpn",
        "windscribe",
        "surfshark",
        "cyberghost",
        "privatevpn",
        "tunnelbear",
        "hotspotshield", "hsscp", "hsssrv",
        "vyprvpn",
        "ipvanish",
        "purevpn",
        "hidemyass", "hmavpn",
        "zenmate",
        "betternet",
        "hola",
        "operavpn",
        // Антивирусы с встроенным VPN/Firewall
        "eset", "ekrn", "egui", "eguiproxy",
        "avast",
        "avg",
        "kaspersky", "avp", "kavfs",
        "bitdefender", "bdagent",
        "mcafee", "mcuicnt", "mcshield",
        "norton",
        "comodo firewall", "comodo", // только полное имя Comodo
        "drweb", "drwebupw", "spidergate", "spiderml", "drwebcom",
        "avira", "avscan", "avguard",
        "trendmicro", "pcclient", "tmproxy",
        "panda", "psksvc", "pavfnsvr",
        "f-secure", "fsma", "fsgk",
        "sophos", "sophoshealth", "sav32cli",
        "malwarebytes", "mbam",
        "gdata", "avgnt",
        "webroot", "wrsa",
        "vipre",
        "360safe", "360tray", // 360 Total Security
        // Другие VPN решения
        "openvpn",
        "wireguard",
        "softether", "vpnserver", "vpnbridge", "vpnclient",
        "zerotier",
        "tailscale",
        "pritunl",
        "viscosity",
        "tunnelblick",
        // Virtual network adapters managers
        "vmware", "vmnat", "vmnetdhcp",
        "virtualbox", "vboxheadless", "vboxsvc",
        // DPI обход и proxy утилиты
        "goodbyedpi", "goodbye-dpi",
        "zapret", "winws", "nfqws", "tpws", "mdig", "ip2net",
        "dpitunnel", "dpi-tunnel",
        "greentunnel", "green-tunnel",
        "powertunnel",
        "spoofdpi", "spoofing",
        "byedpi", "ciadpi",
        "geph", // китайский обход блокировок
        "psiphon",
        "lantern",
        "v2ray", "v2rayn", "v2rayng",
        "xray", "xray-core",
        "clash", "clashmeta", "clash-verge",
        "sing-box",
        "shadowsocks", "ss-local", "ss-server",
        "kcptun",
        "gost",
        "brook",
        "trojan", "trojan-go",
        "naiveproxy",
        "hysteria",
    };

#ifdef Q_OS_WIN
    // Windows: используем tasklist
    QProcess process;
    process.start("tasklist", QStringList() << "/FO" << "CSV" << "/NH");
    
    if (process.waitForFinished(3000)) {
        QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
        QStringList lines = output.split('\n', Qt::SkipEmptyParts);
        
        MW_show_log(QString("[CheckConflict] Scanning %1 processes...").arg(lines.size()));
        
        for (const QString& line : lines) {
            // Формат: "processname.exe","PID","Session Name","Session#","Mem Usage"
            QString processName = line.split(',').first().replace("\"", "").toLower();
            // Убираем .exe из имени
            if (processName.endsWith(".exe")) {
                processName = processName.left(processName.length() - 4);
            }
            
            // Проверяем белый список
            bool isWhitelisted = false;
            for (const QString& whitelisted : systemWhitelist) {
                if (processName.contains(whitelisted.toLower())) {
                    isWhitelisted = true;
                    break;
                }
            }
            if (isWhitelisted) continue;
            
            for (const QString& conflicting : conflictingNames) {
                if (processName.contains(conflicting)) {
                    QString displayName = line.split(',').first().replace("\"", "");
                    if (!conflictingProcesses.contains(displayName)) {
                        conflictingProcesses.append(displayName);
                        MW_show_log(QString("[CheckConflict] Found: %1").arg(displayName));
                    }
                    break;
                }
            }
        }
    } else {
        MW_show_log("[CheckConflict] Failed to run tasklist");
    }
#elif defined(Q_OS_MAC)
    // macOS: используем ps
    QProcess process;
    process.start("ps", QStringList() << "ax" << "-o" << "comm=");
    
    if (process.waitForFinished(3000)) {
        QString output = QString::fromUtf8(process.readAllStandardOutput());
        QStringList lines = output.split('\n', Qt::SkipEmptyParts);
        
        MW_show_log(QString("[CheckConflict] Scanning %1 processes...").arg(lines.size()));
        
        for (const QString& line : lines) {
            QString processName = line.trimmed().toLower();
            // Убираем путь, оставляем только имя процесса
            if (processName.contains('/')) {
                processName = processName.split('/').last();
            }
            
            // Проверяем белый список
            bool isWhitelisted = false;
            for (const QString& whitelisted : systemWhitelist) {
                if (processName.contains(whitelisted.toLower())) {
                    isWhitelisted = true;
                    break;
                }
            }
            if (isWhitelisted) continue;
            
            for (const QString& conflicting : conflictingNames) {
                if (processName.contains(conflicting)) {
                    QString displayName = line.trimmed();
                    if (displayName.contains('/')) {
                        displayName = displayName.split('/').last();
                    }
                    if (!conflictingProcesses.contains(displayName)) {
                        conflictingProcesses.append(displayName);
                        MW_show_log(QString("[CheckConflict] Found: %1").arg(displayName));
                    }
                    break;
                }
            }
        }
    } else {
        MW_show_log("[CheckConflict] Failed to run ps");
    }
#elif defined(Q_OS_LINUX)
    // Linux: используем ps
    QProcess process;
    process.start("ps", QStringList() << "ax" << "-o" << "comm=");
    
    if (process.waitForFinished(3000)) {
        QString output = QString::fromUtf8(process.readAllStandardOutput());
        QStringList lines = output.split('\n', Qt::SkipEmptyParts);
        
        MW_show_log(QString("[CheckConflict] Scanning %1 processes...").arg(lines.size()));
        
        for (const QString& line : lines) {
            QString processName = line.trimmed().toLower();
            
            // Проверяем белый список
            bool isWhitelisted = false;
            for (const QString& whitelisted : systemWhitelist) {
                if (processName.contains(whitelisted.toLower())) {
                    isWhitelisted = true;
                    break;
                }
            }
            if (isWhitelisted) continue;
            
            for (const QString& conflicting : conflictingNames) {
                if (processName.contains(conflicting)) {
                    QString displayName = line.trimmed();
                    if (!conflictingProcesses.contains(displayName)) {
                        conflictingProcesses.append(displayName);
                        MW_show_log(QString("[CheckConflict] Found: %1").arg(displayName));
                    }
                    break;
                }
            }
        }
    } else {
        MW_show_log("[CheckConflict] Failed to run ps");
    }
#endif
    
    if (conflictingProcesses.isEmpty()) {
        MW_show_log("[CheckConflict] No conflicting processes found");
    } else {
        MW_show_log(QString("[CheckConflict] Total found: %1").arg(conflictingProcesses.size()));
    }
    
    return conflictingProcesses;
}

// Спрашивать при каждом запуске нельзя: диалог модальный и держит profile_start,
// а перезапуск бывает автоматическим — после обновления подписки или падения
// ядра. Тогда туннель лежит до тех пор, пока кто-нибудь не нажмёт кнопку.
// Поэтому предупреждаем один раз за сеанс, а флажок в диалоге глушит его совсем.
bool MainWindow::ConfirmConflictingProcesses() {
    static bool askedThisSession = false;

    const QStringList conflicting = CheckConflictingProcesses();
    if (conflicting.isEmpty()) return true;

    if (askedThisSession || Configs::dataManager->settingsRepo->conflict_warning_disabled) {
        MW_show_log("[CheckConflict] Warning suppressed, starting anyway");
        return true;
    }
    askedThisSession = true;

    QString message = tr("Обнаружены программы, которые могут помешать работе TUN режима:\n\n");
    message += "• " + conflicting.join("\n• ");
    message += tr("\n\nЭти программы могут конфликтовать с виртуальным сетевым адаптером TUN.\n");
    message += tr("Рекомендуется закрыть эти программы перед запуском профиля в VPN режиме.\n\n");
    message += tr("Продолжить запуск?");

    QMessageBox msgBox(GetMessageBoxParent());
    msgBox.setWindowTitle(tr("Предупреждение о конфликтах"));
    msgBox.setText(message);
    msgBox.setIcon(QMessageBox::Warning);

    auto *never = new QCheckBox(tr("Больше не показывать"), &msgBox);
    msgBox.setCheckBox(never);

    QPushButton *continueBtn = msgBox.addButton(tr("Продолжить"), QMessageBox::AcceptRole);
    QPushButton *cancelBtn = msgBox.addButton(tr("Отмена"), QMessageBox::RejectRole);
    msgBox.setDefaultButton(cancelBtn);

    msgBox.exec();

    if (never->isChecked()) {
        Configs::dataManager->settingsRepo->conflict_warning_disabled = true;
        Configs::dataManager->settingsRepo->Save();
    }

    if (msgBox.clickedButton() != continueBtn) {
        MW_show_log("[CheckConflict] User cancelled profile start");
        return false;
    }
    MW_show_log("[CheckConflict] User chose to continue despite conflicts");
    return true;
}
