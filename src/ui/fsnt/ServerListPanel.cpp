#include <memory>

#include "include/ui/fsnt/ServerListPanel.h"
#include <QApplication>
#include <QScopeGuard>
#include <QClipboard>
#include "include/ui/fsnt/SubscriptionSettings.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QAction>
#include <QMouseEvent>
#include <QTimer>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QScrollBar>
#include <QVBoxLayout>

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/engine/services/AssetUpdates.h"
#include "include/engine/services/GroupService.h"
#include "include/global/Configs.hpp"
#include "include/ui/fsnt/BusyButton.h"
#include "include/ui/fsnt/FsntControls.h"
#include "include/ui/fsnt/FsntPalette.hpp"
#include <QMenu>
#include "include/ui/fsnt/PingProbe.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/configs/sub/ProviderPolicy.hpp"
#include "include/ui/mainwindow.h"
#include "src/ui/fsnt/ServerItemDelegate.h"

#include <QCoreApplication>
#include <QPointer>

namespace {
    // Опросов подряд без единого ответа, после которых замер считается
    // несостоявшимся. Опрос идёт раз в две секунды. Нужно только там, где о
    // конце прогона никто не скажет, — иначе конец приходит ответом.
    constexpr int kIdlePollsBeforeGivingUp = 6;
}

