#include "include/ui/fsnt/DiagnosticsDialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

#include "include/global/Utils.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"

namespace {
    constexpr int kDiagRowSpacing = 6;

    QString diagStateTone(const Fsnt::CheckResult::State state) {
        switch (state) {
            case Fsnt::CheckResult::Ok: return QStringLiteral("ok");
            case Fsnt::CheckResult::Warning: return QStringLiteral("busy");
            case Fsnt::CheckResult::Failed: return QStringLiteral("bad");
            case Fsnt::CheckResult::Skipped: break;
        }
        return {};
    }

    QString diagStateText(const Fsnt::CheckResult::State state) {
        switch (state) {
            case Fsnt::CheckResult::Ok: return QObject::tr("OK");
            case Fsnt::CheckResult::Warning: return QObject::tr("Check");
            case Fsnt::CheckResult::Failed: return QObject::tr("Problem");
            case Fsnt::CheckResult::Skipped: return QObject::tr("Skipped");
        }
        return {};
    }
}

FsntDiagnosticsDialog::FsntDiagnosticsDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("fsntDialog");
    setWindowTitle(tr("Diagnostics"));
    resize(600, 640);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(12);

    auto *title = new QLabel(tr("Diagnostics"), this);
    title->setObjectName("fsntDialogTitle");
    layout->addWidget(title);

    auto *hint = new QLabel(
        tr("The client checks itself and says what to fix. Everything here runs on this "
           "computer; nothing is sent anywhere."), this);
    hint->setObjectName("fsntDialogHint");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *host = new QWidget(scroll);
    auto *column = new QVBoxLayout(host);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(kDiagRowSpacing);

    m_checks = Fsnt::BuildChecks();
    for (const auto &check : m_checks) {
        auto *card = new QWidget(host);
        card->setObjectName("fsntCard");
        auto *box = new QVBoxLayout(card);
        box->setContentsMargins(14, 10, 14, 12);
        box->setSpacing(4);

        auto *head = new QHBoxLayout;
        auto *name = new QLabel(check.title, card);
        name->setObjectName("fsntRowLabel");
        head->addWidget(name, 1);

        Row row;
        row.status = new QLabel(tr("checking…"), card);
        row.status->setObjectName("fsntStatus");
        head->addWidget(row.status);
        box->addLayout(head);

        row.detail = new QLabel(card);
        row.detail->setObjectName("fsntRowNote");
        row.detail->setWordWrap(true);
        row.detail->hide();
        box->addWidget(row.detail);

        row.fix = new QPushButton(card);
        row.fix->setObjectName("fsntGhost");
        row.fix->setCursor(Qt::PointingHandCursor);
        row.fix->hide();
        box->addWidget(row.fix, 0, Qt::AlignLeft);

        column->addWidget(card);
        m_rows << row;
    }
    column->addStretch();
    scroll->setWidget(host);
    layout->addWidget(scroll, 1);

    m_summary = new QLabel(this);
    m_summary->setObjectName("fsntDialogHint");
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto *buttons = new QHBoxLayout;
    m_again = new QPushButton(tr("Run again"), this);
    m_again->setObjectName("fsntGhost");
    m_again->setCursor(Qt::PointingHandCursor);
    m_again->setEnabled(false);
    connect(m_again, &QPushButton::clicked, this, [this] {
        // Пересобирать окно незачем: набор проверок тот же, меняются результаты.
        for (auto &row : m_rows) {
            row.status->setText(tr("checking…"));
            row.status->setProperty("tone", QString());
            row.detail->hide();
            row.fix->hide();
        }
        m_summary->clear();
        m_again->setEnabled(false);
        m_current = 0;
        runNext();
    });
    buttons->addWidget(m_again);
    buttons->addStretch();

    auto *close = new QPushButton(tr("Close"), this);
    close->setObjectName("fsntPrimary");
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    setStyleSheet(Fsnt::BuildStyleSheet());
    runNext();
}

void FsntDiagnosticsDialog::runNext() {
    if (m_current >= m_checks.size()) {
        finish();
        return;
    }

    const int index = m_current;
    auto run = m_checks[index].run;
    QPointer<FsntDiagnosticsDialog> self = this;
    runOnNewThread([self, index, run] {
        const auto result = run();
        runOnUiThread([self, index, result] {
            // Окно могли закрыть, пока проверка ходила в сеть.
            if (self.isNull()) return;
            self->applyResult(index, result);
            self->m_current = index + 1;
            self->runNext();
        });
    });
}

void FsntDiagnosticsDialog::applyResult(const int index, const Fsnt::CheckResult &result) {
    if (index < 0 || index >= m_rows.size()) return;
    Row &row = m_rows[index];

    row.status->setText(diagStateText(result.state));
    row.status->setProperty("tone", diagStateTone(result.state));
    row.status->style()->unpolish(row.status);
    row.status->style()->polish(row.status);

    if (!result.detail.isEmpty()) {
        row.detail->setText(result.detail);
        row.detail->show();
    }

    if (!result.fixLabel.isEmpty() && result.fix) {
        row.fix->setText(result.fixLabel);
        row.fix->show();
        const auto fix = result.fix;
        connect(row.fix, &QPushButton::clicked, this, [this, fix] {
            fix();
            // После починки прогоняем всё заново: одно исправление часто
            // снимает и соседние красные строки.
            m_again->click();
        }, Qt::UniqueConnection);
    }
}

void FsntDiagnosticsDialog::finish() {
    m_again->setEnabled(true);

    int problems = 0;
    int warnings = 0;
    for (const auto &row : m_rows) {
        const QString tone = row.status->property("tone").toString();
        if (tone == QLatin1String("bad")) ++problems;
        if (tone == QLatin1String("busy")) ++warnings;
    }

    if (problems > 0) {
        m_summary->setText(tr("Found %1 problem(s). Fix the topmost one first: the checks run "
                              "from cause to effect, and the rest are often its consequences.")
                               .arg(problems));
        return;
    }
    if (warnings > 0) {
        m_summary->setText(tr("No breakage found, but %1 thing(s) are worth a look.").arg(warnings));
        return;
    }
    m_summary->setText(tr("Everything checks out. If something still does not work, build a "
                          "support report from the menu and send it over."));
}
