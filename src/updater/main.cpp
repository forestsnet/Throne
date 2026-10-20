// Обновлятор Throne: отдельная программа, которая ставит обновление после
// выхода основного приложения.
//
// На Windows раньше использовался сторонний Odin. Он не дожидается выхода
// приложения и на попытке заменить ещё запущенный Throne.exe отвечает
// Permission_Denied — обновление на Windows просто не работало. Здесь выход
// приложения ожидается явно, поэтому свой обновлятор собирается и под Windows.
// Linux по-прежнему на Odin.

#include <QCoreApplication>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QThread>
#include <QDebug>
#include <QDateTime>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <csignal>
#include <cerrno>
#endif

#define THRONE_UPDATER_VERSION "1.0.0"

class ThroneUpdater {
public:
    // Каталог обновления передаёт приложение первым аргументом: оно одно знает,
    // куда клало архив (на Windows это зависит от flag_use_appdata). Значение по
    // умолчанию оставлено для запуска вручную и для старых вызовов без аргумента.
    static QString GetUpdateDir() {
        const auto args = QCoreApplication::arguments();
        if (args.size() > 1 && !args[1].trimmed().isEmpty()) return args[1];
#ifdef Q_OS_WIN
        const QString beside = QCoreApplication::applicationDirPath() + "/Throne_update";
        if (QFile::exists(beside + "/Throne.zip")) return beside;
#endif
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/Throne_update";
    }

    // Папку программы передаёт приложение вторым аргументом. Своя собственная
    // годится не всегда: копия обновлятора может лежать и не рядом с программой.
    // Путь до .app: приложение живёт в Contents/MacOS внутри бандла, а
    // обновляем мы бандл целиком. Раньше здесь поднимались на один шаг вверх и
    // получали Contents — старый бандл сносился по этому пути, а новый ложился
    // внутрь него как Contents/Contents. Установка после такого «обновления»
    // переставала существовать.
    static QString FindAppBundle(const QString& startPath) {
        QString path = startPath;
        while (!path.endsWith(".app") && path.contains('/')) {
            path = path.left(path.lastIndexOf('/'));
        }
        return path.endsWith(".app") ? path : QString();
    }

    // PID приложения приходит третьим аргументом. Ждать по имени процесса
    // нельзя: у людей рядом стоит несколько копий Throne (папки вида
    // "Throne 1.2.6", "Throne 1.1.2"), и запущенная соседняя копия держит имя
    // занятым вечно — обновлятор в такой ситуации всегда сдавался по таймауту.
    static qint64 GetMainPid() {
        const auto args = QCoreApplication::arguments();
        if (args.size() > 3) return args[3].toLongLong();
        return 0;
    }

    static bool IsPidRunning(qint64 pid) {
#ifdef Q_OS_WIN
        HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        if (handle == nullptr) return false;
        const bool alive = WaitForSingleObject(handle, 0) == WAIT_TIMEOUT;
        CloseHandle(handle);
        return alive;
#else
        return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
    }

    static QString GetAppDir() {
        const auto args = QCoreApplication::arguments();
        if (args.size() > 2 && !args[2].trimmed().isEmpty()) return args[2];
        return QCoreApplication::applicationDirPath();
    }