ServerListPanel::ServerListPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *groupRow = new QHBoxLayout;
    groupRow->setSpacing(6);

    m_groups = new FsntSelect(this);
    groupRow->addWidget(m_groups, 1);

    m_addSub = new FsntIconButton(Fsnt::Glyph::Plus, this);
    m_addSub->setFixedSize(36, 36);
    m_addSub->setToolTip(tr("Add subscription"));
    connect(m_addSub, &FsntIconButton::clicked, this, &ServerListPanel::addSubscriptionRequested);
    groupRow->addWidget(m_addSub);

    m_updateSub = new BusyButton(Fsnt::Glyph::Refresh, this);
    m_updateSub->setFixedSize(36, 36);
    m_updateSub->setToolTip(tr("Update subscription"));
    connect(m_updateSub, &BusyButton::clicked, this, &ServerListPanel::updateSubscription);
    groupRow->addWidget(m_updateSub);

    // Что можно сделать с самой подпиской. Раньше за этим приходилось идти в
    // расширенный режим: человек с полутора десятками случайных дублей удалял
    // их по одному в инженерном окне групп.
    m_subMenu = new FsntIconButton(Fsnt::Glyph::More, this);
    m_subMenu->setFixedSize(36, 36);
    m_subMenu->setToolTip(tr("Subscription"));
    connect(m_subMenu, &FsntIconButton::clicked, this, &ServerListPanel::showSubscriptionMenu);
    groupRow->addWidget(m_subMenu);

    layout->addLayout(groupRow);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(6);

    m_search = new QLineEdit(this);
    m_search->setObjectName("fsntSearch");
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(Fsnt::GlyphIcon(Fsnt::Glyph::Search, 15, Fsnt::CurrentPalette().textMuted),
                        QLineEdit::LeadingPosition);
    // Крестик очистки Qt берёт из стиля платформы; подменяем на свой значок.
    if (auto *clear = m_search->findChild<QAction *>(QStringLiteral("_q_qlineeditclearaction"))) {
        clear->setIcon(Fsnt::GlyphIcon(Fsnt::Glyph::Close, 13, Fsnt::CurrentPalette().textMuted));
    }
    searchRow->addWidget(m_search, 1);

    m_ping = new BusyButton(Fsnt::Glyph::Bolt, this);
    m_ping->setFixedSize(36, 36);
    m_ping->setToolTip(tr("Measure latency"));
    // Этой же кнопкой замер и обрывают, значит во время работы она должна
    // оставаться нажимаемой.
    m_ping->setDisableWhileBusy(false);
    searchRow->addWidget(m_ping);

    layout->addLayout(searchRow);

    // Полоса-подложка: без неё две пилюли висят в пустоте и не читаются как переключатель.
    auto *tabStrip = new QWidget(this);
    tabStrip->setObjectName("fsntTabStrip");
    auto *tabs = new QHBoxLayout(tabStrip);
    tabs->setContentsMargins(3, 3, 3, 3);
    tabs->setSpacing(3);
    m_tabAll = new QPushButton(tr("All"), tabStrip);
    m_tabFav = new QPushButton(tr("Favorites"), tabStrip);
    for (auto *tab : {m_tabAll, m_tabFav}) {
        tab->setObjectName("fsntTab");
        tab->setCheckable(true);
        tab->setCursor(Qt::PointingHandCursor);
        tabs->addWidget(tab, 1);
    }
    m_tabAll->setChecked(true);
    layout->addWidget(tabStrip);

    m_list = new QListWidget(this);
    m_list->setObjectName("fsntServerList");
    m_list->setItemDelegate(new ServerItemDelegate(m_list));
    m_list->setMouseTracking(true);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_list, 1);

    m_empty = new QWidget(this);
    auto *emptyLayout = new QVBoxLayout(m_empty);
    emptyLayout->setContentsMargins(24, 24, 24, 24);
    emptyLayout->setSpacing(12);
    emptyLayout->addStretch();

    m_emptyText = new QLabel(m_empty);
    m_emptyText->setObjectName("fsntPlaceholder");
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setWordWrap(true);
    emptyLayout->addWidget(m_emptyText);

    auto *emptyAdd = new QPushButton(tr("Add subscription"), m_empty);
    emptyAdd->setObjectName("fsntPrimary");
    emptyAdd->setCursor(Qt::PointingHandCursor);
    connect(emptyAdd, &QPushButton::clicked, this, &ServerListPanel::addSubscriptionRequested);

    auto *emptyRow = new QHBoxLayout;
    emptyRow->addStretch();
    emptyRow->addWidget(emptyAdd);
    emptyRow->addStretch();
    emptyLayout->addLayout(emptyRow);
    emptyLayout->addStretch();

    m_empty->hide();
    layout->addWidget(m_empty, 1);

    connect(m_search, &QLineEdit::textChanged, this, &ServerListPanel::applyFilter);
    connect(m_groups, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) return;
        const int gid = m_groups->itemData(index).toInt();
        Configs::dataManager->settingsRepo->current_group = gid;
        Configs::dataManager->settingsRepo->Save();
        reloadServers();
    });
    // Клик в правой зоне строки переключает избранное, не запуская сервер.
    m_list->viewport()->installEventFilter(this);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested, this, &ServerListPanel::showServerMenu);

    m_latencyPoll = new QTimer(this);
    m_latencyPoll->setInterval(2000);
    connect(m_latencyPoll, &QTimer::timeout, this, [this] {
        reloadServers();

        // Если не ответил вообще никто, замер, скорее всего, и не начался.
        // Гадание нужно только без ответа о конце прогона: с ним пустые
        // опросы означают лишь медленную сеть, и обрывать на них нельзя.
        if (!m_measureExactEnd) {
            m_measureIdlePolls = answeredCount() == 0 ? m_measureIdlePolls + 1 : 0;
            if (m_measureIdlePolls >= kIdlePollsBeforeGivingUp) {
                finishMeasurement();
                return;
            }
        }

        // Где конец приходит ответом, опрос только обновляет список. Решать
        // ему нельзя: такт в две секунды обгоняет настоящий конец на последних
        // миллисекундах, а finishMeasurement поднимает номер прогона — после
        // этого настоящий ответ отбрасывается, ядро остаётся с незакрытым
        // сеансом, и следующее нажатие упирается в инженерное окно «прошлый
        // тест ещё не вышел». Выход из затянувшегося замера даёт та же кнопка.
        if (m_measureExactEnd) return;

        // Счётчик остаётся верхней границей: до части серверов может не быть
        // связи вовсе, и сами по себе они никогда не «ответят».
        if (allMeasured() || --m_latencyPollsLeft <= 0) finishMeasurement();
    });

    // Точки «идёт замер» анимируются от часов, поэтому достаточно будить
    // перерисовку; хранить состояние в строках не нужно.
    m_measureRepaint = new QTimer(this);
    m_measureRepaint->setInterval(60);
    connect(m_measureRepaint, &QTimer::timeout, this, [this] {
        if (m_list != nullptr) m_list->viewport()->update();
    });

    connect(m_ping, &QPushButton::clicked, this, [this] {
        // Пока идёт замер, та же кнопка его и обрывает: иначе на подписке в
        // сотню серверов остаётся только ждать или убивать клиент.
        if (m_ping->isBusy()) stopMeasurement();
        else measureLatency();
    });
    connect(m_tabAll, &QPushButton::clicked, this, [this] { setShowFavouritesOnly(false); });
    connect(m_tabFav, &QPushButton::clicked, this, [this] { setShowFavouritesOnly(true); });
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item != nullptr) emit serverActivated(item->data(ProfileIdRole).toInt());
    });
    connect(m_list, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
        if (item == nullptr) return;
        const int id = item->data(ProfileIdRole).toInt();
        // Запоминаем именно выбор, а не запуск: remember_id управляет автостартом,
        // и писать туда по клику значило бы подключаться при следующем старте.
        Configs::dataManager->settingsRepo->simple_selected_server = item->text();
        Configs::dataManager->settingsRepo->Save();
        emit serverSelected(id);
    });

    reloadGroups();

    // subscription-ping-onopen-enabled: панель просит замерить пинги при открытии.
    // Один раз за запуск и с задержкой — ядру нужно подняться.
    const auto policy = Subscription::DeserializeProviderPolicy(
        Configs::dataManager->groupsRepo->CurrentGroup()
            ? Configs::dataManager->groupsRepo->CurrentGroup()->provider_policy_json
            : QString());
    if (policy.pingOnOpen.has_value() && policy.pingOnOpen.value()) {
        QTimer::singleShot(4000, this, &ServerListPanel::measureLatency);
    }
}



