#include <csignal>

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QTranslator>
#include <QMessageBox>
#include <QStandardPaths>
#include <QLocalSocket>
#include <QLocalServer>
#include <QThread>
#include <QSettings>
#include <QTimer>
#include <QUrlQuery>
#include <QFileOpenEvent>
#include <QDateTime>
#include <QTextStream>
#include <QFileInfo>
#include <3rdparty/WinCommander.hpp>

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/global/Configs.hpp"

#include "include/ui/mainwindow_interface.h"

#ifdef Q_OS_WIN
#include "include/sys/windows/MiniDump.h"
#include "include/sys/windows/eventHandler.h"
#include "include/sys/windows/WinVersion.h"
#include <qfontdatabase.h>
#endif
#ifdef Q_OS_LINUX
#include <include/sys/linux/coreDump.h>
#include <qfontdatabase.h>
#endif

void signal_handler(int signum) {
    if (GetMainWindow()) {
        GetMainWindow()->prepare_exit();
        qApp->quit();
    }
}

QTranslator* trans = nullptr;
QTranslator* trans_qt = nullptr;

void loadTranslate(const QString& locale) {
    QT_TRANSLATE_NOOP("QPlatformTheme", "Cancel");
    QT_TRANSLATE_NOOP("QPlatformTheme", "Apply");
    QT_TRANSLATE_NOOP("QPlatformTheme", "Yes");
    QT_TRANSLATE_NOOP("QPlatformTheme", "No");
    QT_TRANSLATE_NOOP("QPlatformTheme", "OK");
    if (trans != nullptr) {
        trans->deleteLater();
    }
    if (trans_qt != nullptr) {
        trans_qt->deleteLater();
    }
    //
    trans = new QTranslator;
    trans_qt = new QTranslator;
    QLocale::setDefault(QLocale(locale));
    //
    if (trans->load(":/translations/" + locale + ".qm")) {
        QCoreApplication::installTranslator(trans);
    }
}

#define LOCAL_SERVER_PREFIX "throne-"

void registerUrlScheme()
{
#ifdef Q_OS_WIN
    const QString appPath = QDir::toNativeSeparators(QApplication::applicationFilePath());
    const QString protocolName = "throne";
    const QString description = "URL:Throne Protocol";

    // Основная ветка реестра
    QSettings protocolKey(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(protocolName),
                          QSettings::NativeFormat);
    protocolKey.setValue(".", description);  // "." вместо "Default" — чтобы точно попало в (Default)
    protocolKey.setValue("URL Protocol", "");

    // Команда для открытия
    const QString correctValue = QString("\"%1\" \"%2\"").arg(appPath, "%1");
    QSettings commandKey(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1\\shell\\open\\command")
                             .arg(protocolName),
                         QSettings::NativeFormat);

    QString currentValue = commandKey.value(".").toString();

    if (currentValue != correctValue)
    {
        commandKey.setValue(".", correctValue);
        qDebug() << "Fixed registry command for throne://" << correctValue;
    }
    else
    {
        qDebug() << "Registry command already correct:" << correctValue;
    }

#elif defined(Q_OS_MACOS)
    qDebug() << "URL scheme for macOS is handled via Info.plist";

#elif defined(Q_OS_LINUX)
    const QString desktopEntry =
        QString("[Desktop Entry]\n"
                "Type=Application\n"
                "Name=Throne\n"
                "Exec=%1 %%u\n"
                "MimeType=x-scheme-handler/throne;\n"
                "NoDisplay=true\n")
            .arg(QApplication::applicationFilePath());

    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    const QString desktopFilePath = configDir + "/applications/throne.desktop";
    QDir().mkpath(QFileInfo(desktopFilePath).absolutePath());

    QFile desktopFile(desktopFilePath);
    if (desktopFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        desktopFile.write(desktopEntry.toUtf8());
        desktopFile.close();
        QProcess::execute("update-desktop-database", QStringList() << QFileInfo(desktopFilePath).absolutePath());
        qDebug() << "Created desktop entry:" << desktopFilePath;
    } else {
        qWarning() << "Failed to write desktop entry:" << desktopFilePath;
    }
#endif
}


