#include <algorithm>
#include <iterator>

#include "include/ui/fsnt/FsntSettingsDialog.h"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>
#include <QTimer>

#include "include/engine/EngineHost.h"
#include "include/engine/services/AssetUpdates.h"
#include "include/engine/connection/ConnectionManager.h"
#include "include/ui/fsnt/BusyButton.h"
#include <QScrollArea>
#include <QVBoxLayout>

#include "NkrVersion.h"

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/configs/sub/ProviderPolicy.hpp"
#include "include/database/GroupsRepo.h"
#include "include/configs/generate.h"
#include "include/configs/DnsCatalog.hpp"
#include "include/configs/RoutePresets.hpp"
#include "include/ui/fsnt/Transport.hpp"
#include "include/database/RoutesRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/global/Configs.hpp"
#include "include/sys/AutoRun.hpp"
#ifdef Q_OS_MACOS
#include "include/sys/macos/CorePrivilege.h"
#include "include/sys/macos/MacOS.h"
#endif
#include "include/ui/fsnt/FsntControls.h"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/ui/fsnt/PingProbe.hpp"
#include "include/ui/fsnt/SettingsCard.h"
#include "include/ui/mainwindow.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/setting/dialog_per_app_proxy.h"

namespace {
    // Интервалы автообновления подписок в минутах, как их хранит sub_auto_update.
    constexpr int kAutoUpdateMinutes[] = {0, 30, 60, 120, 360, 720, 1440};

    // Сколько окно ждёт конца перезапуска, прежде чем сказать, что ответа не
    // было. С запасом: на холодном старте ядру случается подниматься долго.
    constexpr int kRestartGuardMs = 90000;

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
    buildNotifications(column, host);
    column->addStretch();

    scroll->setWidget(host);
    layout->addWidget(scroll, 1);

    auto *cancel = new QPushButton(tr("Cancel"), this);
    cancel->setObjectName("fsntGhost");
    cancel->setCursor(Qt::PointingHandCursor);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    m_saveStatus = new QLabel(this);
    m_saveStatus->setWordWrap(true);
    layout->addWidget(m_saveStatus);
    m_restartGuard = new QTimer(this);
    m_restartGuard->setSingleShot(true);
    // Срок задаём здесь, иначе вызовы start() без аргумента живут на интервале,
    // который им случайно оставил предыдущий start(ms).
    m_restartGuard->setInterval(kRestartGuardMs);
    connect(m_restartGuard, &QTimer::timeout, this, [this] {
        finishRestart(tr("Settings were saved, but the connection restart has not finished. Check the connection before trying again."));
    });