bool ServerListPanel::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_list->viewport() && event->type() == QEvent::MouseButtonRelease) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (auto *item = m_list->itemAt(mouse->pos())) {
            const QRect row = m_list->visualItemRect(item);
            if (mouse->pos().x() >= row.right() - ServerItemDelegate::kHeartZone) {
                toggleFavorite(item->text());
                item->setData(FavoriteRole, !item->data(FavoriteRole).toBool());
                applyFilter(m_search->text());
                m_list->viewport()->update();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

QSet<QString> ServerListPanel::favorites() {
    QSet<QString> names;
    const auto doc = QJsonDocument::fromJson(
        Configs::dataManager->settingsRepo->favorite_profiles.toUtf8());
    if (!doc.isArray()) return names;
    // Числа в массиве — наследие прежнего формата по id. Молча пропускаем:
    // после обновления подписки те id всё равно ни на что не указывают.
    for (const auto &value : doc.array()) {
        if (value.isString() && !value.toString().isEmpty()) names.insert(value.toString());
    }
    return names;
}

void ServerListPanel::toggleFavorite(const QString &serverName) {
    if (serverName.isEmpty()) return;
    auto names = favorites();
    if (names.contains(serverName)) names.remove(serverName);
    else names.insert(serverName);

    QJsonArray array;
    for (const QString &name : names) array.append(name);
    Configs::dataManager->settingsRepo->favorite_profiles =
        QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    Configs::dataManager->settingsRepo->Save();
}

void ServerListPanel::setShowFavouritesOnly(bool onlyFavourites) {
    m_favouritesOnly = onlyFavourites;
    m_tabAll->setChecked(!onlyFavourites);
    m_tabFav->setChecked(onlyFavourites);
    applyFilter(m_search->text());
}


bool ServerListPanel::deferUntilGeoAssets(const std::function<void()> &then) {
    if (Engine::AssetUpdates::GeoAssetsPresent()) return false;
    // Второе нажатие не заводит второго ожидающего: он уже стоит в очереди.
    if (m_waitingForGeo) return true;

    // Ожидание открываем до вызова. У EnsureGeoAssets есть синхронная ветка:
    // файлы могли лечь на диск между проверкой выше и этой строкой — их пишет
    // фоновая догрузка с запуска, — и тогда колбэк приходит прямо отсюда. Со
    // флагом, выставленным после возврата, он видел бы ожидание незакрытым,
    // выходил сразу, а кнопка оставалась занятой под надписью «загружаем»
    // навсегда.
    m_waitingForGeo = true;
    // Занятая кнопка здесь и есть ответ на нажатие: иначе человек видит, что
    // ничего не произошло, и жмёт её ещё раз.
    setPingBusy(true);

    QPointer<ServerListPanel> self(this);
    const bool joined = Engine::AssetUpdates::EnsureGeoAssets([self, then](const QString &error) {
        if (self.isNull() || !self->m_waitingForGeo) return;
        self->m_waitingForGeo = false;
        self->setPingBusy(false);
        if (!error.isEmpty()) {
            emit self->notice(tr("Could not download the routing data: %1").arg(error.trimmed()));
            return;
        }
        then();
    });
    if (!joined) {
        m_waitingForGeo = false;
        setPingBusy(false);
        emit notice(tr("The routing data is still downloading. Try again in a moment."));
        return true;
    }

    // Колбэк мог отработать синхронно и уже закрыть ожидание — тогда замер
    // идёт, и обещать загрузку нечего.
    if (m_waitingForGeo) emit notice(tr("Downloading the routing data, the test will start on its own…"));
    return true;
}

void ServerListPanel::measureLatency() {
    auto *mw = GetMainWindow();
    if (mw == nullptr) return;

    if (deferUntilGeoAssets([this] { measureLatency(); })) return;

    // testRunner приватный, но действие меню — именованный дочерний объект окна.
    // Дёргаем его, чтобы не править код, который правит upstream.
    auto *action = mw->findChild<QAction *>("actionUrl_Test_Group");
    if (action == nullptr && !MW_url_test_group) return;

    // Отметку ставим до запуска: строка считается ждущей, пока её latency_at
    // старше этого момента. Так видно, кто уже ответил, а кто ещё нет.
    m_measureStartedAt = QDateTime::currentSecsSinceEpoch();
    // Результаты приходят порциями: обновляем список, пока идёт замер.
    m_measureExactEnd = static_cast<bool>(MW_url_test_group);
    m_latencyPollsLeft = 30;
    m_measureIdlePolls = 0;
    setPingBusy(true);
    m_latencyPoll->start();
    m_measureRepaint->start();
    reloadServers();

    if (MW_url_test_group) {
        // Прогон, который не начался, кончается за миллисекунду — например
        // ядру не хватило гео-файлов и конфиг не собрался. Без этого ответа
        // панель узнавала об этом только по шести пустым опросам, то есть
        // двенадцать секунд рисовала думающие точки на замере, которого нет.
        const quint64 run = ++m_measureRun;
        QPointer<ServerListPanel> self(this);
        MW_url_test_group([self, run] {
            if (self.isNull() || self->m_measureRun != run) return;
            self->finishMeasurement();
        });
        return;
    }
    action->trigger();
}

// Занятость и подсказка меняются вместе: кнопка, которая во время работы
// обрывает замер, не должна обещать «замерить».
void ServerListPanel::setPingBusy(const bool busy) {
    m_ping->setBusy(busy);
    m_ping->setToolTip(busy ? tr("Stop the measurement") : tr("Measure latency"));
}

void ServerListPanel::stopMeasurement() {
    // Той же кнопкой человек отменяет и ожидание гео-файлов: сама загрузка
    // идёт дальше, но замера после неё уже не будет. Ядро тут ни при чём,
    // останавливать нечего.
    if (m_waitingForGeo) {
        m_waitingForGeo = false;
        setPingBusy(false);
        emit notice(tr("Measurement cancelled"));
        return;
    }

    if (auto *mw = GetMainWindow()) {
        if (auto *action = mw->findChild<QAction *>("menu_stop_testing")) action->trigger();
    }

    // Отпускать кнопку сразу нельзя: ядро держит сеанс до конца текущей партии,
    // каждая проба в ней ещё досиживает свой таймаут. Следующее нажатие по
    // освобождённой кнопке упиралось в окно «прошлый тест ещё не вышел» —
    // инженерный текст посреди простого окна. Закрывает замер его собственный
    // ответ о конце, он придёт.
    if (m_measureExactEnd && m_measureStartedAt != 0) {
        emit notice(tr("Stopping the measurement…"));
        return;
    }
    finishMeasurement();
    emit notice(tr("Measurement stopped"));
}

void ServerListPanel::probeOne(const int profileId, const int kind) {
    // Второй замер поверх идущего ядро всё равно не возьмёт — сеанс один, — а
    // ответы двух прогонов легли бы в одни и те же строки и закрывали бы друг
    // друга чужой сводкой.
    if (m_ping->isBusy()) {
        emit notice(tr("A measurement is already running"));
        return;
    }
    if (deferUntilGeoAssets([this, profileId, kind] { probeOne(profileId, kind); })) return;

    const auto probeKind = Fsnt::PingKindFromSetting(kind);
    m_probeTarget = profileId;
    // Отметку ставим как при общем замере, но ждёт по ней только цель.
    m_measureStartedAt = QDateTime::currentSecsSinceEpoch();
    // ProbeProfile зовёт колбэк на каждом пути выхода, так что конец известен
    // точно. Без этого флаг оставался от прошлого общего замера, а проверка
    // через ядро (RequestHead) освобождала кнопку, пока сеанс ещё занят.
    m_measureExactEnd = true;
    m_latencyPollsLeft = 15;
    m_measureIdlePolls = 0;
    setPingBusy(true);
    m_latencyPoll->start();
    m_measureRepaint->start();
    reloadServers();

    // Номер прогона — как у общего замера: ответ прошлой проверки не должен
    // закрывать следующую.
    const quint64 run = ++m_measureRun;
    QPointer<ServerListPanel> self(this);
    Fsnt::ProbeProfile(profileId, probeKind, [self, run](int ms, const QString &error) {
        if (self.isNull() || self->m_measureRun != run) return;
        self->finishMeasurement();
        // Своя причина у способа бывает подробнее общей («имя не разрешается»),
        // и тогда она перекрывает сообщение, которое уже поставил finishMeasurement.
        if (ms < 0 && !error.isEmpty()) emit self->notice(error);
        self->reloadServers();
    });
}

void ServerListPanel::showSubscriptionMenu() {
    const int gid = m_groups->currentData().toInt();
    const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
    if (group == nullptr) return;

    QMenu menu(this);
    auto *header = menu.addAction(group->name);
    header->setEnabled(false);
    menu.addSeparator();

    const bool isSubscription = !group->url.isEmpty();
    if (isSubscription) {
        connect(menu.addAction(tr("Update subscription")), &QAction::triggered, this,
                &ServerListPanel::updateSubscription);
        connect(menu.addAction(tr("Copy link")), &QAction::triggered, this, [this, gid] {
            const auto entity = Configs::dataManager->groupsRepo->GetGroup(gid);
            if (entity == nullptr) return;
            QApplication::clipboard()->setText(entity->url);
            emit notice(tr("Link copied"));
        });
        connect(menu.addAction(tr("Subscription settings")), &QAction::triggered, this,
                [this, gid] { Fsnt::EditSubscription(this, gid); });
        menu.addSeparator();
    }

    // Последнюю группу удалять нельзя: списку серверов нужно хоть что-то.
    auto *remove = menu.addAction(tr("Delete subscription"));
    remove->setEnabled(Configs::dataManager->groupsRepo->GetAllGroupIds().size() > 1);
    connect(remove, &QAction::triggered, this, [this, gid] { deleteSubscription(gid); });

    // Дубли и оптовое удаление: человеку, у которого их полтора десятка,
    // ходить по одной — наказание.
    const int duplicates = duplicateSubscriptions().size();
    if (duplicates > 0) {
        connect(menu.addAction(tr("Delete duplicates (%1)").arg(duplicates)), &QAction::triggered, this,
                [this] { deleteDuplicateSubscriptions(); });
    }
    if (subscriptionCount() > 1) {
        connect(menu.addAction(tr("Delete all subscriptions")), &QAction::triggered, this,
                [this] { deleteAllSubscriptions(); });
    }

    menu.exec(m_subMenu->mapToGlobal(QPoint(0, m_subMenu->height() + 4)));
}

int ServerListPanel::subscriptionCount() {
    int count = 0;
    for (const int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (group != nullptr && !group->url.isEmpty()) ++count;
    }
    return count;
}

QList<int> ServerListPanel::duplicateSubscriptions() {
    // Дубль — это вторая и следующие подписки с той же ссылкой. Оставляем ту,
    // в которой больше серверов: она и есть удачная попытка.
    QHash<QString, int> keep;
    QList<int> extra;
    for (const int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (group == nullptr || group->url.isEmpty()) continue;
        const QString key = QUrl(group->url.trimmed()).toString(QUrl::StripTrailingSlash);
        const auto it = keep.find(key);
        if (it == keep.end()) {
            keep.insert(key, gid);
            continue;
        }
        const auto kept = Configs::dataManager->groupsRepo->GetGroup(it.value());
        const int keptSize = kept != nullptr ? kept->Profiles().size() : 0;
        if (group->Profiles().size() > keptSize) {
            extra << it.value();
            it.value() = gid;
        } else {
            extra << gid;
        }
    }
    return extra;
}

void ServerListPanel::removeGroups(const QList<int> &ids) {
    auto &groupsRepo = Configs::dataManager->groupsRepo;
    const int startedId = Configs::dataManager->settingsRepo->started_id;
    const auto running = Configs::dataManager->profilesRepo->GetProfile(startedId);

    int removed = 0;
    int pinned = 0;
    int failed = 0;
    int keptLast = 0;
    {
        // Удаление идёт на UI-потоке и не быстрое: остановка туннеля ждёт ядро,
        // а удаление группы уносит все её профили одной транзакцией и заставляет
        // пересобрать таблицы. Курсор — единственное, что здесь можно показать,
        // не перекрывая подтверждение, которое человек только что закрыл.
        QApplication::setOverrideCursor(Qt::BusyCursor);
        const auto restoreCursor = qScopeGuard([] { QApplication::restoreOverrideCursor(); });

        for (const int gid : ids) {
            // Последнюю группу не трогаем: списку серверов нужно хоть что-то.
            if (groupsRepo->GetAllGroupIds().size() <= 1) {
                ++keptLast;
                continue;
            }
            if (Subscription::PolicyBlocksDeletion(gid)) {
                ++pinned;
                continue;
            }
            if (running != nullptr && running->gid == gid) {
                if (auto *mw = GetMainWindow(); mw != nullptr) mw->profile_stop(false, true, false);
            }
            // Через службу, а не напрямую в репозиторий: голый DeleteGroup
            // уносит строки и оставляет started_id и remember_id указывать на
            // удалённый профиль — автоподключение на следующем запуске бьётся
            // в пустоту, а панель рисует «подключено» без сервера. Служба их
            // сбрасывает и заодно снимает ссылки front и landing proxy у
            // соседних групп, иначе их серверы перестают собираться.
            //
            // По возврату, а не по факту вызова: сорвавшаяся транзакция
            // считалась удалением, и человек читал, что подписки больше нет.
            if (Engine::GroupService::Delete(gid)) ++failed;
            else ++removed;
        }

        if (removed > 0) {
            MW_dialog_message(MwMessage::GroupsChanged, {});
            reloadGroups();
        }
    }

    // Сначала то, что пошло не так: человеку важнее несделанное.
    if (failed > 0) {
        emit notice(tr("Deleted %n subscription(s), %1 could not be deleted", nullptr, removed).arg(failed));
        return;
    }
    if (pinned > 0) {
        emit notice(tr("Deleted %n subscription(s), %1 pinned by the provider left in place", nullptr, removed)
                        .arg(pinned));
        return;
    }
    if (keptLast > 0) {
        emit notice(tr("Deleted %n subscription(s), the last one is kept — the list needs something",
                       nullptr, removed));
        return;
    }
    emit notice(tr("Deleted %n subscription(s)", nullptr, removed));
}

void ServerListPanel::deleteDuplicateSubscriptions() {
    const auto extra = duplicateSubscriptions();
    if (extra.isEmpty()) return;
    if (!Fsnt::Confirm(this, tr("Delete duplicates"),
                       tr("%n subscription(s) repeat a link that is already added. Delete the extra ones?",
                          nullptr, extra.size()),
                       tr("Delete"))) {
        return;
    }
    removeGroups(extra);
}

void ServerListPanel::deleteAllSubscriptions() {
    QList<int> ids;
    for (const int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (group != nullptr && !group->url.isEmpty()) ids << gid;
    }
    if (ids.isEmpty()) return;
    if (!Fsnt::Confirm(this, tr("Delete all subscriptions"),
                       tr("Delete %n subscription(s) and all their servers? Links you will have to add "
                          "again.",
                          nullptr, ids.size()),
                       tr("Delete all"))) {
        return;
    }
    removeGroups(ids);
}

void ServerListPanel::deleteSubscription(int gid) {
    const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
    if (group == nullptr) return;

    if (Subscription::PolicyBlocksDeletion(gid)) {
        Fsnt::Notice(this, tr("Delete subscription"),
                     tr("The provider pinned this subscription: it cannot be removed while its server "
                        "is running."));
        return;
    }
    if (!Fsnt::Confirm(this, tr("Delete subscription"),
                       tr("Delete «%1» along with its servers?").arg(group->name), tr("Delete"))) {
        return;
    }

    if (auto *mw = GetMainWindow(); mw != nullptr) {
        // Работающий сервер этой подписки останавливаем: иначе туннель живёт
        // на профиле, которого больше нет.
        if (const auto running = Configs::dataManager->profilesRepo->GetProfile(
                Configs::dataManager->settingsRepo->started_id);
            running != nullptr && running->gid == gid) {
            mw->profile_stop(false, true, false);
        }
    }
    // Через службу: она сбрасывает started_id на удалённый профиль и снимает
    // ссылки front и landing proxy у соседних групп. Голый DeleteGroup этого
    // не делает, и автоподключение после удаления подписки било в пустоту.
    //
    // По возврату, а не по факту вызова: сорвавшаяся транзакция считалась
    // удалением, и человек читал, что подписки больше нет.
    if (Engine::GroupService::Delete(gid)) {
        emit notice(tr("%1 could not be deleted").arg(group->name));
        return;
    }
    MW_dialog_message(MwMessage::GroupsChanged, {});
    reloadGroups();
    emit notice(tr("Subscription deleted"));
}

void ServerListPanel::showServerMenu(const QPoint &where) {
    auto *item = m_list->itemAt(where);
    if (item == nullptr) return;
    const int profileId = item->data(ProfileIdRole).toInt();
    if (profileId < 0) return;

    QMenu menu(this);
    auto *header = menu.addAction(item->text().split('\n').first());
    header->setEnabled(false);
    menu.addSeparator();

    const auto defaultKind = Fsnt::PingKindFromSetting(Configs::dataManager->settingsRepo->ping_kind);
    auto *check = menu.addAction(tr("Check (%1)").arg(Fsnt::PingKindTitle(defaultKind)));
    connect(check, &QAction::triggered, this,
            [this, profileId, defaultKind] { probeOne(profileId, Fsnt::PingKindToSetting(defaultKind)); });

    auto *other = menu.addMenu(tr("Check another way"));
    for (const auto kind : {Fsnt::PingKind::Icmp, Fsnt::PingKind::Tcp, Fsnt::PingKind::Handshake,
                            Fsnt::PingKind::RequestGet, Fsnt::PingKind::RequestHead}) {
        auto *action = other->addAction(Fsnt::PingKindTitle(kind));
        action->setToolTip(Fsnt::PingKindHint(kind));
        connect(action, &QAction::triggered, this,
                [this, profileId, kind] { probeOne(profileId, Fsnt::PingKindToSetting(kind)); });
    }
    other->setToolTipsVisible(true);

    menu.exec(m_list->viewport()->mapToGlobal(where));
}

void ServerListPanel::reloadGroups() {
    // Состав профилей мог смениться: при sub_clear обновление подписки
    // пересоздаёт их целиком, и старые id указывают уже не туда.
    m_subtitleCache.clear();

    const QSignalBlocker blocker(m_groups);
    m_groups->clear();

    int current = Configs::dataManager->settingsRepo->current_group;

    // Если текущая группа пуста, а рядом есть подписка с серверами — открываем её:
    // пустой список при импортированной подписке выглядит как поломка.
    if (const auto currentGroup = Configs::dataManager->groupsRepo->GetGroup(current);
        !currentGroup || currentGroup->Profiles().isEmpty()) {
        for (int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
            const auto candidate = Configs::dataManager->groupsRepo->GetGroup(gid);
            if (candidate && !candidate->Profiles().isEmpty()) {
                current = gid;
                Configs::dataManager->settingsRepo->current_group = gid;
                Configs::dataManager->settingsRepo->Save();
                break;
            }
        }
    }

    // Пустышки в список не берём: группа без серверов и без ссылки — это
    // остаток вроде «Default», выбирать в нём нечего, а в переключателе он
    // выглядит как ещё одна подписка. Управление группами живёт в расширенном
    // режиме, так что спрятать её здесь ничего не ломает.
    QList<int> visible;
    for (int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (!group) continue;
        if (group->Profiles().isEmpty() && group->url.isEmpty()) continue;
        visible << gid;
    }
    // Если пусто вообще всё, показываем как есть: пустой переключатель
    // непонятнее, чем группа без серверов.
    if (visible.isEmpty()) visible = Configs::dataManager->groupsRepo->GetGroupsTabOrder();

    // Текущая группа могла попасть под фильтр — например подписка ещё
    // скачивается, а current_group указывает на пустой «Default». Тогда
    // переключатель показывал бы одну группу, а список грузился из другой.
    if (!visible.isEmpty() && !visible.contains(current)) {
        current = visible.first();
        Configs::dataManager->settingsRepo->current_group = current;
        Configs::dataManager->settingsRepo->Save();
    }

    int currentIndex = 0;
    for (int gid : visible) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (!group) continue;
        m_groups->addItem(group->name, gid);
        if (gid == current) currentIndex = m_groups->count() - 1;
    }
    if (m_groups->count() > 0) m_groups->setCurrentIndex(currentIndex);

    reloadServers();

    emit groupsReloaded();
}
void ServerListPanel::reloadServers() {
    // Замер обновляет список каждые две секунды — не теряем выбор и прокрутку.
    const int keepId = m_list->currentItem()
                           ? m_list->currentItem()->data(ProfileIdRole).toInt() : -1;
    const int scroll = m_list->verticalScrollBar()->value();

    m_list->clear();

    const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (!group) {
        updateEmptyState();
        return;
    }

    const auto favs = favorites();
    for (const auto &profile : Configs::dataManager->profilesRepo->GetProfileBatch(group->Profiles())) {
        if (!profile || !profile->outbound) continue;
        auto *item = new QListWidgetItem(profile->outbound->DisplayName(), m_list);
        item->setData(ProfileIdRole, profile->id);
        item->setData(LatencyRole, profile->latency);
        item->setData(FavoriteRole, favs.contains(profile->outbound->DisplayName()));
        // При проверке одного сервера ждёт только он: иначе весь список
        // начинал «думать», хотя проверяли одну строку.
        const bool waiting = m_measureStartedAt > 0 && profile->latency_at < m_measureStartedAt;
        item->setData(MeasuringRole,
                      m_probeTarget >= 0 ? (profile->id == m_probeTarget && waiting) : waiting);

        // Подпись строим здесь, а не в делегате: у делегата нет доступа к
        // профилю, только к ролям элемента.
        item->setData(SubtitleRole, subtitleFor(profile));
        item->setData(InsecureRole, profile->outbound->GetSecurity().isDangerous());
    }

    applyFilter(m_search->text());
    updateEmptyState();

    if (keepId >= 0) {
        // Блокируем сигнал: восстановление прежней строки после перезагрузки
        // списка — не выбор пользователя. Без этого любой пересбор списка
        // записывался бы в simple_selected_profile и глушил автоподбор.
        const QSignalBlocker blocker(m_list);
        for (int row = 0; row < m_list->count(); ++row) {
            if (m_list->item(row)->data(ProfileIdRole).toInt() == keepId) {
                m_list->setCurrentRow(row);
                break;
            }
        }
    }
    m_list->verticalScrollBar()->setValue(scroll);
}

void ServerListPanel::applyFilter(const QString &text) {
    for (int row = 0; row < m_list->count(); ++row) {
        auto *item = m_list->item(row);
        const bool matchesText = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive);
        const bool matchesTab = !m_favouritesOnly || item->data(FavoriteRole).toBool();
        item->setHidden(!(matchesText && matchesTab));
    }
}

