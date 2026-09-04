#include "include/ui/fsnt/FsntSettingsDialog.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDesktopServices>
#include <QDir>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include "NkrVersion.h"

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/database/GroupsRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/global/Configs.hpp"
#include "include/sys/AutoRun.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/ui/mainwindow.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/setting/dialog_per_app_proxy.h"

namespace {
    // Интервалы автообновления подписок в минутах, как их хранит sub_auto_update.
    constexpr int kAutoUpdateMinutes[] = {0, 30, 60, 120, 360, 720, 1440};
}

FsntSettingsDialog::FsntSettingsDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("fsntDialog");
    setWindowTitle(tr("Settings"));
    setModal(true);
    resize(520, 640);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(14);

    auto *title = new QLabel(tr("Settings"), this);
    title->setObjectName("fsntDialogTitle");
    layout->addWidget(title);

    // Настроек стало заметно больше одного экрана — без прокрутки диалог
    // пришлось бы либо резать, либо растягивать выше экрана ноутбука.
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // Длинные подписи не должны превращаться в горизонтальную прокрутку.
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);

    auto *host = new QWidget(scroll);
    host->setAutoFillBackground(false);
    auto *column = new QVBoxLayout(host);
    column->setContentsMargins(0, 0, 10, 0);
    column->setSpacing(16);

    buildConnection(column, host);
    buildSubscriptions(column, host);
    buildApplication(column, host);
    buildSupport(column, host);
    column->addStretch();

    scroll->setWidget(host);
    layout->addWidget(scroll, 1);

    auto *cancel = new QPushButton(tr("Cancel"), this);
    cancel->setObjectName("fsntGhost");
    cancel->setCursor(Qt::PointingHandCursor);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    auto *ok = new QPushButton(tr("Save"), this);
    ok->setObjectName("fsntPrimary");
    ok->setCursor(Qt::PointingHandCursor);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, [this] {
        save();
        accept();
    });

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    layout->addLayout(buttons);

    setStyleSheet(Fsnt::BuildStyleSheet());
}

QFormLayout *FsntSettingsDialog::addSection(QVBoxLayout *column, QWidget *host,
                                            const QString &title) {
    auto *label = new QLabel(title, host);
    label->setObjectName("fsntSectionLabel");
    column->addWidget(label);

    auto *card = new QWidget(host);
    card->setObjectName("fsntCard");
    auto *form = new QFormLayout(card);
    form->setContentsMargins(14, 14, 14, 14);
    form->setSpacing(12);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    column->addWidget(card);
    return form;
}

void FsntSettingsDialog::triggerMainWindowAction(const char *actionName) {
    auto *mw = GetMainWindow();
    if (mw == nullptr) return;
    if (auto *action = mw->findChild<QAction *>(QString::fromLatin1(actionName))) action->trigger();
}