    auto *ok = m_save = new QPushButton(tr("Save"), this);
    ok->setObjectName("fsntPrimary");
    ok->setCursor(Qt::PointingHandCursor);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, [this] {
        if (!m_saving) save();
    });

    auto *buttons = new QHBoxLayout;
    // Вертушка стоит слева от распорки: появись она рядом с кнопками, «Отмена»
    // и «Сохранить» прыгнули бы вбок прямо под курсором.
    m_saveBusy = new BusyButton(Fsnt::Glyph::Refresh, this);
    m_saveBusy->hide();
    buttons->addWidget(m_saveBusy);
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
    // Пункт остаётся на месте и когда им распоряжается провайдер: раньше он
    // просто исчезал при подключении и возвращался после отключения, и человек
    // искал пропавшую кнопку, вместо того чтобы прочитать объяснение.
    const bool providerManagesRouting = Subscription::PolicyHidesSettings();

    {
        // Сначала готовые схемы, потом всё, что пользователь собрал сам в
        // расширенном режиме. Данные пункта: ключ пресета либо id профиля,
        // поэтому храним оба вида в QVariant и различаем по типу.
        m_route = makeSelect(host);
        const auto presets = Configs::RoutePresets();
        for (const auto &preset : presets) m_route->addItem(preset.title, preset.key);

        const auto current = Configs::dataManager->routesRepo->GetRouteProfile(
            settings->current_route_id);
        QString currentKey = current ? Configs::RoutePresetKeyOf(current->name) : QString();
        if (currentKey.isEmpty() && Configs::IsUntouchedDefaultProfile(settings->current_route_id)) {
            currentKey = presets.first().key;
        }

        for (const auto &profile : Configs::dataManager->routesRepo->GetAllRouteProfiles()) {
            if (!profile || !Configs::RoutePresetKeyOf(profile->name).isEmpty()) continue;
            // Стоковый Default в списке лишний: его роль занял первый пресет.
            if (Configs::IsUntouchedDefaultProfile(profile->id)) continue;
            m_route->addItem(tr("Custom: %1").arg(profile->name), profile->id);
        }

        if (!currentKey.isEmpty()) {
            m_route->setCurrentIndex(static_cast<int>(std::distance(
                presets.begin(),
                std::find_if(presets.begin(), presets.end(),
                             [&](const Configs::RoutePreset &p) { return p.key == currentKey; }))));
        } else {
            for (int i = 0; i < m_route->count(); ++i) {
                if (m_route->itemData(i).typeId() == QMetaType::Int
                    && m_route->itemData(i).toInt() == settings->current_route_id) {
                    m_route->setCurrentIndex(i);
                    break;
                }
            }
        }
        card.addControl(tr("Routing"), m_route);
        m_routeNote = card.addNote({});
        connect(m_route, &QComboBox::currentIndexChanged, this,
                &FsntSettingsDialog::updateRouteNote);
        updateRouteNote();
        m_route->setEnabled(!providerManagesRouting);
    }

    m_autoConnect = card.addToggle(tr("Reconnect on start"), settings->remember_enable);
    m_allowLan = card.addToggle(tr("Allow local network access"),
                                QStringList{"::", "0.0.0.0"}.contains(settings->inbound_address));

    auto *chooseApps = card.addAction(tr("Choose apps to route"));
    chooseApps->setEnabled(!providerManagesRouting);
    if (providerManagesRouting) {
        card.addNote(tr("Routing is managed by your provider while this subscription is "
                        "connected. Disconnect to change it."));
    } else {
        connect(chooseApps, &QPushButton::clicked, this, [this] {
            auto *dialog = new DialogPerAppProxy(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->exec();
        });
    }

#ifdef Q_OS_MACOS
    // Снять постоянное разрешение надо там же, где его дали. Строка есть
    // только когда служба стоит: иначе снимать нечего и спрашивать не о чем.
    if (Sys::MacCorePrivilege::Installed()) {
        auto *dropPrivilege = card.addAction(tr("Remove the permanent tunnel permission"));
        auto *dropNote = card.addNote(tr("A system service keeps the core allowed to open the tunnel, so an "
                                         "update no longer asks for your password. Remove it and the password "
                                         "is asked again after every update."));
        connect(dropPrivilege, &QPushButton::clicked, this, [this, dropPrivilege, dropNote] {
            if (!Fsnt::Confirm(this, tr("Tunnel permission"),
                               tr("Remove the service that keeps the tunnel permission? Your password will be "
                                  "asked again after every update."),
                               tr("Remove"))) {
                return;
            }
            int code = 0;
            if (Sys::MacCorePrivilege::Remove(Configs::FindCoreRealPath(),
                                              tr("FSNT Client needs your password to remove the tunnel permission."),
                                              &code)) {
                dropPrivilege->hide();
                dropNote->hide();
                return;
            }
            if (code == kMacAuthCancelled) return;
            MessageBoxWarning(tr("Tunnel permission"),
                              tr("The service could not be removed. See the log for details."));
        });
    }
#endif
}

void FsntSettingsDialog::updateRouteNote() {
    if (m_route == nullptr || m_routeNote == nullptr) return;
    const QVariant chosen = m_route->currentData();
    if (chosen.typeId() != QMetaType::QString) {
        m_routeNote->setText(tr("A profile you built yourself in advanced mode."));
        return;
    }
    for (const auto &preset : Configs::RoutePresets()) {
        if (preset.key == chosen.toString()) {
            m_routeNote->setText(preset.description);
            return;
        }
    }
    m_routeNote->clear();
}

namespace {
    // Как показать транспорт человеку. Схему в чистом виде показывать незачем:
    // «DoT» понятнее, чем «tls://», а разница между ними — ровно та, из-за
    // которой у людей и отваливается резолв.
    QString dnsTransportLabel(const QString &address) {
        // Разбором распоряжается сборщик конфига: именно он решает, что уедет в
        // ядро, и подсказка обязана говорить о том же адресе, что и он.
        Configs::DnsAddress parsed;
        if (!Configs::ParseDnsAddress(address, &parsed)) return {};
        if (parsed.type == QLatin1String("local")) return QObject::tr("System resolver");
        if (parsed.type == QLatin1String("tls")) return QStringLiteral("DoT");
        if (parsed.type == QLatin1String("https") || parsed.type == QLatin1String("h3")) return QStringLiteral("DoH");
        return parsed.type.toUpper();
    }

