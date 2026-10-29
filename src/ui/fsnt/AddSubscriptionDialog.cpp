#include "include/ui/fsnt/AddSubscriptionDialog.h"

#include "include/ui/mainwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "include/database/GroupsRepo.h"
#include "include/configs/sub/GroupUpdater.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"

namespace {
    // Подписка тянется по сети и может упереться в таймаут HTTP-клиента.
    constexpr int kAddSubGuardMs = 45000;
}

AddSubscriptionDialog::AddSubscriptionDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("fsntDialog");
    setWindowTitle(tr("Add a server"));
    setModal(true);
    setMinimumWidth(460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(10);

    auto *title = new QLabel(tr("Add a server"), this);
    title->setObjectName("fsntDialogTitle");
    layout->addWidget(title);

    auto *hint = new QLabel(tr("Paste the subscription link your provider gave you, or a single "
                               "server link such as vless:// or ss://."), this);
    hint->setObjectName("fsntDialogHint");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    layout->addSpacing(4);

    m_input = new QLineEdit(this);
    m_input->setObjectName("fsntInput");
    m_input->setPlaceholderText("https://");
    m_input->setClearButtonEnabled(true);
    layout->addWidget(m_input);

    auto *paste = new QPushButton(tr("Paste from clipboard"), this);
    paste->setObjectName("fsntGhost");
    paste->setCursor(Qt::PointingHandCursor);
    connect(paste, &QPushButton::clicked, this, &AddSubscriptionDialog::pasteFromClipboard);

    auto *pasteRow = new QHBoxLayout;
    pasteRow->addWidget(paste);
    pasteRow->addStretch();
    layout->addLayout(pasteRow);

    m_status = new QLabel(this);
    m_status->setObjectName("fsntDialogHint");
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    layout->addSpacing(6);

    m_cancel = new QPushButton(tr("Cancel"), this);
    m_cancel->setObjectName("fsntGhost");
    m_cancel->setCursor(Qt::PointingHandCursor);
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);

    m_add = new QPushButton(tr("Add"), this);
    m_add->setObjectName("fsntPrimary");
    m_add->setCursor(Qt::PointingHandCursor);
    m_add->setDefault(true);
    connect(m_add, &QPushButton::clicked, this, &AddSubscriptionDialog::submit);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_add);
    layout->addLayout(buttons);

    connect(m_input, &QLineEdit::returnPressed, this, &AddSubscriptionDialog::submit);

    m_guard = new QTimer(this);
    m_guard->setSingleShot(true);
    m_guard->setInterval(kAddSubGuardMs);
    connect(m_guard, &QTimer::timeout, this, [this] {
        fail(tr("The provider did not answer. Check the link and your connection."));
    });

    setStyleSheet(Fsnt::BuildStyleSheet());
    m_input->setFocus();
}

void AddSubscriptionDialog::pasteFromClipboard() {
    const QString text = QApplication::clipboard()->text().trimmed();
    if (text.isEmpty()) {
        showStatus(tr("The clipboard is empty."));
        return;
    }
    m_input->setText(text);
    showStatus({});
}

void AddSubscriptionDialog::setBusy(const bool busy) {
    m_busy = busy;
    m_input->setEnabled(!busy);
    m_cancel->setEnabled(!busy);
    m_add->setEnabled(!busy);
    m_add->setText(busy ? tr("Adding…") : tr("Add"));
}

QString AddSubscriptionDialog::subscriptionFailureHint() {
    // Чаще всего дело не в ссылке. У людей с обходчиками блокировок запрос к
    // провайдеру умирает на рукопожатии TLS: пакеты правит сторонний драйвер,
    // и клиент видит «wrong version number». Поэтому сначала называем причину,
    // которую человек в состоянии проверить сам.
    QStringList conflicts;
    if (auto *mw = GetMainWindow(); mw != nullptr) conflicts = mw->CheckConflictingProcesses();
    if (!conflicts.isEmpty()) {
        return tr("Could not open the subscription. Most likely it is blocked by %1 — such programs "
                  "rewrite network packets, and the provider's site stops answering. Close it and try "
                  "again.")
            .arg(conflicts.join(", "));
    }
    return tr("Could not open the subscription. Check the link, and if it opens in a browser — look "
              "for an anti-censorship tool or antivirus that filters traffic on this computer.");
}