void FsntSettingsDialog::buildConnection(QVBoxLayout *column, QWidget *host) {
    auto *form = addSection(column, host, tr("Connection"));
    const auto &settings = Configs::dataManager->settingsRepo;

    m_transport = new QComboBox(host);
    m_transport->setObjectName("fsntSelect");
    m_transport->addItem(tr("Full tunnel (TUN)"), 0);
    m_transport->addItem(tr("System proxy"), 1);
    m_transport->setCurrentIndex(qBound(0, settings->simple_transport, 1));
    form->addRow(tr("Connection mode"), m_transport);

    m_route = new QComboBox(host);
    m_route->setObjectName("fsntSelect");
    for (const auto &profile : Configs::dataManager->routesRepo->GetAllRouteProfiles()) {
        if (!profile) continue;
        m_route->addItem(profile->name, profile->id);
        if (profile->id == settings->current_route_id) {
            m_route->setCurrentIndex(m_route->count() - 1);
        }
    }
    form->addRow(tr("Routing"), m_route);

    m_autoConnect = new QCheckBox(tr("Reconnect on start"), host);
    m_autoConnect->setChecked(settings->remember_enable);
    form->addRow(m_autoConnect);

    m_allowLan = new QCheckBox(tr("Allow local network access"), host);
    m_allowLan->setChecked(QStringList{"::", "0.0.0.0"}.contains(settings->inbound_address));
    form->addRow(m_allowLan);

    auto *perApp = new QPushButton(tr("Choose apps to route…"), host);
    perApp->setObjectName("fsntGhost");
    perApp->setCursor(Qt::PointingHandCursor);
    connect(perApp, &QPushButton::clicked, this, [this] {
        auto *dialog = new DialogPerAppProxy(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->exec();
    });
    form->addRow(perApp);
}

void FsntSettingsDialog::buildSubscriptions(QVBoxLayout *column, QWidget *host) {
    auto *form = addSection(column, host, tr("Subscriptions"));
    const auto &settings = Configs::dataManager->settingsRepo;

    m_subAutoUpdate = new QComboBox(host);
    m_subAutoUpdate->setObjectName("fsntSelect");
    m_subAutoUpdate->addItem(tr("Off"), 0);
    m_subAutoUpdate->addItem(tr("Every 30 minutes"), 30);
    m_subAutoUpdate->addItem(tr("Every hour"), 60);
    m_subAutoUpdate->addItem(tr("Every 2 hours"), 120);
    m_subAutoUpdate->addItem(tr("Every 6 hours"), 360);
    m_subAutoUpdate->addItem(tr("Every 12 hours"), 720);
    m_subAutoUpdate->addItem(tr("Once a day"), 1440);
    // Интервал хранится знаковым, отрицательное значение — та же периодичность.
    const int stored = qAbs(settings->sub_auto_update);
    for (int index = 0; index < static_cast<int>(std::size(kAutoUpdateMinutes)); ++index) {
        if (kAutoUpdateMinutes[index] == stored) {
            m_subAutoUpdate->setCurrentIndex(index);
            break;
        }
    }
    form->addRow(tr("Auto update"), m_subAutoUpdate);

    auto *updateAll = new QPushButton(tr("Update all now"), host);
    updateAll->setObjectName("fsntGhost");
    updateAll->setCursor(Qt::PointingHandCursor);
    connect(updateAll, &QPushButton::clicked, this,
            [] { Subscription::updater()->RefreshAll(); });
    form->addRow(updateAll);

    auto *remove = new QPushButton(tr("Remove the current subscription"), host);
    remove->setObjectName("fsntGhost");
    remove->setCursor(Qt::PointingHandCursor);
    // Действие расширенного режима уже умеет всё: не даёт удалить последнюю
    // группу, уважает pin провайдера и останавливает работающий профиль.
    connect(remove, &QPushButton::clicked, this, [this] {
        triggerMainWindowAction("actionDelete_Group");
    });
    form->addRow(remove);
}

void FsntSettingsDialog::buildApplication(QVBoxLayout *column, QWidget *host) {
    auto *form = addSection(column, host, tr("Application"));
    const auto &settings = Configs::dataManager->settingsRepo;

    m_language = new QComboBox(host);
    m_language->setObjectName("fsntSelect");
    // Индексы совпадают со switch в main.cpp: 0 системный, 1 English, 2 中文, 3 فارسی, 4 русский.
    m_language->addItem(tr("System"), 0);
    m_language->addItem("English", 1);
    m_language->addItem("简体中文", 2);
    m_language->addItem("فارسی", 3);
    m_language->addItem("Русский", 4);
    m_language->setCurrentIndex(qBound(0, settings->language, 4));
    form->addRow(tr("Language"), m_language);

    m_theme = new QComboBox(host);
    m_theme->setObjectName("fsntSelect");
    // Значения — те же строки, что понимает ThemeManager::ApplyTheme.
    // Простому режиму хватает трёх: остальные варианты живут в расширенном.
    m_theme->addItem(tr("System"), "System");
    m_theme->addItem(tr("Dark"), "QDarkStyle");
    m_theme->addItem(tr("Light"), "LightBlue");
    for (int i = 0; i < m_theme->count(); ++i) {
        if (m_theme->itemData(i).toString().compare(settings->theme, Qt::CaseInsensitive) == 0) {
            m_theme->setCurrentIndex(i);
            break;
        }
    }
    form->addRow(tr("Theme"), m_theme);

    m_autoRun = new QCheckBox(tr("Launch at login"), host);
    m_autoRun->setChecked(AutoRun_IsEnabled());
    form->addRow(m_autoRun);

    m_startMinimal = new QCheckBox(tr("Start minimized to tray"), host);
    m_startMinimal->setChecked(settings->start_minimal);
    form->addRow(m_startMinimal);

    auto *hint = new QLabel(tr("Language changes apply after restarting the application."), host);
    hint->setObjectName("fsntSubMeta");
    hint->setWordWrap(true);
    form->addRow(hint);
}

void FsntSettingsDialog::buildSupport(QVBoxLayout *column, QWidget *host) {
    auto *form = addSection(column, host, tr("Help"));

    auto *report = new QPushButton(tr("Build a support report"), host);
    report->setObjectName("fsntGhost");
    report->setCursor(Qt::PointingHandCursor);
    connect(report, &QPushButton::clicked, this,
            [this] { triggerMainWindowAction("menu_profile_debug_info"); });
    form->addRow(report);

    auto *update = new QPushButton(tr("Check for updates"), host);
    update->setObjectName("fsntGhost");
    update->setCursor(Qt::PointingHandCursor);
    connect(update, &QPushButton::clicked, this,
            [this] { triggerMainWindowAction("actionCheck_For_Update"); });
    form->addRow(update);

    auto *folder = new QPushButton(tr("Open the config folder"), host);
    folder->setObjectName("fsntGhost");
    folder->setCursor(Qt::PointingHandCursor);
    connect(folder, &QPushButton::clicked, this, [] {
        // Рабочий каталог приложения и есть каталог конфигурации, см. main.cpp.
        QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::currentPath()));
    });
    form->addRow(folder);

    auto *advanced = new QPushButton(tr("Open advanced mode"), host);
    advanced->setObjectName("fsntGhost");
    advanced->setCursor(Qt::PointingHandCursor);
    connect(advanced, &QPushButton::clicked, this, [this] {
        save();
        emit advancedModeRequested();
        accept();
    });
    form->addRow(advanced);

    auto *version = new QLabel(QString("FSNT Client · %1").arg(NKR_VERSION), host);
    version->setObjectName("fsntSubMeta");
    version->setAlignment(Qt::AlignCenter);
    form->addRow(version);
}