    // Заполняет список из каталога и своих адресов. Текущее значение не теряем,
    // даже если оно пришло из расширенного режима или от провайдера.
    void fillDns(FsntSelect *box, const QString &current, bool direct) {
        const auto add = [box](const QString &label, const QString &value) {
            for (int i = 0; i < box->count(); ++i) {
                if (box->itemData(i).toString() == value) return;
            }
            box->addItem(label, value);
        };

        for (const auto &entry : Configs::DnsCatalog()) {
            if (direct && !entry.forDirect) continue;
            if (!direct && !entry.forRemote) continue;
            for (const auto &address : entry.addresses) {
                if (entry.isSystem()) {
                    add(QObject::tr("System resolver"), address);
                    continue;
                }
                add(QStringLiteral("%1 — %2").arg(entry.title, dnsTransportLabel(address)), address);
            }
        }
        for (const auto &own : Configs::dataManager->settingsRepo->dns_custom_servers) {
            if (own.trimmed().isEmpty()) continue;
            add(QObject::tr("Mine: %1").arg(own.trimmed()), own.trimmed());
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
    fillDns(m_remoteDns, settings->remote_dns, false);
    card.addControl(tr("Through the VPN"), m_remoteDns);

    m_directDns = makeSelect(host);
    fillDns(m_directDns, settings->direct_dns, true);
    card.addControl(tr("Direct traffic"), m_directDns);

    m_dnsAuto = card.addToggle(tr("Switch automatically when it stops answering"),
                               settings->direct_dns_auto);

    auto *addOwn = card.addAction(tr("Add your own server"));
    connect(addOwn, &QPushButton::clicked, this, [this, host] {
        bool accepted = false;
        const auto entered = QInputDialog::getText(
            this, tr("Add your own server"),
            tr("Address, for example 1.1.1.1, tls://dns.google or "
               "https://dns.google/dns-query:"),
            QLineEdit::Normal, {}, &accepted).trimmed();
        if (!accepted || entered.isEmpty()) return;

        Configs::DnsAddress parsed;
        QString why;
        if (!Configs::ParseDnsAddress(entered, &parsed, &why)) {
            // Раньше адрес сохранялся любой, а ошибка всплывала много позже —
            // отсутствием интернета, без всякой связи с тем, что человек вписал.
            MessageBoxWarning(tr("Address not understood"), why);
            return;
        }

        auto &s = Configs::dataManager->settingsRepo;
        if (!s->dns_custom_servers.contains(entered)) {
            s->dns_custom_servers << entered;
            s->Save();
        }
        for (auto *box : {m_remoteDns, m_directDns}) {
            const auto keep = box->currentData().toString();
            box->clear();
            fillDns(box, keep, box == m_directDns);
        }
        for (int i = 0; i < m_directDns->count(); ++i) {
            if (m_directDns->itemData(i).toString() == entered) m_directDns->setCurrentIndex(i);
        }
        Q_UNUSED(host)
    });

    card.addNote(tr("The first resolves names for tunnelled traffic, the second for "
                    "everything that goes direct. The direct one has to work before the "
                    "tunnel is up, so a provider that blocks DoT or DoH leaves nothing to "
                    "connect with — that is what the automatic switch is for."));

    if (settings->direct_dns_auto && !settings->direct_dns_effective.isEmpty()
        && settings->direct_dns_effective != settings->direct_dns) {
        card.addNote(tr("Last connection actually used %1.").arg(settings->direct_dns_effective));
    }

    m_blockPrivateRelay = card.addToggle(tr("Turn off iCloud Private Relay while connected"),
                                         settings->block_private_relay);
    card.addNote(tr("Apple hides the IP address of Safari behind its own relay, and it does not "
                    "step aside for this client the way it does for others, so pages see Apple's "
                    "address instead of the country you picked. While connected, the relay is "
                    "switched off for the network you are on — the same switch as Limit IP Address "
                    "Tracking in System Settings — and put back when you disconnect. Apple devices "
                    "on the same network are affected too."));
    card.addNote(tr("Off by default: Apple does not offer the relay in every country, and where it "
                    "is not offered there is nothing to switch off, while the system still tells you "
                    "that your own network is incompatible with Private Relay."));
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
    int chosen = -1;
    for (int index = 0; index < static_cast<int>(std::size(kAutoUpdateMinutes)); ++index) {
        if (kAutoUpdateMinutes[index] == stored) {
            chosen = index;
            break;
        }
    }
    // Значение вне списка ставит политика провайдера. Раньше оно не находилось,
    // список оставался на «Off», и «Сохранить» молча выключало автообновление
    // подписок — человеку, пришедшему сюда совсем за другим.
    if (chosen < 0 && stored > 0) {
        m_subAutoUpdate->addItem(tr("Every %n minute(s)", nullptr, stored), stored);
        chosen = m_subAutoUpdate->count() - 1;
    }
    if (chosen >= 0) m_subAutoUpdate->setCurrentIndex(chosen);
    card.addControl(tr("Auto update"), m_subAutoUpdate);

    // Провайдеры считают устройства по этому идентификатору. Без него панель
    // не понимает, кто пришёл, и вместо серверов отдаёт заглушку — человек
    // видит подписку без единого сервера и не понимает, почему.
    m_sendHwid = card.addToggle(tr("Send device id"), settings->sub_send_hwid);
    card.addNote(tr("Providers count devices by it. With it off a subscription may come back "
                    "empty, and the reason will not be obvious."));

    auto *update = new BusyButton(Fsnt::Glyph::Refresh, host);
    update->setAccessibleName(tr("Update all now"));
    card.addControl(tr("Update all now"), update);
    auto *status = card.addNote({});
    connect(update, &QAbstractButton::clicked, this, [this, update, status] {
        QList<int> groups;
        for (const int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
            const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
            if (group && !group->url.isEmpty() && !group->archive) groups.append(gid);
        }
        if (groups.isEmpty()) {
            status->setText(tr("No subscriptions to update."));
            return;
        }
        update->setBusy(true);
        status->setText(tr("Updating subscriptions…"));
        struct Progress { int remaining; int failed = 0; QStringList errors; };
        auto progress = std::make_shared<Progress>();
        progress->remaining = groups.size();
        const QPointer<FsntSettingsDialog> guard(this);
        // Результат приходит с рабочего потока. Проверяем диалог уже в UI,
        // чтобы его закрытие не гонялось с доставкой ответа к виджетам.
        for (const int gid : groups) {
            Subscription::updater()->RefreshWithOutcome(gid, Subscription::GroupUpdater::Surface::Log,
                [guard, update, status, progress](const Subscription::UpdateOutcome &outcome) {
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, update, status, progress, ok = outcome.ok(), error = outcome.error] {
                        if (!guard) return;
                        if (!ok) {
                            ++progress->failed;
                            if (!error.isEmpty()) progress->errors.append(error);
                        }
                        if (--progress->remaining != 0) return;
                        update->setBusy(false);
                        status->setText(progress->failed == 0
                            ? tr("All subscriptions updated successfully.")
                            : tr("Subscription update finished. Failed: %1.").arg(progress->failed)
                                + (progress->errors.isEmpty() ? QString() : QStringLiteral("\n") + progress->errors.join(QStringLiteral("\n"))));
                    }, Qt::QueuedConnection);
                });
        }
    });

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

