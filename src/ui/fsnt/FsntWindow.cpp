#include "include/ui/fsnt/FsntWindow.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QCloseEvent>
#include <QMenu>
#include <QUrl>
#include <QHBoxLayout>
#include <QLabel>
#include <QSvgRenderer>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "include/database/GroupsRepo.h"
#include "NkrVersion.h"

#include "include/database/SettingsRepo.h"
#include "include/global/Configs.hpp"
#include "include/ui/fsnt/AddSubscriptionDialog.h"
#include "include/ui/fsnt/OnboardingDialog.h"
#include "include/ui/fsnt/ConnectPanel.h"
#include "include/ui/mainwindow.h"
#include "include/ui/fsnt/FsntSettingsDialog.h"
#include "include/ui/fsnt/FsntControls.h"
#include "include/ui/fsnt/FsntLogDialog.h"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/ui/fsnt/FsntToast.h"
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
    chainLogLines();

    auto *central = new QWidget(this);
    central->setObjectName("fsntRoot");
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildHeader(root);
    buildPanels(root);

    // Поверх центрального виджета, а не в компоновке: иначе появление
    // уведомления сдвигало бы всё окно вниз.
    m_toast = new FsntToast(central);

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
    // QPointer, а не this: колбэк переживает окно. При выходе MainWindow
    // продолжает слать сообщения и писать лог, и вызов по указателю на уже
    // разрушенное окно — обращение к освобождённой памяти.
    auto previous = MW_dialog_message;
    QPointer<FsntWindow> self(this);
    MW_dialog_message = [self, previous](MwMessage cmd, QStringList args) {
        if (previous) previous(cmd, args);
        if (self.isNull()) return;
        QMetaObject::invokeMethod(self, [self, cmd, args] {
            if (!self.isNull()) self->onCoreMessage(cmd, args);
        }, Qt::QueuedConnection);
    };
}

void FsntWindow::chainLogLines() {
    // Как и с сообщениями ядра: не перехватываем, а оборачиваем — прежний
    // обработчик MainWindow должен продолжать писать лог в файл и в окно.
    auto previous = MW_show_log;
    QPointer<FsntWindow> self(this);
    MW_show_log = [self, previous](const QString &line) {
        if (previous) previous(line);
        if (self.isNull()) return;
        QMetaObject::invokeMethod(self, [self, line] {
            if (!self.isNull()) emit self->logLine(line);
        }, Qt::QueuedConnection);
    };
}

void FsntWindow::onCoreMessage(MwMessage cmd, const QStringList &args) {
    Q_UNUSED(args)
    switch (cmd) {
        case MwMessage::CoreCrashed:
            // Отдельной веткой: refresh() по одному лишь «не подключено» не
            // отличит упавшую попытку от ещё идущей.
            if (m_connectPanel != nullptr) m_connectPanel->reportFailure();
            refreshConnectionState();
            break;
        case MwMessage::CoreStarted:
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
    return Fsnt::BrandMark(size, devicePixelRatioF());
}

void FsntWindow::buildHeader(QVBoxLayout *root) {
    auto *header = new QWidget(this);
    header->setObjectName("fsntHeader");
    header->setFixedHeight(52);

    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(Fsnt::kPanelPadding, 0, Fsnt::kPanelPadding, 0);
    row->setSpacing(8);

    m_logo = new QLabel(header);
    m_logo->setObjectName("fsntLogo");
    m_logo->setPixmap(renderLogo(26));
    m_logo->setFixedSize(30, 30);
    m_logo->setAlignment(Qt::AlignCenter);
    row->addWidget(m_logo);

    auto *title = new QLabel("FSNT Client", header);
    title->setObjectName("fsntTitle");
    row->addWidget(title);

    row->addStretch();

    auto *settings = new FsntIconButton(Fsnt::Glyph::Gear, header);
    settings->setFixedSize(38, 38);
    settings->setToolTip(tr("Settings"));
    connect(settings, &FsntIconButton::clicked, this, [this] {
        auto *dialog = new FsntSettingsDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->exec();
    });
    row->addWidget(settings);

    auto *logs = new FsntIconButton(Fsnt::Glyph::Logs, header);
    logs->setFixedSize(38, 38);
    logs->setToolTip(tr("Logs"));
    connect(logs, &FsntIconButton::clicked, this, [this] {
        // Немодально: за логом смотрят как раз тогда, когда что-то нажимают
        // в основном окне.
        auto *dialog = new FsntLogDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(this, &FsntWindow::logLine, dialog, &FsntLogDialog::appendLine);
        dialog->show();
    });
    row->addWidget(logs);

    auto *more = new FsntIconButton(Fsnt::Glyph::More, header);
    more->setFixedSize(38, 38);
    more->setToolTip(tr("More"));
    connect(more, &FsntIconButton::clicked, this, [this, more] { showMainMenu(more); });
    row->addWidget(more);

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
    connect(m_serverList, &ServerListPanel::notice, this, [this](const QString &text) {
        if (m_toast != nullptr) m_toast->show(text);
    });

    m_subscriptionCard = new SubscriptionCard(sidePanel);
    m_sideLayout->addWidget(m_subscriptionCard);

    connect(m_serverList, &ServerListPanel::addSubscriptionRequested,
            this, &FsntWindow::openAddSubscription);

    columns->addWidget(serverPanel, 105);
    columns->addWidget(sidePanel, 100);

    root->addWidget(body, 1);
}

void FsntWindow::showMainMenu(QWidget *anchor) {
    QMenu menu(this);
    menu.setStyleSheet(Fsnt::BuildStyleSheet());

    // NKR_VERSION подставляется из тега при сборке; локально он пуст, и
    // «FSNT Client » с висячим пробелом выглядело бы недоделкой.
    const QString build = QStringLiteral(NKR_VERSION);
    auto *version = menu.addAction(build.isEmpty()
                                       ? tr("FSNT Client · development build")
                                       : QStringLiteral("FSNT Client %1").arg(build));
    version->setEnabled(false);
    menu.addSeparator();

    // Обновление умеет только внешний updater; без него действие молча ничего
    // не делало бы, поэтому говорим об этом прямо.
    const QString appDir = QApplication::applicationDirPath();
    const bool hasUpdater = QFile::exists(appDir + "/updater") || QFile::exists(appDir + "/updater.exe");
    auto *update = menu.addAction(tr("Check for updates"));
    update->setEnabled(hasUpdater);
    if (!hasUpdater) update->setText(tr("Updates are unavailable in this build"));
    connect(update, &QAction::triggered, this, [this] { triggerAction("actionCheck_For_Update"); });

    connect(menu.addAction(tr("Build a support report")), &QAction::triggered, this,
            [this] { triggerAction("menu_profile_debug_info"); });

    connect(menu.addAction(tr("Open the config folder")), &QAction::triggered, this, [] {
        // Рабочий каталог приложения и есть каталог конфигурации, см. main.cpp.
        QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::currentPath()));
    });

    menu.addSeparator();
    connect(menu.addAction(tr("Advanced mode")), &QAction::triggered,
            this, &FsntWindow::switchToAdvancedMode);

    menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height() + 4)));
}

