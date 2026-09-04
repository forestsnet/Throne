#include "include/ui/fsnt/FsntWindow.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "include/database/SettingsRepo.h"
#include "include/global/Configs.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/ui/fsnt/UiMode.hpp"
#include "include/global/Logger.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/setting/ThemeManager.hpp"

FsntWindow::FsntWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("FSNT Client");
    resize(880, 560);
    setMinimumSize(760, 500);

    registerCoreCallbacks();

    // Тема глобальная и применяется тем окном, которое стартует. MainWindow делает
    // это в своём конструкторе; без этого вызова палитра остаётся пустой и всё чёрное.
    themeManager()->ApplyTheme(Configs::dataManager->settingsRepo->theme);

    auto *central = new QWidget(this);
    central->setObjectName("fsntRoot");
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildHeader(root);
    buildPanels(root);

    applyTheme();
    connect(themeManager(), &ThemeManager::themeChanged, this, [this] { applyTheme(); });

    // Как и MainWindow: при запуске в трей окно не показываем.
    if (!Configs::dataManager->settingsRepo->flag_tray) show();
}


void FsntWindow::registerCoreCallbacks() {
    // Ядро вызывает окно обратно через эти глобальные std::function. MainWindow
    // регистрирует их в своём конструкторе; без регистрации первый же вызов из
    // ядра падает с std::bad_function_call.
    //
    // В каркасе реализации минимальные: логи уходят в общий журнал, остальные
    // сообщения пока не обрабатываются — их наполняют следующие приросты.
    MW_show_log = [](const QString &message) {
        Logging::WriteUserLog(message);
    };

    MW_dialog_message = [](MwMessage cmd, QStringList args) {
        Q_UNUSED(args)
        LOG_DEBUG(QString("[FSNT] unhandled dialog message: %1").arg(static_cast<int>(cmd)));
    };

    MW_handle_deeplink = [](const QString &url) {
        Q_UNUSED(url)
        LOG_DEBUG("[FSNT] deeplink ignored: no handler in the shell yet");
    };

    MW_import_files = [](const QStringList &paths) {
        Q_UNUSED(paths)
        LOG_DEBUG("[FSNT] file import ignored: no handler in the shell yet");
    };
}

void FsntWindow::buildHeader(QVBoxLayout *root) {
    auto *header = new QWidget(this);
    header->setObjectName("fsntHeader");
    header->setFixedHeight(44);

    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(Fsnt::kPanelPadding, 0, Fsnt::kPanelPadding, 0);
    row->setSpacing(8);

    auto *logo = new QLabel("FN", header);
    logo->setObjectName("fsntLogo");
    row->addWidget(logo);

    auto *title = new QLabel("FSNT Client", header);
    title->setObjectName("fsntTitle");
    row->addWidget(title);

    row->addStretch();

    auto *advanced = new QToolButton(header);
    advanced->setObjectName("fsntIconButton");
    advanced->setText(tr("Advanced mode"));
    advanced->setToolTip(tr("Switch to the advanced interface"));
    connect(advanced, &QToolButton::clicked, this, &FsntWindow::switchToAdvancedMode);
    row->addWidget(advanced);

    root->addWidget(header);
}

void FsntWindow::buildPanels(QVBoxLayout *root) {
    auto *body = new QWidget(this);
    auto *columns = new QHBoxLayout(body);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(0);

    auto *serverPanel = new QWidget(body);
    serverPanel->setObjectName("fsntServerPanel");
    m_serverLayout = new QVBoxLayout(serverPanel);
    m_serverLayout->setContentsMargins(Fsnt::kPanelPadding, Fsnt::kPanelPadding,
                                       Fsnt::kPanelPadding, Fsnt::kPanelPadding);

    auto *serverStub = new QLabel(tr("Server list appears here"), serverPanel);
    serverStub->setObjectName("fsntPlaceholder");
    serverStub->setAlignment(Qt::AlignCenter);
    m_serverLayout->addWidget(serverStub);

    auto *sidePanel = new QWidget(body);
    m_sideLayout = new QVBoxLayout(sidePanel);
    m_sideLayout->setContentsMargins(Fsnt::kPanelPadding, Fsnt::kPanelPadding,
                                     Fsnt::kPanelPadding, Fsnt::kPanelPadding);

    auto *sideStub = new QLabel(tr("Connection and subscription appear here"), sidePanel);
    sideStub->setObjectName("fsntPlaceholder");
    sideStub->setAlignment(Qt::AlignCenter);
    m_sideLayout->addWidget(sideStub);

    columns->addWidget(serverPanel, 105);
    columns->addWidget(sidePanel, 100);

    root->addWidget(body, 1);
}

void FsntWindow::applyTheme() {
    setStyleSheet(Fsnt::BuildStyleSheet(themeManager()->tokens));
}

void FsntWindow::switchToAdvancedMode() {
    const auto answer = QMessageBox::question(
        this, tr("Advanced mode"),
        tr("The application will restart in the advanced interface. Continue?"));
    if (answer != QMessageBox::StandardButton::Yes) return;

    Configs::dataManager->settingsRepo->ui_mode = static_cast<int>(Fsnt::UiMode::Advanced);
    Configs::dataManager->settingsRepo->Save();

    // Перезапуск, а не подмена окна: два окна подписались бы на одни сигналы ядра дважды.
    // Механизм ExitReason живёт в MainWindow и отсюда недоступен, поэтому перезапускаемся сами.
    QProcess::startDetached(QApplication::applicationFilePath(), {});
    QApplication::quit();
}
