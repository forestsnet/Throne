#include <iterator>

#include "include/ui/fsnt/FsntSettingsDialog.h"

#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "NkrVersion.h"

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/configs/sub/ProviderPolicy.hpp"
#include "include/database/GroupsRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/global/Configs.hpp"
#include "include/sys/AutoRun.hpp"
#include "include/ui/fsnt/FsntControls.h"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/ui/fsnt/SettingsCard.h"
#include "include/ui/mainwindow.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/setting/dialog_per_app_proxy.h"

namespace {
    // Интервалы автообновления подписок в минутах, как их хранит sub_auto_update.
    constexpr int kAutoUpdateMinutes[] = {0, 30, 60, 120, 360, 720, 1440};

    FsntSelect *makeSelect(QWidget *parent) {
        auto *box = new FsntSelect(parent);
        box->setMinimumWidth(190);
        return box;
    }
}

FsntSettingsDialog::FsntSettingsDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("fsntDialog");
    setWindowTitle(tr("Settings"));
    setModal(true);
    resize(560, 660);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(14);

    auto *title = new QLabel(tr("Settings"), this);
    title->setObjectName("fsntDialogTitle");
    layout->addWidget(title);

    // Настроек заметно больше одного экрана — без прокрутки диалог пришлось бы
    // либо резать, либо растягивать выше экрана ноутбука.
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);

    auto *host = new QWidget(scroll);
    host->setAutoFillBackground(false);
    auto *column = new QVBoxLayout(host);
    column->setContentsMargins(0, 0, 10, 0);
    column->setSpacing(18);

    buildConnection(column, host);
    buildDns(column, host);
    buildSubscriptions(column, host);
    buildApplication(column, host);
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

void FsntSettingsDialog::triggerMainWindowAction(const char *actionName) {
    auto *mw = GetMainWindow();
    if (mw == nullptr) return;
    if (auto *action = mw->findChild<QAction *>(QString::fromLatin1(actionName))) action->trigger();
}

