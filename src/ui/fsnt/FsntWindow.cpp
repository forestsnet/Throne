#include "include/ui/fsnt/FsntWindow.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QSvgRenderer>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "include/database/GroupsRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/global/Configs.hpp"
#include "include/ui/fsnt/AddSubscriptionDialog.h"
#include "include/ui/fsnt/OnboardingDialog.h"
#include "include/ui/fsnt/ConnectPanel.h"
#include "include/ui/mainwindow.h"
#include "include/ui/fsnt/FsntSettingsDialog.h"
#include "include/ui/fsnt/FsntControls.h"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/ui/fsnt/ServerListPanel.h"
#include "include/ui/fsnt/SubscriptionCard.h"
#include "include/ui/fsnt/UiMode.hpp"
#include "include/global/Logger.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/setting/ThemeManager.hpp"

FsntWindow::FsntWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("FSNT Client");
    resize(960, 640);
    setMinimumSize(820, 560);

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

    // Мастер поднимаем после первого прохода цикла событий: до show() модальный
    // диалог встал бы поверх ещё не отрисованного окна.
    if (!Configs::dataManager->settingsRepo->flag_tray && OnboardingDialog::ShouldRun()) {
        QTimer::singleShot(0, this, [this] {
            auto dialog = OnboardingDialog(this);
            dialog.exec();
            // Мастер мог добавить подписку и сменить транспорт — перечитываем всё.
            refreshServerList();
            refreshConnectionState();
        });
    }
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


QPixmap FsntWindow::renderLogo(int size) {
    // Вектор рисуем под плотность экрана: на Retina иначе получится мыло.
    const qreal dpr = devicePixelRatioF();
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(QString(":/brand/fn-logo.svg"));
    if (!renderer.isValid()) return pixmap;

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(0, 0, size, size));
    return pixmap;
}

void FsntWindow::buildHeader(QVBoxLayout *root) {
    auto *header = new QWidget(this);
    header->setObjectName("fsntHeader");
    header->setFixedHeight(52);

    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(Fsnt::kPanelPadding, 0, Fsnt::kPanelPadding, 0);
    row->setSpacing(8);

    auto *logo = new QLabel(header);
    logo->setObjectName("fsntLogo");
    logo->setPixmap(renderLogo(26));
    logo->setFixedSize(30, 30);
    logo->setAlignment(Qt::AlignCenter);
    row->addWidget(logo);

    auto *title = new QLabel("FSNT Client", header);
    title->setObjectName("fsntTitle");
    row->addWidget(title);

    row->addStretch();

    auto *settings = new QToolButton(header);
    settings->setObjectName("fsntGearButton");
    settings->setText("⚙");
    settings->setFixedSize(38, 38);
    settings->setCursor(Qt::PointingHandCursor);
    settings->setToolTip(tr("Settings"));
    connect(settings, &QToolButton::clicked, this, [this] {
        auto *dialog = new FsntSettingsDialog(this);
        connect(dialog, &FsntSettingsDialog::advancedModeRequested,
                this, &FsntWindow::switchToAdvancedMode);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->exec();
    });
    row->addWidget(settings);

    auto *advanced = new QToolButton(header);
    advanced->setObjectName("fsntIconButton");
    advanced->setCursor(Qt::PointingHandCursor);
    advanced->setText(tr("Advanced mode"));
    advanced->setToolTip(tr("Switch to the advanced interface"));
    connect(advanced, &QToolButton::clicked, this, &FsntWindow::switchToAdvancedMode);
    row->addWidget(advanced);

    root->addWidget(header);
}

void FsntWindow::buildPanels(QVBoxLayout *root) {
    auto *body = new QWidget(this);
    body->setObjectName("fsntBody");
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
            const bool wantTun = Configs::dataManager->settingsRepo->simple_transport == 0;
            if (wantTun != Configs::dataManager->settingsRepo->spmode_vpn) {
                mw->set_spmode_vpn(wantTun);
            }
            mw->profile_start(profileId);
        }
    });

    auto *sidePanel = new QWidget(body);
    sidePanel->setObjectName("fsntSidePanel");
    m_sideLayout = new QVBoxLayout(sidePanel);
    m_sideLayout->setContentsMargins(Fsnt::kPanelPadding, Fsnt::kPanelPadding,
                                     Fsnt::kPanelPadding, Fsnt::kPanelPadding);

    m_connectPanel = new ConnectPanel(sidePanel);
    m_sideLayout->addWidget(m_connectPanel, 1);
    connect(m_connectPanel, &ConnectPanel::subscriptionNeeded,
            this, &FsntWindow::openAddSubscription);
    // Подпись под кнопкой и выделенная строка обязаны показывать один сервер.
    connect(m_connectPanel, &ConnectPanel::profileResolved,
            m_serverList, &ServerListPanel::selectProfile);
    connect(m_serverList, &ServerListPanel::serverSelected,
            this, [this](int) { refreshConnectionState(); });

    m_subscriptionCard = new SubscriptionCard(sidePanel);
    m_sideLayout->addWidget(m_subscriptionCard);

    connect(m_serverList, &ServerListPanel::addSubscriptionRequested,
            this, &FsntWindow::openAddSubscription);

    columns->addWidget(serverPanel, 105);
    columns->addWidget(sidePanel, 100);

    root->addWidget(body, 1);
}

void FsntWindow::refreshConnectionState() {
    if (m_connectPanel != nullptr) m_connectPanel->refresh();
}

void FsntWindow::refreshServerList() {
    if (m_serverList != nullptr) m_serverList->reloadGroups();
    if (m_subscriptionCard != nullptr) m_subscriptionCard->refresh();
}

void FsntWindow::applyTheme() {
    setStyleSheet(Fsnt::BuildStyleSheet());
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

void FsntWindow::openAddSubscription() {
    auto *dialog = new AddSubscriptionDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // Список обновит и колбэк ядра (SubscriptionNewGroup), но он приходит не всегда:
    // импорт текста без сети группу не создаёт и сигнала не шлёт.
    connect(dialog, &QDialog::accepted, this, [this] { refreshServerList(); });
    dialog->exec();
}