void ServerListPanel::updateEmptyState() {
    const bool hasServers = m_list->count() > 0;
    m_list->setVisible(hasServers);
    m_empty->setVisible(!hasServers);
    if (hasServers) return;

    // Различаем два разных «пусто»: подписки вообще нет и подписка есть, но пустая.
    const bool hasGroups = m_groups->count() > 0;
    m_emptyText->setText(hasGroups
        ? tr("This subscription has no servers yet. Refresh it or add another one.")
        : tr("No subscription yet.\nPaste the link your provider gave you and the servers will appear here."));
}

QString ServerListPanel::subtitleFor(const std::shared_ptr<Configs::Profile> &profile) {
    if (const auto cached = m_subtitleCache.constFind(profile->id);
        cached != m_subtitleCache.cend()) {
        return cached.value();
    }

    const bool fullConfig = profile->outbound->IsXrayFullConfig();

    // Описание от провайдера. Панели кладут его в meta.serverDescription
    // полного конфига, и оно заменяет техническую строку целиком: провайдер
    // лучше знает, что сказать про свой сервер, чем перечисление транспорта.
    if (fullConfig) {
        if (auto *custom = profile->Custom(); custom != nullptr) {
            // Дешёвая проверка перед разбором: конфиг весит десятки килобайт,
            // а поля в нём чаще всего нет.
            if (custom->config.contains(QStringLiteral("serverDescription"))) {
                const auto meta = QString2QJsonObject(custom->config)["meta"].toObject();
                if (const auto text = meta["serverDescription"].toString(); !text.isEmpty()) {
                    m_subtitleCache.insert(profile->id, text);
                    return text;
                }
            }
        }
    }

    const auto security = profile->outbound->GetSecurity();
    QStringList parts;
    // У полного конфига протокол лежит внутри JSON и наружу не выставлен,
    // а DisplayType() у него — «Custom Xray Config», то есть то же самое
    // слово «JSON», только длиннее. Не повторяемся: метку ставим в конец.
    if (!fullConfig) parts << profile->outbound->DisplayType();
    if (!security.transport.isEmpty()) parts << security.transport;
    if (!security.label.isEmpty()) parts << security.label;
    // По строке видно, пришёл сервер обычной ссылкой или полным конфигом JSON.
    if (fullConfig) parts << QStringLiteral("JSON");

    const QString subtitle = parts.join(QStringLiteral(" · "));
    m_subtitleCache.insert(profile->id, subtitle);
    return subtitle;
}