void FsntSettingsDialog::buildConnection(QVBoxLayout *column, QWidget *host) {
    Fsnt::SettingsCard card(column, host, tr("Connection"));
    const auto &settings = Configs::dataManager->settingsRepo;

    m_transport = makeSelect(host);
    m_transport->addItem(tr("Full tunnel (TUN)"), 0);
    m_transport->addItem(tr("System proxy"), 1);
    m_transport->setCurrentIndex(qBound(0, settings->simple_transport, 1));
    card.addControl(tr("Connection mode"), m_transport);

    // Провайдер может закрыть настройки, которыми управляет сам. Прячем ровно
    // маршрутизацию: и профиль маршрутов, и выбор приложений — это она же.
    // Режим подключения и автозапуск остаются: пользователь должен видеть,
    // что происходит. Ограничение снимается остановкой профиля.
    const bool providerManagesRouting = Subscription::PolicyHidesSettings();

    if (!providerManagesRouting) {
        m_route = makeSelect(host);
        for (const auto &profile : Configs::dataManager->routesRepo->GetAllRouteProfiles()) {
            if (!profile) continue;
            m_route->addItem(profile->name, profile->id);
            if (profile->id == settings->current_route_id) {
                m_route->setCurrentIndex(m_route->count() - 1);
            }
        }
        card.addControl(tr("Routing"), m_route);
    }

    m_autoConnect = card.addToggle(tr("Reconnect on start"), settings->remember_enable);
    m_allowLan = card.addToggle(tr("Allow local network access"),
                                QStringList{"::", "0.0.0.0"}.contains(settings->inbound_address));

    if (!providerManagesRouting) {
        connect(card.addAction(tr("Choose apps to route")), &QPushButton::clicked, this, [this] {
            auto *dialog = new DialogPerAppProxy(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->exec();
        });
    } else {
        card.addNote(tr("Routing is managed by your provider while this subscription is "
                        "connected. Disconnect to change it."));
    }
}

namespace {
    // Готовые серверы. Все по DoH или DoT: обычный DNS по 53 порту виден
    // провайдеру целиком, и прятать трафик в туннель, оставляя запросы
    // открытыми, бессмысленно.
    struct DnsPreset { const char *label; const char *value; };
    constexpr DnsPreset kDnsPresets[] = {
        // Первым — тот, что стоит по умолчанию.
        {"Quad9 (9.9.9.9)",      "https://9.9.9.9/dns-query"},
        {"Cloudflare (1.1.1.1)", "https://1.1.1.1/dns-query"},
        {"Google (8.8.8.8)",     "https://8.8.8.8/dns-query"},
        {"AdGuard",              "https://dns.adguard-dns.com/dns-query"},
        {"Yandex",               "tls://77.88.8.8"},
    };

    // Заполняет список и выбирает текущее значение. Своё значение из
    // расширенного режима не теряем: добавляем его отдельным пунктом.
    void fillDns(FsntSelect *box, const QString &current) {
        for (const auto &[label, value] : kDnsPresets) {
            box->addItem(QString::fromUtf8(label), QString::fromUtf8(value));
        }
        for (int i = 0; i < box->count(); ++i) {
            if (box->itemData(i).toString() == current) {
                box->setCurrentIndex(i);
                return;
            }
        }
        box->addItem(QObject::tr("Custom: %1").arg(current), current);
        box->setCurrentIndex(box->count() - 1);
    }
}

void FsntSettingsDialog::buildDns(QVBoxLayout *column, QWidget *host) {
    Fsnt::SettingsCard card(column, host, tr("DNS"));
    const auto &settings = Configs::dataManager->settingsRepo;

    m_remoteDns = makeSelect(host);
    fillDns(m_remoteDns, settings->remote_dns);
    card.addControl(tr("Through the VPN"), m_remoteDns);

    m_directDns = makeSelect(host);
    fillDns(m_directDns, settings->direct_dns);
    card.addControl(tr("Direct traffic"), m_directDns);

    card.addNote(tr("The first resolves names for tunnelled traffic, the second for "
                    "everything that goes direct."));
}

void FsntSettingsDialog::buildSubscriptions(QVBoxLayout *column, QWidget *host) {
    Fsnt::SettingsCard card(column, host, tr("Subscriptions"));
    const auto &settings = Configs::dataManager->settingsRepo;

    m_subAutoUpdate = makeSelect(host);
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
    card.addControl(tr("Auto update"), m_subAutoUpdate);

    connect(card.addAction(tr("Update all now")), &QPushButton::clicked, this,
            [] { Subscription::updater()->RefreshAll(); });

    // Закреплённую подписку удалить нельзя — не показываем и кнопку, иначе
    // пользователь жмёт её и получает отказ без объяснений.
    if (Subscription::PolicyBlocksDeletion(settings->current_group)) {
        card.addNote(tr("This subscription is pinned by your provider and cannot be removed "
                        "while it is connected."));
    } else {
        // Действие расширенного режима уже умеет всё: не даёт удалить последнюю
        // группу, уважает pin провайдера и останавливает работающий профиль.
        connect(card.addAction(tr("Remove the current subscription")), &QPushButton::clicked, this,
                [this] { triggerMainWindowAction("actionDelete_Group"); });
    }
}

void FsntSettingsDialog::buildApplication(QVBoxLayout *column, QWidget *host) {
    Fsnt::SettingsCard card(column, host, tr("Application"));
    const auto &settings = Configs::dataManager->settingsRepo;

    m_language = makeSelect(host);
    // Индексы совпадают со switch в main.cpp: 0 системный, 1 English, 2 中文, 3 فارسی, 4 русский.
    m_language->addItem(tr("System"), 0);
    m_language->addItem("English", 1);
    m_language->addItem("简体中文", 2);
    m_language->addItem("فارسی", 3);
    m_language->addItem("Русский", 4);
    m_language->setCurrentIndex(qBound(0, settings->language, 4));
    card.addControl(tr("Language"), m_language);

    m_theme = makeSelect(host);
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
    card.addControl(tr("Theme"), m_theme);

    m_autoRun = card.addToggle(tr("Launch at login"), AutoRun_IsEnabled());
    m_startMinimal = card.addToggle(tr("Start minimized to tray"), settings->start_minimal);
    card.addNote(tr("Language changes apply after restarting the application."));
}

void FsntSettingsDialog::save() {
    auto &settings = Configs::dataManager->settingsRepo;

    settings->simple_transport = m_transport->currentData().toInt();
    settings->language = m_language->currentData().toInt();
    settings->start_minimal = m_startMinimal->isChecked();
    settings->remember_enable = m_autoConnect->isChecked();
    settings->inbound_address = m_allowLan->isChecked() ? "::" : "127.0.0.1";
    settings->remote_dns = m_remoteDns->currentData().toString();
    settings->direct_dns = m_directDns->currentData().toString();

    // m_route отсутствует, когда маршрутизацией управляет провайдер.
    if (m_route != nullptr && m_route->currentIndex() >= 0) {
        settings->current_route_id = m_route->currentData().toInt();
    }

    // Знак интервала несёт свой смысл в расширенном режиме, поэтому меняем
    // только величину и сохраняем прежний знак.
    const int minutes = m_subAutoUpdate->currentData().toInt();
    settings->sub_auto_update = settings->sub_auto_update < 0 ? -minutes : minutes;

    // Пишем только при настоящей смене: значение в базе хранится в нижнем
    // регистре, а у пунктов списка он смешанный, и запись «как есть» меняла бы
    // регистр при каждом сохранении без всякой причины.
    const QString theme = m_theme->currentData().toString();
    const bool themeChanged = theme.compare(settings->theme, Qt::CaseInsensitive) != 0;
    if (themeChanged) settings->theme = theme;

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
