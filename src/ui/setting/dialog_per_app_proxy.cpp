#include <algorithm>

#include "include/ui/setting/dialog_per_app_proxy.h"

#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

#include "include/database/RoutesRepo.h"
#include "include/database/entities/RouteProfile.h"
#include "include/global/Configs.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/fsnt/FsntControls.h"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/ui/mainwindow_interface.h"

namespace {
    constexpr auto kAppPrefix = "processName:";
    constexpr int kAppIconSize = 30;
    constexpr int kAppRowHeight = 52;

    // Разделяем набор простых правил на строки по процессам и все остальные.
    // Остальные обязаны пережить сохранение: там пользовательские домены и адреса.
    void split(const QString &rules, QStringList &processes, QStringList &others) {
        for (const QString &line : rules.split('\n', Qt::SkipEmptyParts)) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;
            if (trimmed.startsWith(kAppPrefix, Qt::CaseInsensitive)) {
                processes << trimmed.mid(QString(kAppPrefix).length()).trimmed();
            } else {
                others << trimmed;
            }
        }
    }

    // Путь до бандла приложения, если исполняемый файл лежит внутри .app.
    // Берём первый .app в пути: вложенные — это хелперы того же приложения,
    // и относиться к ним надо как к нему самому.
    QString bundlePath(const QString &executable) {
        const int marker = executable.indexOf(QStringLiteral(".app/"));
        if (marker < 0) return {};
        return executable.left(marker + 4);
    }

    // Отличаем приложение пользователя от системной службы. Без этого список —
    // сотни строк вроде cfprefsd, и найти в нём свой браузер невозможно.
    bool looksLikeUserApp(const QString &path) {
        // Разделитель проверяем оба: в Windows путь идёт с обратным слэшем, и
        // проверка только на '/' помечала системным вообще всё, из-за чего
        // список приложений оказывался пустым.
        if (path.isEmpty() || (!path.contains('/') && !path.contains('\\'))) return false;

#ifdef Q_OS_MACOS
        // .appex — виджет или расширение внутри приложения, а не приложение.
        if (path.contains(QStringLiteral(".appex/"))) return false;
        // Приложение пользователя на macOS — это бандл, всё остальное служебное.
        if (!path.contains(QStringLiteral(".app/"))) return false;
        return !path.startsWith(QStringLiteral("/System/"));
#elif defined(Q_OS_WIN)
        const QString lower = path.toLower();
        return !lower.startsWith(QStringLiteral("c:\\windows\\"))
               && !lower.startsWith(QStringLiteral("c:/windows/"));
#else
        return !path.startsWith(QStringLiteral("/usr/lib"))
               && !path.startsWith(QStringLiteral("/usr/libexec"))
               && !path.startsWith(QStringLiteral("/lib"))
               && !path.startsWith(QStringLiteral("/sbin/"));
#endif
    }
}

QList<DialogPerAppProxy::AppEntry> DialogPerAppProxy::runningApplications() {
    QProcess process;
#ifdef Q_OS_WIN
    // tasklist не отдаёт путь, а без пути нет ни иконки, ни отсева системных.
    //
    // OutputEncoding задаём явно: по умолчанию PowerShell пишет в кодировке
    // консоли, а не в UTF-8, и путь с кириллицей приезжал мусором.
    // SilentlyContinue — у процессов другого пользователя чтение Path кидает
    // отказ в доступе, и без этого поток ошибок забивал вывод.
    process.start("powershell", QStringList()
        << "-NoProfile" << "-NonInteractive" << "-Command"
        << "[Console]::OutputEncoding=[Text.Encoding]::UTF8; "
           "Get-Process -ErrorAction SilentlyContinue | "
           "ForEach-Object { $_.Path } | Where-Object { $_ } | Sort-Object -Unique");
#else
    process.start("ps", QStringList() << "ax" << "-o" << "comm=");
#endif
    if (!process.waitForFinished(8000)) return {};

    // Ключ группы — бандл приложения, а для одиночных программ их собственный
    // путь. Иначе Electron даёт четыре строки «Claude» на один значок.
    QMap<QString, AppEntry> groups;
    for (const QString &raw : QString::fromUtf8(process.readAllStandardOutput())
                                  .split('\n', Qt::SkipEmptyParts)) {
        const QString path = QDir::fromNativeSeparators(raw.trimmed());
        // Ядерные потоки ps печатает в скобках, исполняемого файла у них нет.
        if (path.isEmpty() || path.startsWith('[')) continue;

        const QString processName = QFileInfo(path).fileName();
        if (processName.isEmpty()) continue;

        const QString bundle = bundlePath(path);
        const QString key = bundle.isEmpty() ? path : bundle;

        AppEntry &entry = groups[key];
        if (entry.processes.isEmpty()) {
            entry.iconPath = key;
            entry.name = bundle.isEmpty() ? processName : QFileInfo(bundle).completeBaseName();
        }
        if (!entry.processes.contains(processName)) entry.processes << processName;
        // Достаточно одного «настоящего» процесса, чтобы показать приложение:
        // сам бандл лежит в /Applications, а хелперы — в его Frameworks.
        entry.userApp = entry.userApp || looksLikeUserApp(path);
    }

    return groups.values();
}

