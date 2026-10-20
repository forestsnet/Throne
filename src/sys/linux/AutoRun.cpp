#include "include/sys/AutoRun.hpp"

#include <QApplication>
#include <QDir>
#include "include/global/Configs.hpp"
#include <QSettings>


#include <QStandardPaths>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTextStream>

#define NEWLINE "\n"

//  launchatlogin.cpp
//  ShadowClash
//
//  Created by TheWanderingCoel on 2018/6/12.
//  Copyright © 2019 Coel Wu. All rights reserved.
//
QString getUserAutostartDir_private() {
    QString config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    config += QLatin1String("/autostart/");
    return config;
}

bool AutoRun_SetEnabled(bool enable, QString *error) {
    // From https://github.com/nextcloud/desktop/blob/master/src/common/utility_unix.cpp
    QString appName = QCoreApplication::applicationName();
    QString userAutoStartPath = getUserAutostartDir_private();
    QString desktopFileLocation = userAutoStartPath + appName + QLatin1String(".desktop");
    QStringList appCmdList;

#ifdef NKR_DESKTOP_EXEC
    appCmdList << NKR_DESKTOP_EXEC;
#else
    if (QProcessEnvironment::systemEnvironment().contains("APPIMAGE")) {
        appCmdList << QProcessEnvironment::systemEnvironment().value("APPIMAGE");
    } else {
        appCmdList << QApplication::applicationFilePath();
    }
#endif

    appCmdList << "-tray";

    if (Configs::dataManager->settingsRepo->flag_use_appdata) {
        appCmdList << "-appdata";
    }

    if (enable) {
        if (!QDir().exists(userAutoStartPath) && !QDir().mkpath(userAutoStartPath)) {
            if (error) *error = QString("Could not create %1").arg(userAutoStartPath);
            return false;
        }

        QFile iniFile(desktopFileLocation);

        if (!iniFile.open(QIODevice::WriteOnly)) {
            if (error) *error = iniFile.errorString();
            return false;
        }

        QTextStream ts(&iniFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        ts.setCodec("UTF-8");
#endif
        ts << QLatin1String("[Desktop Entry]") << NEWLINE
           << QLatin1String("Name=") << appName << NEWLINE
           << QLatin1String("Exec=") << appCmdList.join(" ") << NEWLINE
           << QLatin1String("Terminal=") << "false" << NEWLINE
           << QLatin1String("Categories=") << "Network" << NEWLINE
           << QLatin1String("Type=") << "Application" << NEWLINE
           << QLatin1String("StartupNotify=") << "false" << NEWLINE
           << QLatin1String("X-GNOME-Autostart-enabled=") << "true" << NEWLINE;
        ts.flush();
        iniFile.close();
    } else {
        QFile desktopFile(desktopFileLocation);
        if (desktopFile.exists() && !desktopFile.remove()) {
            if (error) *error = desktopFile.errorString();
            return false;
        }
    }
    return true;
}

bool AutoRun_IsEnabled() {
    QString appName = QCoreApplication::applicationName();
    QString desktopFileLocation = getUserAutostartDir_private() + appName + QLatin1String(".desktop");
    return QFile::exists(desktopFileLocation);
}

void AutoRun_FixTaskIfNeeded() {
    // Файл автозапуска от удалённой или перемещённой копии остаётся и при
    // каждом входе пытается запустить то, чего нет.
    const QString path = getUserAutostartDir_private() + QCoreApplication::applicationName() + QLatin1String(".desktop");
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) return;
    const auto content = QString::fromUtf8(file.readAll());
    file.close();

    for (const auto &line : content.split('\n')) {
        if (!line.startsWith(QLatin1String("Exec="))) continue;
        // Exec=<путь> [-tray ...]; путь может быть в кавычках.
        auto exec = line.mid(5).trimmed();
        if (exec.startsWith('"')) exec = exec.mid(1).section('"', 0, 0);
        else exec = exec.section(' ', 0, 0);
        if (exec.isEmpty() || QFileInfo::exists(exec)) return;
        QFile::remove(path);
        return;
    }
}

void AutoRun_MigrateIfNeeded() {}