#include "include/ui/fsnt/FsntWindow.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QCloseEvent>
#include <QMenu>
#include <QMessageBox>
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
#include <QRegularExpression>
#include <QSystemTrayIcon>
#include "include/ui/fsnt/CoachMarks.h"
#include "include/ui/fsnt/FsntPalette.hpp"
#include "include/ui/fsnt/TrayMenu.h"
#include "include/ui/fsnt/Transport.hpp"
#include "include/ui/fsnt/DiagnosticsDialog.h"
#include "include/ui/fsnt/TunnelProbe.hpp"
#include "include/ui/fsnt/ConnectPanel.h"
#include "include/ui/mainwindow.h"
#include "include/ui/fsnt/FsntSettingsDialog.h"
#include "include/ui/fsnt/Notifier.hpp"
#include "include/ui/fsnt/UpdatePopover.hpp"
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
    // С этого момента диплинки и диалоги ядра поднимают это окно, а не
    // спрятанный MainWindow.
    SetFacadeWindow(this);

    setWindowTitle("FSNT Client");
    resize(960, 640);
    setMinimumSize(820, 560);

    // Значок в трее тоже создал MainWindow, но с инженерным меню. Забираем
    // его под своё: в простом режиме то меню только мешает.
    if (auto *mw = GetMainWindow(); mw != nullptr) new Fsnt::TrayMenu(mw->trayIcon(), this);

    m_probe = new Fsnt::TunnelProbe(this);
    connect(m_probe, &Fsnt::TunnelProbe::tunnelDead, this, &FsntWindow::offerCompatibleTunnel);

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

    // Экскурсию запускаем после первого прохода цикла событий: до него у
    // виджетов ещё нет окончательной геометрии, а подсвечивать надо по ней.
    // Если она всё же окажется неточной, оверлей пересчитает вырез сам по
    // событию Resize.
    if (!Configs::dataManager->settingsRepo->flag_tray && shouldRunTour()) {
        QTimer::singleShot(0, this, [this] { runTour(); });
    }
}

bool FsntWindow::shouldRunTour() {
    if (Configs::dataManager->settingsRepo->onboarding_done) return false;

    // Считаем по серверам, а не по числу групп: у чистой установки есть пустая
    // группа Default, а у обновившегося пользователя может быть ровно одна
    // подписка — по количеству групп эти два случая не различить.
    for (const int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (group && !group->Profiles().isEmpty()) return false;
    }
    return true;
}