QSet<QString> ServerListPanel::serverNames(const QList<int> &ids) {
    QSet<QString> names;
    for (const auto &profile : Configs::dataManager->profilesRepo->GetProfileBatch(ids)) {
        if (profile && profile->outbound) names.insert(profile->outbound->DisplayName());
    }
    return names;
}

bool ServerListPanel::allMeasured() const {
    // По latency, а не по нулю: повторный замер стартует с уже ненулевыми
    // значениями прошлого прогона, и проверка «латентность != 0» была бы
    // истинной сразу же.
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(MeasuringRole).toBool()) return false;
    }
    return m_list->count() > 0;
}

int ServerListPanel::answeredCount() const {
    int answered = 0;
    for (int row = 0; row < m_list->count(); ++row) {
        if (!m_list->item(row)->data(MeasuringRole).toBool()) ++answered;
    }
    return answered;
}

void ServerListPanel::finishMeasurement() {
    const bool neverStarted = answeredCount() == 0;
    // Ответ прошлого прогона не должен закрывать следующий.
    ++m_measureRun;

    m_latencyPoll->stop();
    m_measureRepaint->stop();
    setPingBusy(false);
    m_measureStartedAt = 0;
    m_measureExactEnd = false;

    if (m_probeTarget >= 0) {
        const auto profile = Configs::dataManager->profilesRepo->GetProfile(m_probeTarget);
        m_probeTarget = -1;
        reloadServers();
        if (profile != nullptr) {
            emit notice(profile->latency > 0
                            ? tr("%1: %2 ms").arg(profile->outbound->DisplayName()).arg(profile->latency)
                            : tr("%1: no answer").arg(profile->outbound->DisplayName()));
        }
        return;
    }

    int withLatency = 0;
    const int total = m_list->count();
    for (int row = 0; row < total; ++row) {
        if (m_list->item(row)->data(LatencyRole).toInt() > 0) ++withLatency;
    }
    // Обновляем строки ещё раз: MeasuringRole надо снять со всех, кто так и
    // не ответил, иначе точки продолжали бы «думать» на замершем списке.
    reloadServers();

    // В журнал тоже: по архиву диагностики видно, когда панель сняла «идёт
    // замер» и чем он кончился, а не только что прогон запускался.
    MW_show_log(QStringLiteral("[Latency] finished: %1 of %2 answered").arg(withLatency).arg(total));

    if (neverStarted) {
        emit notice(tr("Latency check did not start"));
    } else if (withLatency == total) {
        emit notice(tr("All servers responded"));
    } else {
        emit notice(tr("%1 of %2 servers responded").arg(withLatency).arg(total));
    }
}