class ThroneApplication : public QApplication {
public:
    ThroneApplication(int &argc, char **argv) : QApplication(argc, argv) {}

protected:
    bool event(QEvent *event) override {
        if (event->type() == QEvent::FileOpen) {
            QFileOpenEvent *openEvent = static_cast<QFileOpenEvent *>(event);
            QString url = openEvent->url().toString();
            
            if (url.startsWith("throne://")) {
                // Обрабатываем URL с небольшой задержкой, чтобы UI успел инициализироваться
                qDebug() << "[THRONE_URL] FileOpenEvent received URL:" << url;
                QTimer::singleShot(100, this, [this, url]() {
                    qDebug() << "[THRONE_URL] Timer fired, calling handleThroneUrl with:" << url;
                    handleThroneUrl(url);
                });
                return true;
            }
        }
        return QApplication::event(event);
    }

private:
    void handleThroneUrl(QString url) {
        qDebug() << "[THRONE_URL] handleThroneUrl called with:" << url;
        
        // Очистка URL от пробелов и кавычек
        url = url.trimmed();
        qDebug() << "[THRONE_URL] After trimming:" << url;
        
        if (url.startsWith('\"') && url.endsWith('\"')) {
            url = url.mid(1, url.length() - 2);
            qDebug() << "[THRONE_URL] After removing quotes:" << url;
        }
        
        // Извлекаем реальный URL из throne:// схемы
        QString actualUrl = url;
        if (url.startsWith("throne://subscribe?")) {
            qDebug() << "[THRONE_URL] Parsing throne://subscribe URL";
            QUrl throneUrl(url);
            QUrlQuery query(throneUrl);
            actualUrl = query.queryItemValue("url", QUrl::FullyDecoded);
            qDebug() << "[THRONE_URL] Extracted actual URL:" << actualUrl;
            
            if (actualUrl.isEmpty()) {
                qDebug() << "[THRONE_URL] ERROR: Actual URL is empty after extraction!";
                return;
            }
        }
        
        qDebug() << "[THRONE_URL] Getting main window...";
        auto mainWindow = GetMainWindow();
        if (mainWindow) {
            qDebug() << "[THRONE_URL] Main window found, showing and raising";
            mainWindow->show();
            mainWindow->raise();
            mainWindow->activateWindow();
            
            // Даем еще немного времени для UI
            QTimer::singleShot(200, [actualUrl]() {
                qDebug() << "[THRONE_URL] Calling AsyncUpdate with:" << actualUrl;
                Subscription::groupUpdater->AsyncUpdate(actualUrl, -1, nullptr);
                qDebug() << "[THRONE_URL] AsyncUpdate called";
            });
        } else {
            qDebug() << "[THRONE_URL] WARNING: Main window is null, saving URL to pendingThroneUrl:" << url;
            pendingThroneUrl = url;
        }
    }

public:
    QString pendingThroneUrl;
};

// Глобальный файл для логирования
static QFile* logFile = nullptr;

// Обработчик сообщений для записи в файл
void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString formattedMessage;
    QTextStream stream(&formattedMessage);
    
    switch (type) {
    case QtDebugMsg:
        stream << "[DEBUG] ";
        break;
    case QtInfoMsg:
        stream << "[INFO] ";
        break;
    case QtWarningMsg:
        stream << "[WARNING] ";
        break;
    case QtCriticalMsg:
        stream << "[CRITICAL] ";
        break;
    case QtFatalMsg:
        stream << "[FATAL] ";
        break;
    }
    
    stream << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz") << " " << msg;
    
    // Вывод в консоль (если доступна)
    fprintf(stderr, "%s\n", formattedMessage.toLocal8Bit().constData());
    fflush(stderr);
    
    // Вывод в файл
    if (logFile && logFile->isOpen()) {
        QTextStream fileStream(logFile);
        fileStream << formattedMessage << "\n";
        fileStream.flush();
        logFile->flush();
    }
    
    if (type == QtFatalMsg)
        abort();
}