void FsntWindow::runTour() {
    // Один оверлей на окно: повторный запуск из меню переиспользует его.
    if (m_tour == nullptr) {
        m_tour = new Fsnt::CoachMarks(this);
        connect(m_tour, &Fsnt::CoachMarks::finished, this, [](bool) {
            // И пройденная, и пропущенная экскурсия одинаково означают, что
            // навязываться больше не надо. Повторить можно из меню.
            Configs::dataManager->settingsRepo->onboarding_done = true;
            Configs::dataManager->settingsRepo->Save();
        });
    }

    QList<Fsnt::CoachMarks::Step> steps;
    steps << Fsnt::CoachMarks::Step{
        m_serverList->addSubscriptionButton(), tr("Start with a subscription"),
        tr("Many providers put an Add button right on your subscription page: it opens "
           "the client and fills everything in. If yours does not, press this plus and "
           "paste the link by hand.")};
    steps << Fsnt::CoachMarks::Step{
        m_serverList->listArea(), tr("Your servers"),
        tr("Servers appear here once the subscription loads. The number on the right is "
           "the ping: the lower it is, the faster the server answers.")};
    steps << Fsnt::CoachMarks::Step{
        m_connectPanel->powerButton(), tr("One button"),
        tr("Press it to send your traffic through the tunnel, press it again to stop. "
           "With no server picked, the fastest one is chosen for you.")};
    steps << Fsnt::CoachMarks::Step{
        m_gear, tr("Settings"),
        tr("Connection mode, DNS and the apps that go through the tunnel. Everything "
           "here has a working default, so you do not have to touch it.")};
    steps << Fsnt::CoachMarks::Step{
        m_logs, tr("Logs"),
        tr("A live log. If something goes wrong, open it and copy the last lines into "
           "your message to support.")};
    steps << Fsnt::CoachMarks::Step{
        m_more, tr("Everything else"),
        tr("App updates, a support report and the config folder. Closing the window "
           "only hides it to the tray: the tunnel keeps running.")};

    m_tour->setSteps(steps);
    m_tour->start();
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
            if (m_probe != nullptr) m_probe->start();
            break;
        case MwMessage::SubscriptionFinished:
            refreshServerList();
            refreshConnectionState();
            onSubscriptionUpdated();
            break;
        case MwMessage::GroupsChanged:
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

    // Версия рядом с названием: пользователю есть что назвать в поддержке, а
    // нам — понять, о какой сборке речь. NKR_VERSION подставляется из тега при
    // сборке, локально он пуст.
    const QString build = QStringLiteral(NKR_VERSION);
    auto *version = new QLabel(build.isEmpty() ? tr("dev") : build, header);
    version->setObjectName("fsntVersion");
    version->setToolTip(tr("Application version"));
    row->addWidget(version);

    row->addStretch();

    // Колокольчик: скрыт, пока обновления нет. Красную точку рисуем отдельным
    // виджетом поверх кнопки — так значок остаётся обычным значком.
    m_bell = new FsntIconButton(Fsnt::Glyph::Bell, header);
    m_bell->setFixedSize(38, 38);
    m_bell->setToolTip(tr("A new version is out"));
    m_bell->hide();
    m_bellDot = new QWidget(m_bell);
    m_bellDot->setFixedSize(9, 9);
    m_bellDot->move(25, 7);
    m_bellDot->setStyleSheet(QStringLiteral("background:%1;border-radius:4px;border:2px solid %2;")
                                 .arg(Fsnt::CurrentPalette().danger.name(), Fsnt::CurrentPalette().surface.name()));
    connect(m_bell, &FsntIconButton::clicked, this, &FsntWindow::showUpdateCard);
    row->addWidget(m_bell);

    auto *settings = new FsntIconButton(Fsnt::Glyph::Gear, header);
    m_gear = settings;
    settings->setFixedSize(38, 38);
    settings->setToolTip(tr("Settings"));
    connect(settings, &FsntIconButton::clicked, this, [this] {
        auto *dialog = new FsntSettingsDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->exec();
    });
    row->addWidget(settings);

    auto *logs = new FsntIconButton(Fsnt::Glyph::Logs, header);
    m_logs = logs;
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
    m_more = more;
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
            Fsnt::ApplyTransportMode();
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
    connect(m_serverList, &ServerListPanel::serverSelected, this, [this](int profileId) {
        // Выбор страны на работающем туннеле раньше только запоминался, и
        // пользователь оставался на прежнем сервере, не понимая почему.
        m_connectPanel->switchServer(profileId);
        refreshConnectionState();
    });
    connect(m_serverList, &ServerListPanel::notice, this, [this](const QString &text) {
        if (m_toast != nullptr) m_toast->show(text);
    });
    connect(m_connectPanel, &ConnectPanel::connectionChanged, this, &FsntWindow::onConnectionChanged);

    m_subscriptionCard = new SubscriptionCard(sidePanel);
    m_sideLayout->addWidget(m_subscriptionCard);

    connect(m_serverList, &ServerListPanel::groupsReloaded, this,
            &FsntWindow::maybeHintSubscriptionSwitch);
    // Список панель строит ещё в своём конструкторе — до этой подписки. Один
    // раз проверяем сами, иначе человек с двумя подписками увидит подсказку
    // только после следующего обновления.
    QTimer::singleShot(1200, this, [this] { maybeHintSubscriptionSwitch(); });

    // Разрешение спрашиваем не сразу: на старте у человека и так полный экран
    // событий, а уведомления нужны не в первую минуту.
    QTimer::singleShot(30000, this, [] { Fsnt::PrimeNotifications(); });

    m_updates = new Fsnt::UpdateWatcher(this);
    connect(m_updates, &Fsnt::UpdateWatcher::updateFound, this, &FsntWindow::onUpdateFound);
    m_updates->start();
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

    connect(menu.addAction(tr("Diagnostics")), &QAction::triggered, this, [this] {
        auto *dialog = new FsntDiagnosticsDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->exec();
    });

    connect(menu.addAction(tr("How to use the app")), &QAction::triggered,
            this, &FsntWindow::runTour);

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
    maybeHintSubscriptionSwitch();
}

