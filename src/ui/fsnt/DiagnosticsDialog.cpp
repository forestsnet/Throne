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

    // Очередь и работа различаются не только словом, но и цветом: ожидание
    // идёт приглушённым, как и остальной второстепенный текст, идущая проверка
    // — акцентным.
    QString diagQueuedText() { return QObject::tr("in queue"); }

    QString diagRunningText() { return QObject::tr("checking…"); }

    void diagSetStatus(QLabel *label, const QString &text, const QString &tone) {
        label->setText(text);
        label->setProperty("tone", tone);
        label->style()->unpolish(label);
        label->style()->polish(label);
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
           "computer. Nothing is sent anywhere."), this);
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
        row.status = new QLabel(diagQueuedText(), card);
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

        // Кнопка подключается здесь и больше никогда: саму починку она берёт
        // из строки. Подключение заново на каждом прогоне копилось — для лямбд
        // Qt::UniqueConnection не действует, и после N прогонов одно нажатие
        // запускало починку N раз.
        const int index = m_rows.size() - 1;
        connect(m_rows[index].fix, &QPushButton::clicked, this, [this, index] { startFix(index); });
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
    connect(m_again, &QPushButton::clicked, this, &FsntDiagnosticsDialog::runAll);
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

void FsntDiagnosticsDialog::runAll() {
    // Пересобирать окно незачем: набор проверок тот же, меняются результаты.
    for (auto &row : m_rows) {
        diagSetStatus(row.status, diagQueuedText(), {});
        row.detail->hide();
        row.fix->hide();
        row.action = {};
    }
    m_summary->clear();
    m_again->setEnabled(false);
    // Прежняя цепочка могла остаться в сети: её запоздавшие результаты
    // отличаются номером прогона и в строки уже не попадут.
    ++m_run;
    m_current = 0;
    runNext();
}

void FsntDiagnosticsDialog::runNext() {
    if (m_current >= m_checks.size()) {
        finish();
        return;
    }

    const int index = m_current;
    // Здесь, а не при сборке окна: проверки идут по одной, каждая следующая
    // стартует из колбэка предыдущей, и до этой секунды строка ничем не
    // занята.
    diagSetStatus(m_rows[index].status, diagRunningText(), QStringLiteral("busy"));

    auto run = m_checks[index].run;
    const int runId = m_run;
    QPointer<FsntDiagnosticsDialog> self = this;
    runOnNewThread([self, index, run, runId] {
        const auto result = run();
        runOnUiThread([self, index, result, runId] {
            // Окно могли закрыть, пока проверка ходила в сеть.
            if (self.isNull()) return;
            // Отменённый прогон: починка успела перезапустить проверки, пока
            // эта ещё ждала ответа.
            if (self->m_run != runId) return;
            self->applyResult(index, result);
            self->m_current = index + 1;
            self->runNext();
        });
    });
}

void FsntDiagnosticsDialog::applyResult(const int index, const Fsnt::CheckResult &result) {
    if (index < 0 || index >= m_rows.size()) return;
    Row &row = m_rows[index];

    diagSetStatus(row.status, diagStateText(result.state), diagStateTone(result.state));

    if (!result.detail.isEmpty()) {
        row.detail->setText(result.detail);
        row.detail->show();
    }

    row.action = result.fix;
    if (!result.fixLabel.isEmpty() && result.fix) {
        row.fix->setText(result.fixLabel);
        row.fix->show();
    }
}

void FsntDiagnosticsDialog::startFix(const int index) {
    if (index < 0 || index >= m_rows.size()) return;
    // Две починки разом свели бы перезапуск ядра с перестановкой флагов.
    if (m_fixing) return;

    // Своей копией: runAll чистит починку в строке, а done может прийти
    // синхронно — функтор не должен умереть посреди собственного вызова.
    const auto action = m_rows[index].action;
    if (!action) return;

    const QString label = m_rows[index].fix->text();
    m_rows[index].fix->setEnabled(false);
    m_rows[index].fix->setText(tr("Working…"));
    // Пока починка не доложила о конце, прогон проверок — её дело: кнопка,
    // нажатая посреди починки, переписала бы строку, которую та ещё меняет.
    m_fixing = true;
    m_again->setEnabled(false);

    // Перезапуск проверок — только после того, как починка доложит о
    // завершении. Подбор DNS отвечает через секунды, перезапуск профиля — через
    // десяток, и запуск проверок сразу по нажатию читал ещё старое состояние:
    // строка снова краснела, а человек видел кнопку, которая ничего не делает.
    QPointer<FsntDiagnosticsDialog> self = this;
    action([self, index, label] {
        if (self.isNull()) return;
        Row &row = self->m_rows[index];
        row.fix->setEnabled(true);
        row.fix->setText(label);
        self->m_fixing = false;
        self->runAll();
    });
}

void FsntDiagnosticsDialog::finish() {
    // Починку можно нажать, пока цепочка ещё идёт: тогда её конец и перезапустит
    // проверки, а своя кнопка до тех пор остаётся выключенной.
    m_again->setEnabled(!m_fixing);

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
