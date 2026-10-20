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

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"

namespace {
    // Подписка тянется по сети и может упереться в таймаут HTTP-клиента.
    constexpr int kAddSubGuardMs = 45000;
}

AddSubscriptionDialog::AddSubscriptionDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("fsntDialog");
    setWindowTitle(tr("Add subscription"));
    setModal(true);
    setMinimumWidth(460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(10);

    auto *title = new QLabel(tr("Add subscription"), this);
    title->setObjectName("fsntDialogTitle");
    layout->addWidget(title);

    auto *hint = new QLabel(tr("Paste the link your provider gave you."), this);
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
        m_status->setText(tr("The clipboard is empty."));
        return;
    }
    m_input->setText(text);
    m_status->clear();
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

void AddSubscriptionDialog::fail(const QString &message) {
    m_guard->stop();
    setBusy(false);
    m_status->setText(message);
}

void AddSubscriptionDialog::submit() {
    if (m_busy) return;

    const QString text = m_input->text().trimmed();
    if (text.isEmpty()) {
        m_status->setText(tr("Enter a link first."));
        return;
    }

    // Диплинк ведёт своим путём — там могут быть не только подписки.
    if (text.startsWith("throne://")) {
        if (MW_handle_deeplink) MW_handle_deeplink(text);
        accept();
        return;
    }

    if (!text.startsWith("http://") && !text.startsWith("https://")) {
        // Не ссылка — значит это сами профили текстом; провайдеры так тоже отдают.
        setBusy(true);
        m_guard->start();
        Subscription::updater()->ImportText(text, -1, [this] {
            QMetaObject::invokeMethod(this, [this] {
                m_guard->stop();
                accept();
            }, Qt::QueuedConnection);
        });
        return;
    }

    setBusy(true);
    m_status->setText(tr("Fetching the subscription…"));
    m_guard->start();

    // Колбэк приходит из рабочего потока обновлятора — возвращаемся в UI.
    using Result = Subscription::GroupUpdater::SubscribeResult;
    Subscription::updater()->SubscribeUrl(text, [this](Result result) {
        QMetaObject::invokeMethod(this, [this, result] {
            m_guard->stop();
            if (result == Result::Failed) {
                fail(subscriptionFailureHint());
                return;
            }
            accept();
        }, Qt::QueuedConnection);
    });
}