void FsntWindow::showNotice(const QString &text, int milliseconds) {
    if (m_toast != nullptr) m_toast->show(text, milliseconds);
}

void FsntWindow::announce(Fsnt::NotifyKind kind, const QString &title, const QString &body,
                          const QString &inWindow, const std::function<void()> &onActivated) {
    if (!Fsnt::NotifyEnabled(kind)) return;

    // Пока окно перед человеком, хватит строки в нём самом: системный баннер
    // поверх собственного окна — это уведомление о том, что он и так видит.
    // Пустая строка означает, что окно и без нас всё показывает: обновление
    // подписки печатает свой итог, а подключение видно по кнопке и таймеру.
    if (isActiveWindow()) {
        if (!inWindow.isEmpty() && m_toast != nullptr) m_toast->show(inWindow);
        return;
    }
    Fsnt::Notify(kind, title, body, onActivated);
}

void FsntWindow::onConnectionChanged(bool connected, const QString &server) {
    QPointer<FsntWindow> self = this;
    const QString title = connected ? tr("VPN is on") : tr("VPN is off");
    const QString body = connected && !server.isEmpty()
                             ? tr("Traffic goes through %1.").arg(server)
                             : (connected ? tr("Traffic goes through the tunnel.")
                                          : tr("Traffic goes directly again."));
    announce(Fsnt::NotifyKind::Connection, title, body, QString(), [self] {
                 if (self.isNull()) return;
                 self->show();
                 self->raise();
                 self->activateWindow();
             });
}

void FsntWindow::onSubscriptionUpdated() {
    // Считаем серверы в подписках: человеку важно, что список не опустел, а не
    // сколько строк переписалось внутри.
    int servers = 0;
    for (const int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (group && !group->url.isEmpty()) servers += group->Profiles().size();
    }

    const QString body = servers > 0 ? tr("%n server(s) available.", nullptr, servers)
                                     : tr("The provider returned no servers.");
    QPointer<FsntWindow> self = this;
    announce(Fsnt::NotifyKind::Subscription, tr("Subscription updated"), body, QString(), [self] {
                 if (self.isNull()) return;
                 self->show();
                 self->raise();
                 self->activateWindow();
             });
}

void FsntWindow::onUpdateFound(const QString &tag, const QString &notes) {
    m_updateTag = tag;
    m_updateNotes = notes;
    if (m_bell != nullptr) m_bell->show();

    const auto &settings = Configs::dataManager->settingsRepo;
    // Системным уведомлением дёргаем один раз на версию: человек и так увидит
    // точку, когда откроет окно, а всплывать на каждой проверке — навязчиво.
    if (settings->update_seen_version == tag) return;
    settings->update_seen_version = tag;
    settings->Save();

    QPointer<FsntWindow> self = this;
    announce(Fsnt::NotifyKind::Update, tr("FSNT Client %1 is out").arg(tag),
             tr("Click here to update — it takes about a minute."),
             tr("FSNT Client %1 is out — press the bell").arg(tag), [self] {
                 if (self.isNull()) return;
                 self->show();
                 self->raise();
                 self->activateWindow();
                 self->showUpdateCard();
             });
}