void FsntWindow::triggerAction(const char *actionName) {
    auto *mw = GetMainWindow();
    if (mw == nullptr) return;
    if (auto *action = mw->findChild<QAction *>(QString::fromLatin1(actionName))) action->trigger();
}

void FsntWindow::refreshConnectionState() {
    if (m_connectPanel != nullptr) m_connectPanel->refresh();
}

void FsntWindow::refreshServerList() {
    if (m_serverList != nullptr) m_serverList->reloadGroups();
    if (m_subscriptionCard != nullptr) m_subscriptionCard->refresh();
}

void FsntWindow::closeEvent(QCloseEvent *event) {
    // Как и MainWindow: пока есть значок в трее, окно прячется, а приложение
    // продолжает работать. Без трея прятать некуда — тогда честный выход.
    if (!Configs::dataManager->settingsRepo->disable_tray) {
        HideWindow(this);
        event->ignore();
        return;
    }
    triggerAction("menu_exit");
    event->ignore();
}

void FsntWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (m_toast != nullptr) m_toast->relayout();
}

void FsntWindow::applyTheme() {
    setStyleSheet(Fsnt::BuildStyleSheet());
    // Знак нарисован в растр под цвет текста, и сам за темой не следует.
    if (m_logo != nullptr) m_logo->setPixmap(renderLogo(26));
}

void FsntWindow::switchToAdvancedMode() {
    if (!Fsnt::Confirm(this, tr("Advanced mode"),
                       tr("The application will restart in the advanced interface. Continue?"),
                       tr("Restart"))) {
        return;
    }

    Configs::dataManager->settingsRepo->ui_mode = static_cast<int>(Fsnt::UiMode::Advanced);
    Configs::dataManager->settingsRepo->Save();

    // Перезапуск, а не подмена окна: два окна подписались бы на одни сигналы ядра дважды.
    //
    // Своими руками этого делать нельзя. QProcess::startDetached поднимал новый
    // процесс до того, как старый отпускал блокировку единственного экземпляра:
    // новый утыкался в неё, будил старого и завершался, а старый тем временем
    // выходил. Не оставалось ни одного окна — со стороны выглядело как падение.
    // MainWindow умеет это правильно: он перезапускается после полного выхода.
    MW_dialog_message(MwMessage::RestartProgram, {});
}

void FsntWindow::openAddSubscription() {
    auto *dialog = new AddSubscriptionDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // Список обновит и колбэк ядра (SubscriptionNewGroup), но он приходит не всегда:
    // импорт текста без сети группу не создаёт и сигнала не шлёт.
    connect(dialog, &QDialog::accepted, this, [this] { refreshServerList(); });
    dialog->exec();
}