void FsntSettingsDialog::save() {
    auto &settings = Configs::dataManager->settingsRepo;

    settings->simple_transport = m_transport->currentData().toInt();
    settings->language = m_language->currentData().toInt();
    settings->start_minimal = m_startMinimal->isChecked();
    settings->remember_enable = m_autoConnect->isChecked();
    settings->inbound_address = m_allowLan->isChecked() ? "::" : "127.0.0.1";

    if (m_route->currentIndex() >= 0) {
        settings->current_route_id = m_route->currentData().toInt();
    }

    // Знак интервала несёт свой смысл в расширенном режиме, поэтому меняем
    // только величину и сохраняем прежний знак.
    const int minutes = m_subAutoUpdate->currentData().toInt();
    settings->sub_auto_update = settings->sub_auto_update < 0 ? -minutes : minutes;

    const QString theme = m_theme->currentData().toString();
    const bool themeChanged = theme.compare(settings->theme, Qt::CaseInsensitive) != 0;
    settings->theme = theme;

    settings->Save();

    // ApplyTheme перестроит ThemeTokens и пошлёт themeChanged; окно простого
    // режима подписано на него и пересоберёт свой лист само.
    if (themeChanged) themeManager()->ApplyTheme(theme);

    // Автозапуск живёт не в базе, а в системе, и пишется отдельно.
    if (m_autoRun->isChecked() != AutoRun_IsEnabled()) {
        AutoRun_SetEnabled(m_autoRun->isChecked());
    }

    // Смена адреса приёма и маршрута требует пересборки конфига ядра.
    if (MW_dialog_message) MW_dialog_message(MwMessage::UpdateSettings, {});
}