int main(int argc, char* argv[]) {
    // Core dump
#ifdef Q_OS_WIN
    Windows_SetCrashHandler();
#endif
#ifdef Q_OS_LINUX
    enable_core_dumps();
#endif

    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setQuitOnLastWindowClosed(false);
    // QApplication a(argc, argv);
    ThroneApplication a(argc, argv);  // Используем ThroneApplication вместо QApplication
    
    // Настройка логирования в файл
    QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/throne_debug.log";
    QDir().mkpath(QFileInfo(logPath).absolutePath());
    logFile = new QFile(logPath);
    if (logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qInstallMessageHandler(messageHandler);
        qDebug() << "========================================";
        qDebug() << "Throne started at" << QDateTime::currentDateTime().toString();
        qDebug() << "Log file:" << logPath;
        qDebug() << "Arguments:" << QApplication::arguments();
        qDebug() << "========================================";
    } else {
        qWarning() << "Failed to open log file:" << logPath;
    }

#if !defined(Q_OS_MACOS) && (QT_VERSION >= QT_VERSION_CHECK(6,9,0))
    // Load the emoji fonts
#ifdef Q_OS_WIN
    int fontId = QFontDatabase::addApplicationFont(WinVersion::IsBuildNumGreaterOrEqual(BuildNumber::Windows_11_22H2) ? ":/font/notoEmoji" : ":/font/Twemoji");
#else
    int fontId = QFontDatabase::addApplicationFont(":/font/notoEmoji");
#endif
    if (fontId >= 0)
    {
        QStringList fontFamilies = QFontDatabase::applicationFontFamilies(fontId);
        QFontDatabase::setApplicationEmojiFontFamilies(fontFamilies);
    } else
    {
        qDebug() << "could not load emoji font!";
    }
#endif

    // Clean
    QDir::setCurrent(QApplication::applicationDirPath());
    if (QFile::exists("updater.old")) {
        QFile::remove("updater.old");
    }

    QStringList arguments = QApplication::arguments();

    // dirs & clean
    auto wd = QDir(QApplication::applicationDirPath());
    if (arguments.contains("-appdata")) {
        QString appDataDir;
        int appdataIndex = arguments.indexOf("-appdata");
        if (arguments.size() > appdataIndex + 1 && !arguments.at(appdataIndex + 1).startsWith("-")) {
            appDataDir = arguments.at(appdataIndex + 1);
        }
        QApplication::setApplicationName("Throne");
        if (!appDataDir.isEmpty()) {
            wd.setPath(appDataDir);
        } else {
            wd.setPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        }
    }
    if (!wd.exists()) wd.mkpath(wd.absolutePath());
    if (!wd.exists("config")) wd.mkdir("config");
    QDir::setCurrent(wd.absoluteFilePath("config"));
    QDir("temp").removeRecursively();

    // Load database
    Configs::initDB(QString(QDir::currentPath() + QDir::separator() + "throne.db").toStdString());

    // Store Flags
    Configs::dataManager->settingsRepo->argv = arguments;
    if (Configs::dataManager->settingsRepo->argv.contains("-many")) Configs::dataManager->settingsRepo->flag_many = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-appdata")) {
        Configs::dataManager->settingsRepo->flag_use_appdata = true;
        int appdataIndex = Configs::dataManager->settingsRepo->argv.indexOf("-appdata");
        if (Configs::dataManager->settingsRepo->argv.size() > appdataIndex + 1 && !Configs::dataManager->settingsRepo->argv.at(appdataIndex + 1).startsWith("-")) {
            Configs::dataManager->settingsRepo->appdataDir = Configs::dataManager->settingsRepo->argv.at(appdataIndex + 1);
        }
    }
    if (Configs::dataManager->settingsRepo->argv.contains("-tray")) Configs::dataManager->settingsRepo->flag_tray = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-debug")) Configs::dataManager->settingsRepo->flag_debug = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-flag_restart_tun_on")) Configs::dataManager->settingsRepo->flag_restart_tun_on = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-flag_restart_dns_set")) Configs::dataManager->settingsRepo->flag_dns_set = true;
#ifdef NKR_CPP_USE_APPDATA
    Configs::dataManager->settingsRepo->flag_use_appdata = true; // Example: Package & MacOS
#endif
#ifdef NKR_CPP_DEBUG
    Configs::dataManager->settingsRepo->flag_debug = true;
#endif

#ifdef Q_OS_LINUX
    QApplication::addLibraryPath(QApplication::applicationDirPath() + "/usr/plugins");
#endif

    // dispatchers
    DS_cores = new QThread;
    DS_cores->start();

// icons
    QIcon::setFallbackSearchPaths(QStringList{
        ":/icon",
    });

    // icon for no theme
    if (QIcon::themeName().isEmpty()) {
        QIcon::setThemeName("breeze");
    }

