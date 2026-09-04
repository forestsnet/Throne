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
#include "include/ui/fsnt/ConnectPanel.h"
#include "include/ui/mainwindow.h"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/ui/fsnt/ServerListPanel.h"
#include "include/ui/fsnt/UiMode.hpp"
#include "include/global/Logger.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/setting/ThemeManager.hpp"

FsntWindow::FsntWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("FSNT Client");
    resize(880, 560);
    setMinimumSize(760, 500);

    // Тему и колбэки ядра уже поставил MainWindow: он создаётся раньше и служит движком.
    chainCoreMessages();

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


void FsntWindow::chainCoreMessages() {
    // Ядро шлёт состояние через единственный MW_dialog_message, и его владелец —
    // MainWindow. Не перехватываем, а оборачиваем: прежний обработчик вызывается
    // как обычно, мы лишь узнаём о событии дополнительно.
    auto previous = MW_dialog_message;
    MW_dialog_message = [this, previous](MwMessage cmd, QStringList args) {
        if (previous) previous(cmd, args);
        QMetaObject::invokeMethod(this, [this, cmd, args] { onCoreMessage(cmd, args); },
                                  Qt::QueuedConnection);
    };
}

void FsntWindow::onCoreMessage(MwMessage cmd, const QStringList &args) {
    Q_UNUSED(args)
    switch (cmd) {
        case MwMessage::CoreStarted:
        case MwMessage::CoreCrashed:
        case MwMessage::ProfileChanged:
            refreshConnectionState();
            break;
        case MwMessage::GroupsChanged:
        case MwMessage::SubscriptionFinished:
        case MwMessage::SubscriptionGroupChanged:
        case MwMessage::SubscriptionNewGroup:
            refreshServerList();
            refreshConnectionState();
            break;
        default:
            break;
    }
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

    m_serverList = new ServerListPanel(serverPanel);
    m_serverLayout->addWidget(m_serverList);

    connect(m_serverList, &ServerListPanel::serverActivated, this, [](int profileId) {
        if (auto *mw = GetMainWindow()) {
            if (!Configs::dataManager->settingsRepo->spmode_vpn) mw->set_spmode_vpn(true);
            mw->profile_start(profileId);
        }
    });

    auto *sidePanel = new QWidget(body);
    m_sideLayout = new QVBoxLayout(sidePanel);
    m_sideLayout->setContentsMargins(Fsnt::kPanelPadding, Fsnt::kPanelPadding,
                                     Fsnt::kPanelPadding, Fsnt::kPanelPadding);

    m_connectPanel = new ConnectPanel(sidePanel);
    m_sideLayout->addWidget(m_connectPanel);

    columns->addWidget(serverPanel, 105);
    columns->addWidget(sidePanel, 100);

    root->addWidget(body, 1);
}

void FsntWindow::refreshConnectionState() {
    if (m_connectPanel != nullptr) m_connectPanel->refresh();
}

void FsntWindow::refreshServerList() {
    if (m_serverList != nullptr) m_serverList->reloadGroups();
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
