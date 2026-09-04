#include "include/ui/setting/dialog_per_app_proxy.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

#include "include/database/RoutesRepo.h"
#include "include/database/entities/RouteProfile.h"
#include "include/global/Configs.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/mainwindow_interface.h"

namespace {
    constexpr auto kPrefix = "processName:";

    // Разделяем набор простых правил на строки по процессам и все остальные.
    // Остальные обязаны пережить сохранение: там пользовательские домены и адреса.
    void split(const QString &rules, QStringList &processes, QStringList &others) {
        for (const QString &line : rules.split('\n', Qt::SkipEmptyParts)) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;
            if (trimmed.startsWith(kPrefix, Qt::CaseInsensitive)) {
                processes << trimmed.mid(QString(kPrefix).length()).trimmed();
            } else {
                others << trimmed;
            }
        }
    }
}

QStringList DialogPerAppProxy::runningProcesses() {
    QProcess process;
#ifdef Q_OS_WIN
    process.start("tasklist", QStringList() << "/FO" << "CSV" << "/NH");
#else
    process.start("ps", QStringList() << "ax" << "-o" << "comm=");
#endif
    if (!process.waitForFinished(5000)) return {};

    QSet<QString> seen;
    for (const QString &raw : QString::fromUtf8(process.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts)) {
        QString name = raw.trimmed();
        if (name.isEmpty()) continue;
#ifdef Q_OS_WIN
        name = name.split(',').first().remove('"').trimmed();
#else
        // ps отдаёт полный путь: пользователю нужно имя исполняемого файла.
        name = QFileInfo(name).fileName();
#endif
        if (name.isEmpty() || name.startsWith('[')) continue;
        seen.insert(name);
    }

    QStringList list(seen.begin(), seen.end());
    list.sort(Qt::CaseInsensitive);
    return list;
}

DialogPerAppProxy::DialogPerAppProxy(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Per-app proxy"));
    resize(520, 560);

    chain = Configs::dataManager->routesRepo->GetRouteProfile(
        Configs::dataManager->settingsRepo->current_route_id);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        tr("Choose how traffic of each application is routed. The choice is stored as\n"
           "processName rules in the current routing profile."), this));

    if (chain == nullptr) {
        layout->addWidget(new QLabel(tr("No routing profile is selected."), this));
        auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(box);
        return;
    }

    QStringList proxyProcesses, proxyOthers, directProcesses, directOthers;
    split(chain->GetSimpleRules(Configs::proxy), proxyProcesses, proxyOthers);
    split(chain->GetSimpleRules(Configs::bypass), directProcesses, directOthers);

    QMap<QString, int> known;
    for (const QString &p : proxyProcesses) known[p] = Proxy;
    for (const QString &p : directProcesses) known[p] = Direct;

    // Запущенных процессов сотни, без поиска список бесполезен.
    auto *filter = new QLineEdit(this);
    filter->setPlaceholderText(tr("Filter applications..."));
    filter->setClearButtonEnabled(true);
    layout->addWidget(filter);

    buildTable(known);
    layout->addWidget(table);

    connect(filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto *item = table->item(row, 0);
            const bool visible = text.isEmpty()
                || (item != nullptr && item->text().contains(text, Qt::CaseInsensitive));
            table->setRowHidden(row, !visible);
        }
    });

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, [this] { save(); accept(); });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);
}

void DialogPerAppProxy::buildTable(const QMap<QString, int> &known) {
    // Уже настроенные приложения могли завершиться — показываем их вместе с запущенными,
    // иначе сохранение молча потеряло бы правило.
    QStringList names = runningProcesses();
    for (const QString &configured : known.keys()) {
        if (!names.contains(configured, Qt::CaseInsensitive)) names << configured;
    }
    names.sort(Qt::CaseInsensitive);

    table = new QTableWidget(names.size(), 2, this);
    table->setHorizontalHeaderLabels({tr("Application"), tr("Routing")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    for (int row = 0; row < names.size(); ++row) {
        const QString &name = names[row];
        table->setItem(row, 0, new QTableWidgetItem(name));

        auto *mode = new QComboBox(table);
        mode->addItem(tr("Not set"), None);
        mode->addItem(tr("Through proxy"), Proxy);
        mode->addItem(tr("Direct"), Direct);
        mode->setCurrentIndex(known.value(name, None));
        table->setCellWidget(row, 1, mode);
    }
}

void DialogPerAppProxy::save() {
    if (chain == nullptr || table == nullptr) return;

    QStringList proxyProcesses, proxyOthers, directProcesses, directOthers;
    split(chain->GetSimpleRules(Configs::proxy), proxyProcesses, proxyOthers);
    split(chain->GetSimpleRules(Configs::bypass), directProcesses, directOthers);

    QStringList newProxy, newDirect;
    for (int row = 0; row < table->rowCount(); ++row) {
        const auto *item = table->item(row, 0);
        const auto *mode = qobject_cast<QComboBox *>(table->cellWidget(row, 1));
        if (item == nullptr || mode == nullptr) continue;

        switch (mode->currentData().toInt()) {
            case Proxy:  newProxy  << QString(kPrefix) + item->text(); break;
            case Direct: newDirect << QString(kPrefix) + item->text(); break;
            default: break;
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