#ifdef Q_OS_WIN
    // На Windows при первом запуске автоматически запрашиваем права администратора
    bool isFirstRun = !Configs::dataManager->settingsRepo->windows_set_admin && !Configs::IsAdmin() && !Configs::dataManager->settingsRepo->disable_run_admin;
    
    if (Configs::dataManager->settingsRepo->windows_set_admin && !Configs::IsAdmin() && !Configs::dataManager->settingsRepo->disable_run_admin)
    {
        Configs::dataManager->settingsRepo->windows_set_admin = false; // so that if permission denied, we will run as user on the next run
        Configs::dataManager->settingsRepo->Save();
        WinCommander::runProcessElevated(QApplication::applicationFilePath(), {}, "", WinCommander::SW_NORMAL, false);
        QApplication::quit();
        return 0;
    }
    
    // Первый запуск - сразу запускаем с правами администратора
    if (isFirstRun) {
        Configs::dataManager->settingsRepo->windows_set_admin = true;
        Configs::dataManager->settingsRepo->Save();
        WinCommander::runProcessElevated(QApplication::applicationFilePath(), {}, "", WinCommander::SW_NORMAL, false);
        QApplication::quit();
        return 0;
    }
#endif

    // dataManager->settingsRepo & Flags
    if (Configs::dataManager->settingsRepo->start_minimal) Configs::dataManager->settingsRepo->flag_tray = true;

    // Translate
    QString locale;
    switch (Configs::dataManager->settingsRepo->language) {
        case 1: // English
            break;
        case 2:
            locale = "zh_CN";
            break;
        case 3:
            locale = "fa_IR"; // farsi(iran)
            break;
        case 4:
            locale = "ru_RU"; // Russian
            break;
        default:
            locale = QLocale().name();
    }
    QGuiApplication::tr("QT_LAYOUT_DIRECTION");
    loadTranslate(locale);

    // Check if another instance is running
    QByteArray hashBytes = QCryptographicHash::hash(wd.absolutePath().toUtf8(), QCryptographicHash::Md5).toBase64(QByteArray::OmitTrailingEquals);
    hashBytes.replace('+', '0').replace('/', '1');
    auto serverName = LOCAL_SERVER_PREFIX + QString::fromUtf8(hashBytes);
    qDebug() << "server name: " << serverName;

    // Проверяем throne:// URL перед проверкой другого экземпляра
    qDebug() << "[THRONE_URL] Total arguments:" << arguments.size();
    qDebug() << "[THRONE_URL] All arguments:" << arguments;
    
    QString throneUrl;
    for (int i = 1; i < arguments.size(); ++i) {
        QString arg = arguments[i].trimmed();
        qDebug() << "[THRONE_URL] Processing argument" << i << ":" << arg;
        
        // Убираем кавычки если есть (Windows может передавать в кавычках)
        if (arg.startsWith('"') && arg.endsWith('"')) {
            arg = arg.mid(1, arg.length() - 2);
            qDebug() << "[THRONE_URL] After removing quotes:" << arg;
        }
        
        if (arg.startsWith("throne://")) {
            throneUrl = arg;
            qDebug() << "[THRONE_URL] ✓ Found throne URL in arguments:" << throneUrl;
            break;
        }
    }

    if (!throneUrl.isEmpty()) {
        qDebug() << "[THRONE_URL] Processing throne URL:" << throneUrl;
        
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(250))
        {
            qDebug() << "[THRONE_URL] Another instance is running, sending URL and quitting";
            // Передаем URL в запущенный экземпляр
            QByteArray data = throneUrl.toUtf8();
            qDebug() << "[THRONE_URL] Sending" << data.size() << "bytes:" << data;
            socket.write(data);
            socket.waitForBytesWritten(1000);
            socket.disconnectFromServer();
            qDebug() << "[THRONE_URL] URL sent successfully, exiting";
            return 0;
        }
        qDebug() << "[THRONE_URL] No other instance running, will process URL in this instance";
    } else {
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(250))
        {
            qDebug() << "Another instance is running, quitting";
            return 0;
        }
    }

    // QLocalServer
    qDebug() << "[THRONE_URL] Creating QLocalServer with name:" << serverName;
    QLocalServer server(qApp);
    server.setSocketOptions(QLocalServer::WorldAccessOption);
    if (!server.listen(serverName)) {
        qWarning() << "[THRONE_URL] ERROR: Failed to start QLocalServer! Error:" << server.errorString();
        return 1;
    }
    qDebug() << "[THRONE_URL] QLocalServer started successfully";
    
    QObject::connect(&server, &QLocalServer::newConnection, qApp, [&] {
        qDebug() << "[THRONE_URL] ===== NEW CONNECTION RECEIVED =====";
        auto s = server.nextPendingConnection();
        qDebug() << "[THRONE_URL] Socket state:" << s->state();
        
        // Читаем данные из соединения (если новый экземпляр передает URL)
        qDebug() << "[THRONE_URL] Waiting for data...";
        if (s->waitForReadyRead(1000)) {
            QByteArray data = s->readAll();
            qDebug() << "[THRONE_URL] ✓ Data received successfully";
            qDebug() << "[THRONE_URL] Received" << data.size() << "bytes from socket:" << data;
            
            QString receivedUrl = QString::fromUtf8(data).trimmed();
            // Убираем кавычки если есть
            if (receivedUrl.startsWith('"') && receivedUrl.endsWith('"')) {
                receivedUrl = receivedUrl.mid(1, receivedUrl.length() - 2);
                qDebug() << "[THRONE_URL] After removing quotes:" << receivedUrl;
            }
            
            if (receivedUrl.startsWith("throne://")) {
                qDebug() << "[THRONE_URL] ✓ Received throne URL from another instance:" << receivedUrl;
                
                // Извлекаем реальный URL
                QString actualUrl = receivedUrl;
                if (receivedUrl.startsWith("throne://subscribe?")) {
                    QUrl throneUrl(receivedUrl);
                    QUrlQuery query(throneUrl);
                    actualUrl = query.queryItemValue("url", QUrl::FullyDecoded);
                    qDebug() << "[THRONE_URL] Extracted actual subscription URL:" << actualUrl;
                }
                
                if (actualUrl.isEmpty()) {
                    qDebug() << "[THRONE_URL] ERROR: Actual URL is empty!";
                    s->close();
                    MW_dialog_message("", "Raise");
                    return;
                }
                
                // Открываем главное окно и обрабатываем URL
                QTimer::singleShot(100, [actualUrl]() {
                    qDebug() << "[THRONE_URL] Processing subscription URL:" << actualUrl;
                    
                    // Показываем окно
                    MW_dialog_message("", "Raise");
                    
                    auto mainWindow = GetMainWindow();
                    if (mainWindow) {
                        mainWindow->show();
                        mainWindow->raise();
                        mainWindow->activateWindow();
                        qDebug() << "[THRONE_URL] Main window activated";
                    } else {
                        qDebug() << "[THRONE_URL] ERROR: Main window is null!";
                    }

                    qDebug() << "[THRONE_URL] Calling AsyncUpdate with URL:" << actualUrl;
                    Subscription::groupUpdater->AsyncUpdate(actualUrl, -1, nullptr);
                    qDebug() << "[THRONE_URL] AsyncUpdate called successfully";
                });
            } else {
                qDebug() << "[THRONE_URL] Received data is not a throne:// URL";
            }
        } else {
            qDebug() << "[THRONE_URL] No data received from socket or timeout";
        }
        
        qDebug() << "[THRONE_URL] Closing socket connection";
        s->close();
        // raise main window в любом случае
        qDebug() << "[THRONE_URL] Raising main window";
        MW_dialog_message("", "Raise");
    });

    QObject::connect(qApp, &QApplication::aboutToQuit, [&]
    {
        server.close();
        QLocalServer::removeServer(serverName);
    });