    m_pingKind = makeSelect(host);
    for (const auto kind : {Fsnt::PingKind::RequestGet, Fsnt::PingKind::RequestHead,
                            Fsnt::PingKind::Handshake, Fsnt::PingKind::Tcp, Fsnt::PingKind::Icmp}) {
        m_pingKind->addItem(Fsnt::PingKindTitle(kind), Fsnt::PingKindToSetting(kind));
    }
    if (const int index = m_pingKind->findData(settings->ping_kind); index >= 0) {
        m_pingKind->setCurrentIndex(index);
    }
    card.addControl(tr("Latency check"), m_pingKind);

    // Адрес нужен только URL-тесту: у остальных способов дорога идёт до самого
    // сервера, и поле стояло бы там просто так.
    m_pingUrl = new QLineEdit(host);
    m_pingUrl->setObjectName(QStringLiteral("fsntInput"));
    m_pingUrl->setText(settings->test_latency_url);
    m_pingUrl->setPlaceholderText(QStringLiteral("https://www.gstatic.com/generate_204"));
    auto *urlRow = card.addControl(tr("URL test address"), m_pingUrl);

    auto *pingNote = card.addNote(Fsnt::PingKindHint(Fsnt::PingKindFromSetting(settings->ping_kind)));
    const auto syncPingRows = [this, pingNote, urlRow] {
        const auto kind = Fsnt::PingKindFromSetting(m_pingKind->currentData().toInt());
        pingNote->setText(Fsnt::PingKindHint(kind));
        const bool needsUrl = kind == Fsnt::PingKind::RequestGet || kind == Fsnt::PingKind::RequestHead;
        if (urlRow != nullptr) urlRow->setVisible(needsUrl);
    };
    connect(m_pingKind, &QComboBox::currentIndexChanged, this, syncPingRows);
    syncPingRows();

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
    m_autoUpdate = card.addToggle(tr("Install updates by itself"), settings->auto_update);
    card.addNote(tr("Language changes apply after restarting the application."));
}