    static void Log(const QString& message) {
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        QString logMessage = QString("[%1] %2").arg(timestamp, message);
        qDebug() << logMessage;
        
        // Записываем лог в файл
        QString logPath = GetUpdateDir() + "/updater.log";
        QFile logFile(logPath);
        if (logFile.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&logFile);
            out << logMessage << "\n";
            logFile.close();
        }
    }

    static bool IsProcessRunning(const QString& processName) {
        QProcess process;
#ifdef Q_OS_WIN
        // tasklist всегда завершается нулём, поэтому смотрим на вывод, а не на код.
        const QString image = processName + ".exe";
        process.start("tasklist", QStringList() << "/FI" << ("IMAGENAME eq " + image) << "/NH");
        process.waitForFinished(10000);
        return QString::fromLocal8Bit(process.readAllStandardOutput()).contains(image, Qt::CaseInsensitive);
#else
        process.start("pgrep", QStringList() << "-x" << processName);
        process.waitForFinished();
        return process.exitCode() == 0;
#endif
    }

    static bool WaitForMainAppExit(int timeoutSeconds = 120) {
        const qint64 pid = GetMainPid();
        Log(pid > 0 ? QString("Waiting for main application (pid %1) to exit...").arg(pid)
                    : QStringLiteral("Waiting for main application to exit..."));

        const QString processName = QStringLiteral("Throne");
        auto stillRunning = [&] {
            return pid > 0 ? IsPidRunning(pid) : IsProcessRunning(processName);
        };
        int elapsed = 0;

        while (stillRunning() && elapsed < timeoutSeconds) {
            QThread::msleep(500);
            elapsed++;
            if (elapsed % 4 == 0) {
                // Шаг цикла — полсекунды, поэтому и предел делим пополам:
                // раньше в логе стояло "14s/30s", хотя ждали ровно 15 секунд.
                Log(QString("Still waiting... (%1s/%2s)").arg(elapsed / 2).arg(timeoutSeconds / 2));
            }
        }

        if (stillRunning()) {
            Log("ERROR: Main application did not exit in time!");
            return false;
        }

        Log("Main application has exited.");
        QThread::msleep(1000); // Дополнительная задержка для освобождения файлов
        return true;
    }

    static bool ExtractUpdate(const QString& zipPath, const QString& destDir) {
        Log(QString("Extracting update from: %1").arg(zipPath));
        Log(QString("Destination: %1").arg(destDir));

        QDir().mkpath(destDir);

        QProcess process;
#ifdef Q_OS_WIN
        // unzip в Windows нет. Expand-Archive есть в PowerShell начиная с
        // Windows 8.1 и работает без установки чего-либо ещё.
        process.start("powershell", QStringList()
            << "-NoProfile" << "-NonInteractive" << "-Command"
            << QString("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
                   .arg(QDir::toNativeSeparators(zipPath), QDir::toNativeSeparators(destDir)));
#else
        process.start("unzip", QStringList() << "-o" << zipPath << "-d" << destDir);
#endif
        process.waitForFinished(120000);

        if (process.exitCode() != 0) {
            Log(QString("ERROR: Failed to extract: %1").arg(QString::fromLocal8Bit(process.readAllStandardError())));
            return false;
        }

        Log("Extraction completed successfully.");
        return true;
    }

    static bool CopyDirectoryRecursively(const QString& srcDir, const QString& dstDir) {
        QDir src(srcDir);
        if (!src.exists()) return false;

        QDir dst(dstDir);
        if (!dst.exists()) {
            dst.mkpath(".");
        }

        for (const QString& entry : src.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString srcPath = src.filePath(entry);
            QString dstPath = dst.filePath(entry);

            if (QFileInfo(srcPath).isDir()) {
                if (!CopyDirectoryRecursively(srcPath, dstPath)) {
                    return false;
                }
            } else {
                QFile::remove(dstPath);
                if (!QFile::copy(srcPath, dstPath)) {
                    Log(QString("ERROR: Failed to copy: %1 to %2").arg(srcPath, dstPath));
                    return false;
                }
            }
        }
        return true;
    }

    // Каталог внутри распакованного архива, где реально лежат файлы программы.
    // Архив может быть как с папкой Throne внутри, так и без неё.
    static QString FindPayloadDir(const QString& extractDir, const QString& marker) {
        if (QFile::exists(extractDir + "/" + marker)) return extractDir;
        const QString nested = extractDir + "/Throne";
        if (QFile::exists(nested + "/" + marker)) return nested;
        return {};
    }

    static bool InstallUpdate(const QString& updateDir, const QString& appDir) {
        Log("Installing update...");
        Log(QString("From: %1").arg(updateDir));
        Log(QString("To: %1").arg(appDir));

#ifdef Q_OS_WIN
        // На Windows программа — это каталог с файлами, а не бандл. Копируем
        // поверх, ничего не удаляя: рядом лежит config с настройками и базой.
        const QString payload = FindPayloadDir(updateDir, "Throne.exe");
        if (payload.isEmpty()) {
            Log("ERROR: Throne.exe not found in the extracted update!");
            return false;
        }
        Log(QString("Payload directory: %1").arg(payload));

        if (!CopyDirectoryRecursively(payload, appDir)) {
            Log("ERROR: Failed to copy new files!");
            return false;
        }

        Log("Installation completed successfully.");
        return true;
#else
        // На macOS обновляем весь .app bundle
        const QString appBundle = FindAppBundle(appDir);
        if (appBundle.isEmpty()) {
            Log(QString("ERROR: could not find the .app bundle above %1").arg(appDir));
            return false;
        }
        Log(QString("App bundle: %1").arg(appBundle));
        
        // Ищем обновленный .app в распакованной папке
        QString actualUpdateDir = updateDir;
        QDir updateRoot(actualUpdateDir);
        QStringList appBundles = updateRoot.entryList(QStringList() << "*.app", QDir::Dirs);
        
        if (appBundles.isEmpty()) {
            // Возможно файлы в подпапке Throne
            QDir throneSubdir(actualUpdateDir + "/Throne");
            if (throneSubdir.exists()) {
                appBundles = throneSubdir.entryList(QStringList() << "*.app", QDir::Dirs);
                if (!appBundles.isEmpty()) {
                    actualUpdateDir = actualUpdateDir + "/Throne";
                    updateRoot = QDir(actualUpdateDir);
                }
            }
        }
        
        if (appBundles.isEmpty()) {
            Log("ERROR: No .app bundle found in update!");
            return false;
        }
        
        QString newAppBundle = actualUpdateDir + "/" + appBundles.first();
        Log(QString("Found new app bundle: %1").arg(newAppBundle));
        
        // Сначала собираем новый бандл рядом и только потом меняем местами:
        // если копирование оборвётся на середине, у человека останется рабочая
        // программа, а не половина от неё.
        const QString stagedBundle = appBundle + ".new";
        const QString retiredBundle = appBundle + ".old";
        QDir(stagedBundle).removeRecursively();
        QDir(retiredBundle).removeRecursively();

        if (!CopyDirectoryRecursively(newAppBundle, stagedBundle)) {
            Log("ERROR: Failed to copy new app bundle!");
            QDir(stagedBundle).removeRecursively();
            return false;
        }

        if (!QDir().rename(appBundle, retiredBundle)) {
            Log("ERROR: Failed to move the old app bundle aside!");
            QDir(stagedBundle).removeRecursively();
            return false;
        }
        if (!QDir().rename(stagedBundle, appBundle)) {
            Log("ERROR: Failed to put the new app bundle in place, restoring the old one!");
            QDir().rename(retiredBundle, appBundle);
            QDir(stagedBundle).removeRecursively();
            return false;
        }
        QDir(retiredBundle).removeRecursively();
        
        // Устанавливаем права на выполнение для macOS
        QString execPath = appBundle + "/Contents/MacOS/Throne";
        QProcess::execute("chmod", QStringList() << "+x" << execPath);
        
        QString updaterPath = appBundle + "/Contents/MacOS/updater";
        QProcess::execute("chmod", QStringList() << "+x" << updaterPath);
        
        Log("Installation completed successfully.");
        return true;
#endif
    }

    static bool StartApplication(const QString& appPath) {
        Log(QString("Starting application: %1").arg(appPath));

#ifdef Q_OS_WIN
        if (!QProcess::startDetached(appPath, QStringList{})) {
            Log("ERROR: Failed to start application!");
            return false;
        }
        Log("Application started successfully.");
        return true;
#else
        
        // На macOS используем 'open' для запуска .app bundle
        const QString appBundle = FindAppBundle(appPath);
        if (appBundle.isEmpty()) {
            Log(QString("ERROR: could not find the .app bundle above %1").arg(appPath));
            return false;
        }

        if (!QProcess::startDetached("open", QStringList() << appBundle)) {
            Log("ERROR: Failed to start application!");
            return false;
        }

        Log("Application started successfully.");
        return true;
#endif
    }

    static void Cleanup(const QString& updateDir) {
        Log("Cleaning up update files...");
        
        QFile::remove(updateDir + "/Throne.zip");
        QDir(updateDir + "/extracted").removeRecursively();
        QDir(updateDir + "/Throne").removeRecursively();
        QDir(updateDir + "/Throne_update").removeRecursively();
        
        Log("Cleanup completed.");
    }

    static int Run() {
        Log("========================================");
        Log("Throne Updater started");
        Log(QString("Version: %1").arg(THRONE_UPDATER_VERSION));
        Log("========================================");

        QString updateDir = GetUpdateDir();
        QString appDir = GetAppDir();
        QString zipPath = updateDir + "/Throne.zip";
        
        // Также проверяем в текущей папке (как в Odin updater)
        if (!QFile::exists(zipPath)) {
            zipPath = "./Throne.zip";
            if (!QFile::exists(zipPath)) {
                Log("ERROR: Update archive not found!");
                return 1;
            }
        }
        
        QString extractDir = updateDir + "/extracted";

        Log(QString("Update directory: %1").arg(updateDir));
        Log(QString("Application directory: %1").arg(appDir));
        Log(QString("Archive path: %1").arg(zipPath));

        // Ждем завершения главного приложения
        if (!WaitForMainAppExit(30)) {
            Log("ERROR: Cannot proceed - main application is still running!");
            return 2;
        }

        // Очищаем старые файлы
        QDir(extractDir).removeRecursively();

        // Распаковываем обновление
        if (!ExtractUpdate(zipPath, extractDir)) {
            Log("ERROR: Failed to extract update!");
            return 3;
        }

        // Устанавливаем обновление
        if (!InstallUpdate(extractDir, appDir)) {
            Log("ERROR: Failed to install update!");
            return 4;
        }

        // Запускаем обновленное приложение
#ifdef Q_OS_WIN
        const QString appPath = appDir + "/Throne.exe";
#else
        const QString appPath = appDir + "/Throne";
#endif

        if (!StartApplication(appPath)) {
            Log("ERROR: Failed to start updated application!");
            return 5;
        }

        // Очистка
        Cleanup(updateDir);

        Log("========================================");
        Log("Update completed successfully!");
        Log("========================================");

        return 0;
    }
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    // Запускаем updater и выходим
    int result = ThroneUpdater::Run();
    
    // Небольшая задержка перед выходом чтобы лог записался
    QThread::msleep(500);
    
    return result;
}