void FsntWindow::showUpdateCard() {
    if (m_updateTag.isEmpty() || m_bell == nullptr) return;

    // Первые строки описания релиза: полный список изменений человеку здесь не
    // нужен, для него есть ссылка.
    QString summary;
    for (const QString &line : m_updateNotes.split('\n')) {
        const QString clean = QString(line).remove(QRegularExpression("^[#*\\-\\s]+")).trimmed();
        if (clean.isEmpty()) continue;
        summary += (summary.isEmpty() ? "" : "\n") + clean;
        if (summary.count('\n') >= 2) break;
    }

    // Описание к релизу пишут не всегда; без него человеку полезнее знать, что
    // именно сделает кнопка, чем ещё раз прочитать заголовок другими словами.
    const QString body = summary.isEmpty()
                             ? tr("You have %1 installed. Press Update and the client will download and install "
                                  "the new version itself.")
                                   .arg(QStringLiteral(NKR_VERSION))
                             : summary;

    auto *card = new Fsnt::UpdatePopover(m_bell, tr("Version %1 is out").arg(m_updateTag), body);
    m_bell->setActive(true);
    connect(card, &QObject::destroyed, this, [this] {
        if (m_bell != nullptr) m_bell->setActive(false);
    });
    connect(card, &Fsnt::UpdatePopover::updateRequested, this, [this] {
        triggerAction("actionCheck_For_Update");
    });
    connect(card, &Fsnt::UpdatePopover::notesRequested, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/forestsnet/Throne/releases/latest")));
    });
    connect(card, &Fsnt::UpdatePopover::postponed, this, [this] {
        // «Позже» гасит колокольчик до следующей версии: напоминать про то же
        // самое — ровно то, что раздражает в чужих программах.
        if (m_bell != nullptr) m_bell->hide();
    });
    card->popup();
}

void FsntWindow::maybeHintSubscriptionSwitch() {
    const auto &settings = Configs::dataManager->settingsRepo;
    if (settings->hint_switch_subs || m_serverList == nullptr) return;
    // Пока идёт экскурсия, лезть со своей подсказкой некуда.
    if (m_tour != nullptr && m_tour->isVisible()) return;

    // Считаем подписки с серверами: пустая группа Default есть у всех, и по
    // числу групп «вторая подписка» не отличить от чистой установки.
    int withServers = 0;
    for (const int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (group && !group->Profiles().isEmpty()) ++withServers;
    }
    if (withServers < 2) return;

    settings->hint_switch_subs = true;
    settings->Save();

    auto *hint = new Fsnt::CoachMarks(this);
    connect(hint, &Fsnt::CoachMarks::finished, hint, &QObject::deleteLater);
    hint->setSteps({Fsnt::CoachMarks::Step{
        m_serverList->subscriptionSelector(), tr("Now you have two subscriptions"),
        tr("Servers live separately in each one. This is where you switch between them: "
           "pick a subscription and its own servers appear in the list below.")}});
    // Список только что перестроился — дадим ему встать на место, иначе
    // подсветка ляжет по старым координатам.
    QTimer::singleShot(150, hint, [hint] { hint->start(); });
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

void FsntWindow::offerCompatibleTunnel() {
#ifdef Q_OS_WIN
    auto &settings = Configs::dataManager->settingsRepo;
    // Совместимый стек уже стоит — значит дело не в нём, и советовать нечего.
    if (settings->vpn_implementation != QLatin1String("system")) return;
    if (m_compatibleOffered) return;
    m_compatibleOffered = true;

    ActivateUiWindow();
    const auto answer = QMessageBox::question(
        this, tr("Connected, but nothing opens"),
        tr("The tunnel is up, but no traffic goes through it. Usually another program on "
           "this computer intercepts network packets: an antivirus, a DPI-bypass tool or "
           "what another VPN left behind.\n\n"
           "Switch the tunnel to compatible mode and reconnect? It is a little slower, "
           "but it does not depend on them."));
    if (answer != QMessageBox::Yes) return;

    settings->vpn_implementation = QStringLiteral("gvisor");
    settings->Save();
    if (auto *mw = GetMainWindow(); mw != nullptr && settings->started_id >= 0) {
        mw->profile_start(settings->started_id);
    }
#endif
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