void FsntSettingsDialog::buildNotifications(QVBoxLayout *column, QWidget *host) {
    Fsnt::SettingsCard card(column, host, tr("Notifications"));
    const auto &settings = Configs::dataManager->settingsRepo;

    m_notifyUpdate = card.addToggle(tr("New version of the client"), settings->notify_update_system);
    m_notifySubscription = card.addToggle(tr("Subscription updated"), settings->notify_subscription);
    m_notifyConnection = card.addToggle(tr("VPN turned on and off"), settings->notify_connection);
    card.addNote(tr("About the subscription and the tunnel we only tell you when the client window "
                    "is not in front of you: while you are looking at it, you can see everything anyway."));
}

void FsntSettingsDialog::save() {
    m_saveStatus->clear();
    auto &settings = Configs::dataManager->settingsRepo;

    const int transport = m_transport->currentData().toInt();
    bool modeChanged = transport != settings->simple_transport;
    settings->simple_transport = transport;

    settings->language = m_language->currentData().toInt();
    settings->start_minimal = m_startMinimal->isChecked();
    if (m_blockPrivateRelay != nullptr && m_blockPrivateRelay->isChecked() != settings->block_private_relay) {
        settings->block_private_relay = m_blockPrivateRelay->isChecked();
        // Правило живёт в конфиге ядра, а его собирают при запуске профиля.
        modeChanged = true;
    }
    if (m_autoUpdate != nullptr) settings->auto_update = m_autoUpdate->isChecked();
    if (m_notifyUpdate != nullptr) settings->notify_update_system = m_notifyUpdate->isChecked();
    if (m_notifySubscription != nullptr) settings->notify_subscription = m_notifySubscription->isChecked();
    if (m_notifyConnection != nullptr) settings->notify_connection = m_notifyConnection->isChecked();
    settings->remember_enable = m_autoConnect->isChecked();
    settings->inbound_address = m_allowLan->isChecked() ? "::" : "127.0.0.1";
    settings->remote_dns = m_remoteDns->currentData().toString();
    settings->direct_dns = m_directDns->currentData().toString();
    settings->direct_dns_auto = m_dnsAuto->isChecked();
    // Выбор руками отменяет прошлый подбор: иначе клиент продолжит ходить на
    // сервер, который человек только что заменил.
    settings->direct_dns_effective.clear();

    // Заблокирован — значит маршрутизацией распоряжается провайдер: показываем,
    // что выбрано, но ничего не переписываем.
    if (m_route != nullptr && m_route->isEnabled() && m_route->currentIndex() >= 0) {
        const QVariant chosen = m_route->currentData();
        // Профиль пресета создаём только когда его действительно выбрали: иначе
        // база обрастала бы схемами, которых пользователь не просил.
        settings->current_route_id = chosen.typeId() == QMetaType::QString
                                         ? Configs::EnsureRoutePreset(chosen.toString())
                                         : chosen.toInt();
    }

    // Знак интервала несёт свой смысл в расширенном режиме, поэтому меняем
    // только величину и сохраняем прежний знак.
    const int minutes = m_subAutoUpdate->currentData().toInt();
    settings->sub_auto_update = settings->sub_auto_update < 0 ? -minutes : minutes;

    if (m_pingKind != nullptr) settings->ping_kind = m_pingKind->currentData().toInt();
    if (m_sendHwid != nullptr) settings->sub_send_hwid = m_sendHwid->isChecked();
    if (m_pingUrl != nullptr) {
        // Пустое поле означает «как было задумано», а не «проверять нечем».
        const QString url = m_pingUrl->text().trimmed();
        settings->test_latency_url = url.isEmpty() ? QStringLiteral("https://www.gstatic.com/generate_204") : url;
    }

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
        // Причину функция отдаёт вторым параметром. Её выбрасывали, а на её
        // место вставала заготовка про окно повышения прав в Windows — её
        // читал и человек на маке. Переключатель возвращаем в настоящее
        // положение: иначе он остаётся включённым на настройке, которая не
        // применилась.
        if (QString error; !AutoRun_SetEnabled(m_autoRun->isChecked(), &error)) {
            m_autoRun->setChecked(AutoRun_IsEnabled());
            MessageBoxWarning(software_name,
                              tr("Could not change the autostart entry:") + "\n" + error);
        }
    }

    // Транспорт задаётся режимами ядра, а не одной настройкой. Раньше здесь
    // сохранялось только число, режим применялся лишь при подключении, и на
    // работающем профиле смена транспорта не делала ничего — перезапуск просто
    // гасил соединение.
    if (modeChanged) {
        const int running = settings->started_id;
        // Ранний возврат отсюда уносил с собой UpdateSettings ниже, и смена
        // адреса приёма или маршрута оставалась в настройках, не доезжая до
        // конфига ядра. Поэтому отказ только пишет строку и идёт дальше.
        if (!Fsnt::ApplyTransportMode()) {
            m_saveStatus->setText(tr("Settings were saved, but the connection mode could not be applied."));
        } else if (running >= 0) {
            auto *mw = GetMainWindow();
            auto *engine = Engine::host();
            if (mw == nullptr || engine == nullptr) {
                m_saveStatus->setText(tr("Settings were saved, but the connection could not be restarted."));
            } else {
                m_saving = true;
                m_save->setEnabled(false);
                m_saveBusy->show();
                m_saveBusy->setBusy(true);
                m_saveStatus->setText(tr("Settings saved. Restarting the connection…"));
                // Подписываемся до запуска: отказ может прийти синхронно.
                m_restartConnection = connect(engine->connection(), &Engine::ConnectionManager::startFinished, this,
                    [this, running](quint64, int profileId, Engine::StartOutcome outcome, const QString &error) {
                        if (profileId != running) return;
                        // Режим мог уже заказать свой перезапуск. Его вытеснение
                        // нашим запросом не является результатом новой попытки.
                        if (outcome == Engine::StartOutcome::Superseded) return;
                        if (outcome == Engine::StartOutcome::GeoAssetsMissing &&
                            (!Engine::AssetUpdates::GeoAssetsPresent() || Engine::AssetUpdates::GeoUpdateRunning())) {
                            m_saveStatus->setText(tr("Downloading the routing data…"));
                            m_restartGuard->start();
                            return;
                        }
                        finishRestart(outcome == Engine::StartOutcome::Started ? QString()
                            : (error.isEmpty() ? tr("Settings were saved, but the connection restart failed. Please reconnect.") : error));
                    });
                m_restartGuard->start();
                mw->profile_start(running);
            }
        }
    }

    // Смена адреса приёма и маршрута требует пересборки конфига ядра.
    if (MW_dialog_message) MW_dialog_message(MwMessage::UpdateSettings, {});
    if (!m_saving && m_saveStatus->text().isEmpty()) accept();
}

void FsntSettingsDialog::reject() {
    // Ждать перезапуска окно умеет, а запирать в себе человека — нет. Отказ он
    // всё равно увидит: кольцо подключения показывает исход само, а здесь после
    // «Сохранить» осталось одно ожидание.
    if (m_saving) {
        disconnect(m_restartConnection);
        m_restartGuard->stop();
        m_saving = false;
    }
    QDialog::reject();
}

void FsntSettingsDialog::finishRestart(const QString &error) {
    disconnect(m_restartConnection);
    m_restartGuard->stop();
    m_saving = false;
    m_save->setEnabled(true);
    m_saveBusy->setBusy(false);
    m_saveBusy->hide();
    if (error.isEmpty()) accept();
    else m_saveStatus->setText(error);
}
