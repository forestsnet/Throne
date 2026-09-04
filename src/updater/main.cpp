// Throne Updater for macOS - Standalone application for installing updates
// This runs after the main application exits to replace files
// Note: Windows and Linux use the official Odin updater from https://github.com/throneproj/updater

#include <QCoreApplication>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QThread>
#include <QDebug>
#include <QDateTime>
#include <QStandardPaths>

#define THRONE_UPDATER_VERSION "1.0.0"

class ThroneUpdater {
public:
    static QString GetUpdateDir() {
        // macOS: ~/Library/Application Support/Throne/Throne_update
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/Throne_update";
    }

    static QString GetAppDir() {
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
        process.start("pgrep", QStringList() << "-x" << processName);
        process.waitForFinished();
        return process.exitCode() == 0;
    }

    static bool WaitForMainAppExit(int timeoutSeconds = 30) {
        Log("Waiting for main application to exit...");
        
        QString processName = "Throne";
        int elapsed = 0;
        
        while (IsProcessRunning(processName) && elapsed < timeoutSeconds) {
            QThread::msleep(500);
            elapsed++;
            if (elapsed % 4 == 0) {
                Log(QString("Still waiting... (%1s/%2s)").arg(elapsed/2).arg(timeoutSeconds));
            }
        }

        if (IsProcessRunning(processName)) {
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

        // Используем unzip на macOS
        QProcess process;
        process.start("unzip", QStringList() << "-o" << zipPath << "-d" << destDir);
        process.waitForFinished(60000); // 60 секунд таймаут
        
        if (process.exitCode() != 0) {
            Log(QString("ERROR: Failed to extract: %1").arg(QString::fromUtf8(process.readAllStandardError())));
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

    static bool InstallUpdate(const QString& updateDir, const QString& appDir) {
        Log("Installing update...");
        Log(QString("From: %1").arg(updateDir));
        Log(QString("To: %1").arg(appDir));

        // На macOS обновляем весь .app bundle
        QDir dir(appDir);
        dir.cdUp(); // Переходим к Throne.app
        QString appBundle = dir.absolutePath();
        
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
        
        // Удаляем старый bundle
        QDir oldBundle(appBundle);
        if (!oldBundle.removeRecursively()) {
            Log("ERROR: Failed to remove old app bundle!");
            return false;
        }
        
        // Копируем новый bundle
        if (!CopyDirectoryRecursively(newAppBundle, appBundle)) {
            Log("ERROR: Failed to copy new app bundle!");
            return false;
        }
        
        // Устанавливаем права на выполнение для macOS
        QString execPath = appBundle + "/Contents/MacOS/Throne";
        QProcess::execute("chmod", QStringList() << "+x" << execPath);
        
        QString updaterPath = appBundle + "/Contents/MacOS/updater";
        QProcess::execute("chmod", QStringList() << "+x" << updaterPath);
        
        Log("Installation completed successfully.");
        return true;
    }

    static bool StartApplication(const QString& appPath) {
        Log(QString("Starting application: %1").arg(appPath));
        
        // На macOS используем 'open' для запуска .app bundle
        QDir dir(appPath);
        dir.cdUp();
        QString appBundle = dir.absolutePath();
        
        if (!QProcess::startDetached("open", QStringList() << appBundle)) {
            Log("ERROR: Failed to start application!");
            return false;
        }

        Log("Application started successfully.");
        return true;
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
        Log("Throne Updater for macOS Started");
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
        QString appPath = appDir + "/Throne";

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