// Самое нужное в объяснении стоит в конце — что именно искать на компьютере.
// Метка переносит по словам, а высота переноса известна только когда известна
// ширина: окно, размер которого посчитан по пустой метке, само под текст не
// растёт, и хвост уезжает под кнопки. Поэтому высоту пересчитываем руками.
void AddSubscriptionDialog::showStatus(const QString &text) {
    m_status->setText(text);
    if (!isVisible() || layout() == nullptr) return;
    layout()->activate();
    const int wanted = layout()->heightForWidth(width());
    if (wanted > height()) resize(width(), wanted);
}

void AddSubscriptionDialog::fail(const QString &message) {
    m_guard->stop();
    setBusy(false);
    showStatus(message);
}

// Группа для серверов, которые человек добавил сам. Отличается от подписки
// отсутствием ссылки: её не обновляют и, значит, не сверяют с сервером.
int AddSubscriptionDialog::ownServersGroup() {
    auto &groups = Configs::dataManager->groupsRepo;
    for (const int gid : groups->GetGroupsTabOrder()) {
        const auto group = groups->GetGroup(gid);
        if (group != nullptr && group->url.trimmed().isEmpty()) return gid;
    }

    auto group = Configs::GroupsRepo::NewGroup();
    group->name = tr("My servers");
    if (!groups->AddGroup(group)) return -1;
    return group->id;
}

void AddSubscriptionDialog::submit() {
    if (m_busy) return;

    const QString text = m_input->text().trimmed();
    if (text.isEmpty()) {
        showStatus(tr("Enter a link first."));
        return;
    }

    // Диплинк ведёт своим путём — там могут быть не только подписки.
    if (text.startsWith("throne://")) {
        if (MW_handle_deeplink) MW_handle_deeplink(text);
        accept();
        return;
    }

    if (!text.startsWith("http://") && !text.startsWith("https://")) {
        // Не ссылка — значит это сами серверы ссылками или текстом: vless://,
        // ss:// и прочее. Провайдеры так тоже отдают.
        //
        // Кладём в свою группу, а не в текущую. Текущая почти всегда подписка,
        // а её обновление сверяет список с сервером и выбрасывает всё, чего там
        // нет: добавленный вручную сервер молча исчезал при первом же
        // обновлении. Группу без ссылки обновление не трогает вовсе.
        const int gid = ownServersGroup();
        if (gid < 0) {
            showStatus(tr("Could not create a group for your own servers."));
            return;
        }
        setBusy(true);
        showStatus(tr("Adding…"));
        m_guard->start();
        Subscription::updater()->ImportTextTo(gid, text, [this](const Subscription::UpdateOutcome &outcome) {
            const bool ok = outcome.ok();
            const auto error = outcome.error;
            QMetaObject::invokeMethod(this, [this, ok, error] {
                m_guard->stop();
                if (!ok) {
                    // Молча закрываться нельзя: человек вставил ссылку и ждёт
                    // увидеть сервер, а не пустой список.
                    fail(error.isEmpty() ? tr("Nothing here looks like a server link.") : error);
                    return;
                }
                accept();
            }, Qt::QueuedConnection);
        });
        return;
    }

    setBusy(true);
    showStatus(tr("Fetching the subscription…"));
    m_guard->start();

    // Колбэк приходит из рабочего потока обновлятора — возвращаемся в UI.
    Subscription::updater()->SubscribeUrlWithOutcome(text, [this](const Subscription::UpdateOutcome &outcome) {
        const bool ok = outcome.ok();
        QMetaObject::invokeMethod(this, [this, ok] {
            m_guard->stop();
            if (!ok) {
                fail(subscriptionFailureHint());
                return;
            }
            accept();
        }, Qt::QueuedConnection);
    });
}