#ifdef Q_OS_LINUX
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
#endif

#ifdef Q_OS_WIN
    auto eventFilter = new PowerOffTaskkillFilter(signal_handler);
    a.installNativeEventFilter(eventFilter);
#endif

#ifdef Q_OS_MACOS
    QObject::connect(qApp, &QGuiApplication::commitDataRequest, [&](QSessionManager &manager)
    {
        Q_UNUSED(manager);
        signal_handler(0);
    });
#endif

    registerUrlScheme();

    // Проверяем, что схема реально записана корректно
    #ifdef Q_OS_WIN
    {
        QSettings verify("HKEY_CURRENT_USER\\SOFTWARE\\Classes\\throne\\shell\\open\\command", QSettings::NativeFormat);
        QString cmd = verify.value("Default").toString();
        QString expected = QString("\"%1\" \"%%1\"").arg(QDir::toNativeSeparators(QApplication::applicationFilePath()));
        if (cmd != expected) {
            registerUrlScheme();
        }
    }
    #endif

    UI_InitMainWindow();

    // После инициализации UI - обработка отложенных URL для всех платформ
    if (!a.pendingThroneUrl.isEmpty()) {
        qDebug() << "[THRONE_URL] Found pendingThroneUrl, setting up timer";
        QTimer::singleShot(500, [&a]() {
            QString url = a.pendingThroneUrl;
            qDebug() << "[THRONE_URL] Processing pending throne URL after UI init:" << url;
            
            // Извлекаем реальный URL
            QString actualUrl = url;
            if (url.startsWith("throne://subscribe?")) {
                qDebug() << "[THRONE_URL] Parsing throne://subscribe from pending URL";
                QUrl throneUrl(url);
                QUrlQuery query(throneUrl);
                actualUrl = query.queryItemValue("url", QUrl::FullyDecoded);
                qDebug() << "[THRONE_URL] Extracted actual URL:" << actualUrl;
            }

            qDebug() << "[THRONE_URL] Getting main window for pending URL...";
            auto mainWindow = GetMainWindow();
            if (mainWindow) {
                qDebug() << "[THRONE_URL] Main window found for pending URL, showing";
                mainWindow->show();
                mainWindow->raise();
                mainWindow->activateWindow();
                
                // Дополнительная задержка для стабильности
                QTimer::singleShot(300, [actualUrl]() {
                    qDebug() << "[THRONE_URL] Actually calling AsyncUpdate for pending URL:" << actualUrl;
                    Subscription::groupUpdater->AsyncUpdate(actualUrl, -1, nullptr);
                    qDebug() << "[THRONE_URL] AsyncUpdate for pending URL completed";
                });
            } else {
                qDebug() << "[THRONE_URL] ERROR: Main window still null after init!";
            }
        });
    } else {
        qDebug() << "[THRONE_URL] No pendingThroneUrl found";
    }
    
    // Обработка URL при первом запуске (если приложение не было запущено)
    if (!throneUrl.isEmpty()) {
        qDebug() << "[THRONE_URL] Found throneUrl from arguments, setting up timer";
        qDebug() << "[THRONE_URL] throneUrl value:" << throneUrl;
        
        // Извлекаем реальный URL
        QString actualUrl = throneUrl;
        if (throneUrl.startsWith("throne://subscribe?")) {
            qDebug() << "[THRONE_URL] Parsing throne://subscribe from throneUrl";
            QUrl url(throneUrl);
            QUrlQuery query(url);
            actualUrl = query.queryItemValue("url", QUrl::FullyDecoded);
            qDebug() << "[THRONE_URL] Extracted actual URL:" << actualUrl;
        }
        
        // Обработать URL-схему после инициализации UI
        QTimer::singleShot(1000, [actualUrl]() {
            qDebug() << "[THRONE_URL] Timer triggered (1000ms), processing URL:" << actualUrl;
            
            // Показываем главное окно
            qDebug() << "[THRONE_URL] Getting main window...";
            auto mainWindow = GetMainWindow();
            if (mainWindow) {
                qDebug() << "[THRONE_URL] Main window found, showing";
                mainWindow->show();
                mainWindow->raise();
                mainWindow->activateWindow();
                qDebug() << "[THRONE_URL] Main window shown and activated";
                
                // Дополнительная задержка перед обработкой
                QTimer::singleShot(300, [actualUrl]() {
                    qDebug() << "[THRONE_URL] Actually calling AsyncUpdate for command line URL:" << actualUrl;
                    Subscription::groupUpdater->AsyncUpdate(actualUrl, -1, nullptr);
                    qDebug() << "[THRONE_URL] AsyncUpdate for command line URL completed";
                });
            } else {
                qDebug() << "[THRONE_URL] Main window was null, using MW_dialog_message";
                MW_dialog_message("", "Raise");
                qDebug() << "[THRONE_URL] MW_dialog_message called";
            }
        });
    }

    int result = QApplication::exec();
    
    // Закрываем файл логов
    if (logFile) {
        qDebug() << "========================================";
        qDebug() << "Throne exiting with code:" << result;
        qDebug() << "========================================";
        logFile->close();
        delete logFile;
        logFile = nullptr;
    }
    
    return result;
}