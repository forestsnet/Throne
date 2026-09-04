#include "include/ui/fsnt/FsntLogDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

#include "include/global/Logger.hpp"
#include "include/ui/fsnt/FsntControls.h"
#include "include/ui/fsnt/FsntPalette.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"

namespace {
    // Сколько строк держим в памяти. Журнал за сеанс легко уходит в десятки
    // тысяч строк, а листать столько в окне всё равно никто не станет.
    constexpr int kMaxLines = 5000;

    // Слова, по которым строка считается проблемной. Уровня в пользовательском
    // журнале нет — он собирается из свободного текста ядра и интерфейса.
    const QStringList &problemWords() {
        static const QStringList words{
            "error", "fail", "warn", "crash", "invalid", "refused", "timeout",
            "denied", "unable", "ошибк", "сбой", "не удал",
        };
        return words;
    }
}

FsntLogDialog::FsntLogDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("fsntDialog");
    setWindowTitle(tr("Logs"));
    resize(760, 560);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(12);

    auto *title = new QLabel(tr("Logs"), this);
    title->setObjectName("fsntDialogTitle");
    layout->addWidget(title);

    m_filter = new QLineEdit(this);
    m_filter->setObjectName("fsntSearch");
    m_filter->setPlaceholderText(tr("Filter lines"));
    m_filter->setClearButtonEnabled(true);
    m_filter->addAction(Fsnt::GlyphIcon(Fsnt::Glyph::Search, 15, Fsnt::CurrentPalette().textMuted),
                        QLineEdit::LeadingPosition);
    layout->addWidget(m_filter);

    auto *toggles = new QHBoxLayout;
    toggles->setSpacing(10);

    auto *problemsLabel = new QLabel(tr("Problems only"), this);
    problemsLabel->setObjectName("fsntRowLabel");
    toggles->addWidget(problemsLabel);
    m_problemsOnly = new FsntSwitch(this);
    toggles->addWidget(m_problemsOnly);

    toggles->addSpacing(18);

    auto *scrollLabel = new QLabel(tr("Follow new lines"), this);
    scrollLabel->setObjectName("fsntRowLabel");
    toggles->addWidget(scrollLabel);
    m_autoScroll = new FsntSwitch(this);
    m_autoScroll->setChecked(true);
    toggles->addWidget(m_autoScroll);

    toggles->addStretch();
    layout->addLayout(toggles);

    m_view = new QPlainTextEdit(this);
    m_view->setObjectName("fsntLogView");
    m_view->setReadOnly(true);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_view->setMaximumBlockCount(kMaxLines);
    QFont mono("Menlo");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(11.5);
    m_view->setFont(mono);
    layout->addWidget(m_view, 1);

    // История сеанса: журнал держит кольцо последних строк, и без него окно
    // открывалось бы пустым ровно в тот момент, когда что-то уже случилось.
    m_lines = Logging::RecentLines(kMaxLines);
    rebuild();

    connect(m_filter, &QLineEdit::textChanged, this, &FsntLogDialog::rebuild);
    connect(m_problemsOnly, &FsntSwitch::toggled, this, &FsntLogDialog::rebuild);

    auto *copy = new QPushButton(tr("Copy"), this);
    copy->setObjectName("fsntGhost");
    copy->setCursor(Qt::PointingHandCursor);
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_view->toPlainText());
    });

    auto *clear = new QPushButton(tr("Clear"), this);
    clear->setObjectName("fsntGhost");
    clear->setCursor(Qt::PointingHandCursor);
    connect(clear, &QPushButton::clicked, this, [this] {
        m_lines.clear();
        rebuild();
    });

    auto *close = new QPushButton(tr("Close"), this);
    close->setObjectName("fsntPrimary");
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(copy);
    buttons->addWidget(clear);
    buttons->addStretch();
    buttons->addWidget(close);
    layout->addLayout(buttons);

    setStyleSheet(Fsnt::BuildStyleSheet());
}

bool FsntLogDialog::passes(const QString &line) const {
    if (m_problemsOnly->isChecked()) {
        bool hit = false;
        for (const QString &word : problemWords()) {
            if (line.contains(word, Qt::CaseInsensitive)) {
                hit = true;
                break;
            }
        }
        if (!hit) return false;
    }
    const QString needle = m_filter->text().trimmed();
    return needle.isEmpty() || line.contains(needle, Qt::CaseInsensitive);
}

void FsntLogDialog::appendLine(const QString &line) {
    m_lines << line;
    if (m_lines.size() > kMaxLines) m_lines.remove(0, m_lines.size() - kMaxLines);
    if (!passes(line)) return;

    // Прокрутку двигаем, только если пользователь сам не ушёл вверх читать:
    // иначе живой лог выдёргивал бы его из нужного места.
    const bool follow = m_autoScroll->isChecked();
    m_view->appendPlainText(line);
    if (follow) m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
}

void FsntLogDialog::rebuild() {
    m_view->clear();
    QStringList shown;
    for (const QString &line : m_lines) {
        if (passes(line)) shown << line;
    }
    m_view->setPlainText(shown.join('\n'));
    m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
}