QWidget *ServerListPanel::addSubscriptionButton() const { return m_addSub; }

QWidget *ServerListPanel::subscriptionSelector() const { return m_groups; }

QWidget *ServerListPanel::listArea() const {
    if (m_list != nullptr && m_list->isVisible()) return m_list;
    return m_empty != nullptr ? m_empty : m_list;
}

void ServerListPanel::updateSubscription() {
    // Кнопка на время работы отключается сама, а пункт меню «Обновить
    // подписку» ведёт сюда же и ничем не заперт: два ждущих на одном задании
    // дали бы два уведомления об одном обновлении.
    if (m_updateSub->isBusy()) return;

    const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (!group || group->url.isEmpty()) return;

    const int gid = group->id;
    const QString name = group->name;

    m_updateSub->setBusy(true);
    // Surface::Log, а не Report: встроенный отчёт — модальный список всех
    // профилей, два десятка строк «[+] [VLESS (Xray)] …», которые надо
    // закрывать руками. Сводку показываем уведомлением.
    //
    // RefreshWithOutcome, а не RefreshGroup. Тот отдаёт колбэк без результата,
    // и панель считала итог сама, сравнивая имена серверов до и после. При
    // отказе загрузки группа по замыслу остаётся нетронутой, имена совпадают —
    // и человек читал «обновлено, ничего не изменилось» ровно там, где
    // провайдер не ответил, отдал капчу или упёрся в лимит устройств. Отчёт о
    // причине при этом уходил в журнал, которого в простом окне не видно.
    // Вдобавок RefreshGroup на уже идущем обновлении этой группы вызывает
    // колбэк сразу и синхронно, и дуга гасла посреди работы, а RefreshWithOutcome
    // в том же случае встаёт в очередь ожидающих и отвечает вместе с ней.
    QPointer<ServerListPanel> self(this);
    Subscription::updater()->RefreshWithOutcome(
        gid, Subscription::GroupUpdater::Surface::Log,
        [self, name](const Subscription::UpdateOutcome &outcome) {
            // Колбэк приходит из рабочего потока обновлятора — возвращаемся в UI.
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, name, outcome] {
                if (self.isNull()) return;
                self->m_updateSub->setBusy(false);
                self->reloadServers();
                emit self->notice(subscriptionUpdateNotice(name, outcome));
            }, Qt::QueuedConnection);
        });
}