QIcon DialogPerAppProxy::iconFor(const AppEntry &entry) {
    static QFileIconProvider provider;
    const QFileInfo info(entry.iconPath);
    if (!info.exists()) return {};
    return provider.icon(info);
}

DialogPerAppProxy::DialogPerAppProxy(QWidget *parent) : QDialog(parent) {
    setObjectName("fsntDialog");
    setWindowTitle(tr("Per-app proxy"));
    resize(580, 660);

    chain = Configs::dataManager->routesRepo->GetRouteProfile(
        Configs::dataManager->settingsRepo->current_route_id);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(12);

    auto *title = new QLabel(tr("Per-app proxy"), this);
    title->setObjectName("fsntDialogTitle");
    layout->addWidget(title);

    auto *hint = new QLabel(
        tr("Choose how each application is routed. The choice is stored as processName "
           "rules in the current routing profile."), this);
    hint->setObjectName("fsntDialogHint");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    if (chain == nullptr) {
        auto *empty = new QLabel(tr("No routing profile is selected."), this);
        empty->setObjectName("fsntDialogHint");
        layout->addWidget(empty);
        layout->addStretch();

        auto *close = new QPushButton(tr("Close"), this);
        close->setObjectName("fsntGhost");
        connect(close, &QPushButton::clicked, this, &QDialog::reject);
        layout->addWidget(close, 0, Qt::AlignRight);
        setStyleSheet(Fsnt::BuildStyleSheet());
        return;
    }

    QStringList proxyProcesses, proxyOthers, directProcesses, directOthers;
    split(chain->GetSimpleRules(Configs::proxy), proxyProcesses, proxyOthers);
    split(chain->GetSimpleRules(Configs::bypass), directProcesses, directOthers);

    QMap<QString, int> known;
    for (const QString &p : proxyProcesses) known[p] = Proxy;
    for (const QString &p : directProcesses) known[p] = Direct;

    m_filter = new QLineEdit(this);
    m_filter->setObjectName("fsntSearch");
    m_filter->setPlaceholderText(tr("Search applications"));
    m_filter->setClearButtonEnabled(true);
    layout->addWidget(m_filter);

    auto *systemRow = new QHBoxLayout;
    auto *systemLabel = new QLabel(tr("Show system processes"), this);
    systemLabel->setObjectName("fsntRowLabel");
    systemRow->addWidget(systemLabel);
    systemRow->addStretch();
    m_showSystem = new FsntSwitch(this);
    systemRow->addWidget(m_showSystem);
    layout->addLayout(systemRow);

    buildList(known);
    layout->addWidget(m_list, 1);

    connect(m_filter, &QLineEdit::textChanged, this, &DialogPerAppProxy::applyFilter);
    connect(m_showSystem, &FsntSwitch::toggled, this, &DialogPerAppProxy::applyFilter);
    applyFilter();

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

void DialogPerAppProxy::buildList(const QMap<QString, int> &known) {
    m_entries = runningApplications();

    // Уже настроенные приложения могли завершиться — показываем их вместе с
    // запущенными, иначе сохранение молча потеряло бы правило.
    QSet<QString> present;
    for (const AppEntry &entry : m_entries) {
        for (const QString &name : entry.processes) present.insert(name);
    }
    for (const QString &configured : known.keys()) {
        if (present.contains(configured)) continue;
        AppEntry entry;
        entry.name = configured;
        entry.processes << configured;
        entry.userApp = true;   // раз правило есть, прятать его нельзя
        m_entries << entry;
    }

    std::sort(m_entries.begin(), m_entries.end(), [](const AppEntry &a, const AppEntry &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    m_list = new QListWidget(this);
    m_list->setObjectName("fsntServerList");
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);

    for (const AppEntry &entry : m_entries) {
        auto *item = new QListWidgetItem(m_list);
        item->setSizeHint(QSize(0, kAppRowHeight));

        auto *row = new QWidget(m_list);
        auto *box = new QHBoxLayout(row);
        box->setContentsMargins(10, 6, 10, 6);
        box->setSpacing(12);

        auto *icon = new QLabel(row);
        icon->setFixedSize(kAppIconSize, kAppIconSize);
        icon->setScaledContents(true);
        if (const QIcon pic = iconFor(entry); !pic.isNull()) {
            icon->setPixmap(pic.pixmap(kAppIconSize, kAppIconSize));
        }
        box->addWidget(icon);

        auto *name = new QLabel(entry.name, row);
        name->setObjectName("fsntRowLabel");
        box->addWidget(name, 1);

        auto *mode = new FsntSelect(row);
        mode->setMinimumWidth(150);
        mode->addItem(tr("Not set"), None);
        mode->addItem(tr("Through proxy"), Proxy);
        mode->addItem(tr("Direct"), Direct);
        // Режим группы — режим любого её процесса: правила у них общие.
        int current = None;
        for (const QString &process : entry.processes) {
            if (const int stored = known.value(process, None); stored != None) {
                current = stored;
                break;
            }
        }
        mode->setCurrentIndex(current);
        box->addWidget(mode);

        m_modes << mode;
        m_list->setItemWidget(item, row);
    }
}

void DialogPerAppProxy::applyFilter() {
    const QString text = m_filter->text().trimmed();
    const bool showSystem = m_showSystem->isChecked();

    for (int row = 0; row < m_list->count() && row < m_entries.size(); ++row) {
        const AppEntry &entry = m_entries[row];
        // Настроенное приложение видно всегда: иначе правило можно потерять из виду.
        const bool configured = row < m_modes.size() && m_modes[row]->currentIndex() != None;

        const bool matchesKind = showSystem || entry.userApp || configured;
        const bool matchesText = text.isEmpty()
            || entry.name.contains(text, Qt::CaseInsensitive)
            || entry.processes.filter(text, Qt::CaseInsensitive).size() > 0;

        m_list->item(row)->setHidden(!(matchesKind && matchesText));
    }
}

void DialogPerAppProxy::save() {
    if (chain == nullptr) return;

    QStringList proxyProcesses, proxyOthers, directProcesses, directOthers;
    split(chain->GetSimpleRules(Configs::proxy), proxyProcesses, proxyOthers);
    split(chain->GetSimpleRules(Configs::bypass), directProcesses, directOthers);

    QStringList newProxy, newDirect;
    for (int row = 0; row < m_entries.size() && row < m_modes.size(); ++row) {
        const int mode = m_modes[row]->currentData().toInt();
        if (mode == None) continue;
        // Правило пишем на каждый процесс приложения: маршрутизатор знает только
        // имена процессов и про бандлы не в курсе.
        for (const QString &process : m_entries[row].processes) {
            (mode == Proxy ? newProxy : newDirect) << QString(kAppPrefix) + process;
        }
    }

    // Правила по доменам и адресам сохраняем нетронутыми.
    QString error;
    error += chain->UpdateSimpleRules((proxyOthers + newProxy).join('\n'), Configs::proxy);
    error += chain->UpdateSimpleRules((directOthers + newDirect).join('\n'), Configs::bypass);

    if (!error.isEmpty()) {
        MessageBoxWarning(tr("Per-app proxy"), error);
        return;
    }

    Configs::dataManager->routesRepo->Save(chain);
    MW_show_log(tr("Per-app routing updated: %1 via proxy, %2 direct")
                    .arg(newProxy.size()).arg(newDirect.size()));
}
