#include "include/ui/fsnt/BusyDialog.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "include/ui/fsnt/FsntTheme.hpp"

FsntBusyDialog::FsntBusyDialog(const QString &title, const QString &hint, const int steps, QWidget *parent)
    : QDialog(parent), m_steps(steps > 0 ? steps : 1) {
    setObjectName("fsntDialog");
    setWindowTitle(title);
    setModal(true);
    // Ширина фиксирована, чтобы перенос был предсказуем и высоту можно было
    // посчитать заранее.
    setFixedWidth(460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(10);

    auto *heading = new QLabel(title, this);
    heading->setObjectName("fsntDialogTitle");
    layout->addWidget(heading);

    if (!hint.isEmpty()) {
        auto *note = new QLabel(hint, this);
        note->setObjectName("fsntDialogHint");
        note->setWordWrap(true);
        layout->addWidget(note);
    }

    layout->addSpacing(4);

    m_bar = new QProgressBar(this);
    m_bar->setObjectName("fsntBusyBar");
    m_bar->setRange(0, m_steps);
    m_bar->setValue(0);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(6);
    layout->addWidget(m_bar);

    m_stage = new QLabel(this);
    m_stage->setObjectName("fsntDialogHint");
    m_stage->setWordWrap(true);
    // Путь к архиву человек диктует в поддержку — пусть его можно выделить.
    m_stage->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_stage);

    layout->addSpacing(6);

    m_reveal = new QPushButton(tr("Show the file"), this);
    m_reveal->setObjectName("fsntGhost");
    m_reveal->setCursor(Qt::PointingHandCursor);
    m_reveal->hide();

    // Кнопка живёт с самого начала. Сбор отчёта на Windows — это под минуту
    // жёстких таймаутов подряд, и окно без выхода всё это время стояло поверх
    // всего: крестик и Escape шли в заблокированный reject, а показать эту
    // кнопку мог только конец работы. Отменить сам сбор нечем, но прятать
    // окно человек вправе — итог его дождётся сообщением.
    m_close = new QPushButton(tr("Hide"), this);
    m_close->setObjectName("fsntPrimary");
    m_close->setCursor(Qt::PointingHandCursor);
    m_close->setDefault(true);
    connect(m_close, &QPushButton::clicked, this, &QDialog::accept);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_reveal);
    buttons->addWidget(m_close);
    layout->addLayout(buttons);

    setStyleSheet(Fsnt::BuildStyleSheet());
    fitHeight();
}

void FsntBusyDialog::fitHeight() {
    if (layout() == nullptr) return;
    layout()->activate();
    const int wanted = layout()->heightForWidth(width());
    // Только вверх: подписи шагов короткие, и окно, дышащее на каждом шаге,
    // раздражает сильнее, чем лишние полсантиметра пустоты.
    if (wanted > height()) resize(width(), wanted);
}

void FsntBusyDialog::setStage(const QString &text) {
    m_stage->setText(text);
    // Подпись меняется на начале шага, а полоса показывает сделанное: пока
    // идёт первый шаг, она пуста, и это честно.
    m_bar->setValue(qMin(m_done, m_steps));
    ++m_done;
    fitHeight();
}

void FsntBusyDialog::settle(const QString &text, const bool ok) {
    m_settled = true;
    m_bar->setValue(ok ? m_steps : m_bar->value());
    m_stage->setText(text);
    m_close->setText(tr("Close"));
    m_close->show();
    m_close->setFocus();
    fitHeight();
}

void FsntBusyDialog::succeed(const QString &text, const QString &openPath) {
    settle(text, true);
    if (openPath.isEmpty()) return;

    const QFileInfo info(openPath);

    // Открываем папку, а не файл: архив незачем кому-то показывать внутри, его
    // надо найти и приложить к письму.
    const QString folder = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    m_reveal->show();
    connect(m_reveal, &QPushButton::clicked, this,
            [folder] { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
    fitHeight();
}

void FsntBusyDialog::fail(const QString &text) {
    settle(text, false);
}