// Итог обновления одной строкой. Имя подписки в ней обязательно: обновление
// может оставить группу пустой, и тогда панель уезжает на соседнюю — без имени
// человек читает итог, глядя на чужие серверы.
QString ServerListPanel::subscriptionUpdateNotice(const QString &name,
                                                  const Subscription::UpdateOutcome &outcome) {
    using Failure = Subscription::UpdateOutcome::Failure;
    switch (outcome.failure) {
        case Failure::None:
            break;
        case Failure::NoGroup:
            return tr("%1 is gone").arg(name);
        case Failure::Fetch:
            return tr("%1 did not answer, the server list is unchanged").arg(name);
        case Failure::Empty:
            return tr("%1 returned no servers, the list is unchanged").arg(name);
        case Failure::Database:
        case Failure::Internal:
            return tr("%1 could not be updated").arg(name);
    }

    // Переименование сервера приходит как updated, а не как пара
    // «добавлен и удалён»: считает это сам обновлятор, по своему сличению.
    const int changed = outcome.added + outcome.removed + outcome.updated;
    if (changed == 0) return tr("%1 updated, nothing changed").arg(name);
    if (outcome.removed == 0 && outcome.updated == 0) {
        return tr("%1 updated: %2 new").arg(name).arg(outcome.added);
    }
    if (outcome.added == 0 && outcome.updated == 0) {
        return tr("%1 updated: %2 removed").arg(name).arg(outcome.removed);
    }
    return tr("%1 updated: %2 new, %3 changed, %4 removed")
        .arg(name)
        .arg(outcome.added)
        .arg(outcome.updated)
        .arg(outcome.removed);
}

void ServerListPanel::selectProfile(const int profileId) {
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(ProfileIdRole).toInt() != profileId) continue;
        if (m_list->currentRow() == row) return;
        // Блокируем сигнал: это не выбор пользователя, и записывать его
        // в simple_selected_profile нельзя — иначе автоподбор станет ручным.
        const QSignalBlocker blocker(m_list);
        m_list->setCurrentRow(row);
        return;
    }
}
