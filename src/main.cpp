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
#include <3rdparty/WinCommander.hpp>

#include "include/global/Configs.hpp"
#include "include/configs/sub/GroupUpdater.hpp"

#include "include/ui/mainwindow_interface.h"

#ifdef Q_OS_WIN
#include "include/sys/windows/MiniDump.h"
#include "include/sys/windows/eventHandler.h"
#include "include/sys/windows/WinVersion.h"
#include <qfontdatabase.h>
#endif
#ifdef Q_OS_LINUX
#include "include/sys/linux/desktopinfo.h"
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

#ifdef Q_OS_WIN
static void DebugBox(const QString &title, const QString &text)
{
    QMessageBox box;
    box.setWindowTitle(title);
    box.setText(text);
    box.setIcon(QMessageBox::Information);
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}
#else
#define DebugBox(title, text) qDebug() << title << ":" << text
#endif

int main(int argc, char* argv[]) {
    qDebug() << "=== APPLICATION STARTED ===";
    qDebug() << "argc:" << argc;
    for (int i = 0; i < argc; ++i) {
        qDebug() << "argv[" << i << "]:" << argv[i];
    }

    // Core dump
#ifdef Q_OS_WIN
    Windows_SetCrashHandler();
#endif

    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication a(argc, argv);

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

    // Flags
    Configs::dataStore->argv = QApplication::arguments();
    if (Configs::dataStore->argv.contains("-many")) Configs::dataStore->flag_many = true;
    if (Configs::dataStore->argv.contains("-appdata")) {
        Configs::dataStore->flag_use_appdata = true;
        int appdataIndex = Configs::dataStore->argv.indexOf("-appdata");
        if (Configs::dataStore->argv.size() > appdataIndex + 1 && !Configs::dataStore->argv.at(appdataIndex + 1).startsWith("-")) {
            Configs::dataStore->appdataDir = Configs::dataStore->argv.at(appdataIndex + 1);
        }
    }
    if (Configs::dataStore->argv.contains("-tray")) Configs::dataStore->flag_tray = true;
    if (Configs::dataStore->argv.contains("-debug")) Configs::dataStore->flag_debug = true;
    if (Configs::dataStore->argv.contains("-flag_restart_tun_on")) Configs::dataStore->flag_restart_tun_on = true;
    if (Configs::dataStore->argv.contains("-flag_restart_dns_set")) Configs::dataStore->flag_dns_set = true;
#ifdef NKR_CPP_USE_APPDATA
    Configs::dataStore->flag_use_appdata = true; // Example: Package & MacOS
#endif
#ifdef NKR_CPP_DEBUG
    Configs::dataStore->flag_debug = true;
#endif

    // dirs & clean
    auto wd = QDir(QApplication::applicationDirPath());
    if (Configs::dataStore->flag_use_appdata) {
        QApplication::setApplicationName("Throne");
        if (!Configs::dataStore->appdataDir.isEmpty()) {
            wd.setPath(Configs::dataStore->appdataDir);
        } else {
            wd.setPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        }
    }
    if (!wd.exists()) wd.mkpath(wd.absolutePath());
    if (!wd.exists("config")) wd.mkdir("config");
    QDir::setCurrent(wd.absoluteFilePath("config"));
    QDir("temp").removeRecursively();

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

    // Dir
    QDir dir;
    bool dir_success = true;
    if (!dir.exists("profiles")) {
        dir_success &= dir.mkdir("profiles");
    }
    if (!dir.exists("groups")) {
        dir_success &= dir.mkdir("groups");
    }
    if (!dir.exists(ROUTES_PREFIX_NAME)) {
        dir_success &= dir.mkdir(ROUTES_PREFIX_NAME);
    }
    if (!dir_success) {
        QMessageBox::critical(nullptr, "Error", "No permission to write " + dir.absolutePath());
        return 1;
    }

    // migrate the old config file
    if (QFile::exists("groups/nekobox.json"))
    {
        QFile::rename("groups/nekobox.json", "configs.json");
    }

    // Load dataStore
    Configs::dataStore->fn = "configs.json";
    auto isLoaded = Configs::dataStore->Load();
    if (!isLoaded) {
        Configs::dataStore->Save();
    }

#ifdef Q_OS_WIN
    if (Configs::dataStore->windows_set_admin && !Configs::IsAdmin() && !Configs::dataStore->disable_run_admin)
    {
        Configs::dataStore->windows_set_admin = false; // so that if permission denied, we will run as user on the next run
        Configs::dataStore->Save();
        WinCommander::runProcessElevated(QApplication::applicationFilePath(), {}, "", WinCommander::SW_NORMAL, false);
        QApplication::quit();
        return 0;
    }
#endif

    // Datastore & Flags
    if (Configs::dataStore->start_minimal) Configs::dataStore->flag_tray = true;

    // load routing and shortcuts
    Configs::dataStore->routing = std::make_unique<Configs::Routing>();
    Configs::dataStore->routing->fn = ROUTES_PREFIX + "Default";
    isLoaded = Configs::dataStore->routing->Load();
    if (!isLoaded) {
        Configs::dataStore->routing->Save();
    }

    Configs::dataStore->shortcuts = std::make_unique<Configs::Shortcuts>();
    Configs::dataStore->shortcuts->fn = "shortcuts.json";
    isLoaded = Configs::dataStore->shortcuts->Load();
    if (!isLoaded) {
        Configs::dataStore->shortcuts->Save();
    }

    // Translate
    QString locale;
    switch (Configs::dataStore->language) {
        case 1: // English
            break;
        // case 2:
        //     locale = "zh_CN";
        //     break;
        // case 3:
        //     locale = "fa_IR"; // farsi(iran)
        //     break;
        case 2:
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
    
    // Проверяем throne:// URL перед проверкой другого экземпляра
    QStringList arguments = a.arguments();
    
    QString throneUrl;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString &arg = arguments[i];
        if (arg.startsWith("throne://")) {
            throneUrl = arg;
            break;
        }
    }
    
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(250))
    {
        qDebug() << "Another instance is running";
        if (!throneUrl.isEmpty()) {
            // Передаем URL в запущенный экземпляр
            socket.write(throneUrl.toUtf8());
            socket.waitForBytesWritten(1000);
        }
        socket.disconnectFromServer();
        return 0;
    }

    // QLocalServer - обновляем обработчик для получения URL
    QLocalServer server(qApp);
    server.setSocketOptions(QLocalServer::WorldAccessOption);
    if (!server.listen(serverName)) {
        qWarning() << "Failed to start QLocalServer! Error:" << server.errorString();
        return 1;
    }
    QObject::connect(&server, &QLocalServer::newConnection, qApp, [&] {
        auto s = server.nextPendingConnection();
        
        // Читаем данные из соединения (если новый экземпляр передает URL)
        if (s->waitForReadyRead(1000)) {
            QByteArray data = s->readAll();
            QString receivedUrl = QString::fromUtf8(data);
            
            if (receivedUrl.startsWith("throne://")) {
                // Открываем главное окно и обрабатываем URL
                QTimer::singleShot(100, [receivedUrl]() {
                    //
                    // Показываем окно
                    MW_dialog_message("", "Raise");
                    
                    auto mainWindow = GetMainWindow();
                    if (mainWindow) {
                        mainWindow->show();
                        mainWindow->raise();
                        mainWindow->activateWindow();
                        qDebug() << "Main window activated";
                    } else {
                        qDebug() << "Main window is null!";
                    }

                    Subscription::groupUpdater->AsyncUpdate(receivedUrl, -1, nullptr);
                });
            }
        }
        
        s->close();
        // raise main window в любом случае
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

    
    // #ifdef Q_OS_WIN
    // DebugBox("Debug - Before UI Init", "About to call UI_InitMainWindow");
    // #endif
    
    UI_InitMainWindow();
    
    // #ifdef Q_OS_WIN
    // DebugBox("Debug - After UI Init", "UI_InitMainWindow completed");
    // #endif
    
    // Обработка URL при первом запуске (если приложение не было запущено)
    if (!throneUrl.isEmpty()) {
        #ifdef Q_OS_WIN
        // QString debugMsg = QString("Found throne URL: %1").arg(throneUrl);
        // DebugBox("Debug - URL Found", debugMsg);
        // #endif
        
        // Обработать URL-схему после инициализации UI
        QTimer::singleShot(1000, [throneUrl]() {
            // #ifdef Q_OS_WIN
            // DebugBox("Debug - Timer", "Timer triggered, processing URL...");
            // #endif
            
            // Показываем главное окно
            auto mainWindow = GetMainWindow();
            if (mainWindow) {
                mainWindow->show();
                mainWindow->raise();
                mainWindow->activateWindow();
                // #ifdef Q_OS_WIN
                // DebugBox("Debug - Window", "Main window shown");
                // #endif
            } else {
                MW_dialog_message("", "Raise");
                // #ifdef Q_OS_WIN
                // DebugBox("Debug - Window Null", "Main window was null, used MW_dialog_message");
                // #endif
            }
            
            // #ifdef Q_OS_WIN
            // QString processMsg = QString("About to call AsyncUpdate with: %1").arg(throneUrl);
            // DebugBox("Debug - Before AsyncUpdate", processMsg);
            // #endif
            
            // Обрабатываем URL
            Subscription::groupUpdater->AsyncUpdate(throneUrl, -1, nullptr);
            
            // #ifdef Q_OS_WIN
            // DebugBox("AsyncUpdate called", "Debug - After AsyncUpdate");
            // #endif
        });
    }
    // } else {
    //     #ifdef Q_OS_WIN
    //     // DebugBox("No throne URL found in arguments", "Debug - No URL");
    //     #endif
    // }

    return QApplication::exec();
}