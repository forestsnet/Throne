#include "include/ui/fsnt/SettingsCard.h"

#include "include/ui/fsnt/FsntControls.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace {
    constexpr int kCardRowHeight = 44;
    constexpr int kCardSidePadding = 14;

    QWidget *makeRow(QWidget *parent, QHBoxLayout **layout) {
        auto *row = new QWidget(parent);
        row->setMinimumHeight(kCardRowHeight);
        auto *box = new QHBoxLayout(row);
        box->setContentsMargins(kCardSidePadding, 6, kCardSidePadding, 6);
        box->setSpacing(12);
        *layout = box;
        return row;
    }
}

namespace Fsnt {
    SettingsCard::SettingsCard(QVBoxLayout *column, QWidget *host, const QString &title) {
        auto *caption = new QLabel(title, host);
        caption->setObjectName("fsntSectionLabel");
        column->addWidget(caption);

        m_card = new QWidget(host);
        m_card->setObjectName("fsntCard");
        m_rows = new QVBoxLayout(m_card);
        m_rows->setContentsMargins(0, 0, 0, 0);
        m_rows->setSpacing(0);
        column->addWidget(m_card);
    }

    void SettingsCard::addSeparator() {
        if (m_empty) {
            m_empty = false;
            return;
        }
        auto *line = new QFrame(m_card);
        line->setObjectName("fsntRowSeparator");
        line->setFrameShape(QFrame::HLine);
        line->setFixedHeight(1);
        // Разделитель не доходит до края: так он читается как продолжение
        // подписи, а не как рассечение карточки надвое.
        auto *holder = new QWidget(m_card);
        auto *box = new QHBoxLayout(holder);
        box->setContentsMargins(kCardSidePadding, 0, 0, 0);
        box->setSpacing(0);
        box->addWidget(line);
        holder->setFixedHeight(1);
        m_rows->addWidget(holder);
    }

    QWidget *SettingsCard::addControl(const QString &label, QWidget *control) {
        addSeparator();
        QHBoxLayout *box = nullptr;
        auto *row = makeRow(m_card, &box);

        auto *text = new QLabel(label, row);
        text->setObjectName("fsntRowLabel");
        box->addWidget(text);
        box->addStretch();

        control->setParent(row);
        box->addWidget(control);
        m_rows->addWidget(row);
        return row;
    }

    FsntSwitch *SettingsCard::addToggle(const QString &label, const bool checked) {
        addSeparator();
        QHBoxLayout *box = nullptr;
        auto *row = makeRow(m_card, &box);

        auto *text = new QLabel(label, row);
        text->setObjectName("fsntRowLabel");
        text->setWordWrap(true);
        box->addWidget(text, 1);

        auto *check = new FsntSwitch(row);
        check->setChecked(checked);
        box->addWidget(check);

        m_rows->addWidget(row);
        return check;
    }

    QPushButton *SettingsCard::addAction(const QString &label) {
        addSeparator();
        auto *button = new QPushButton(label, m_card);
        button->setObjectName("fsntRowAction");
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(kCardRowHeight);
        m_rows->addWidget(button);
        return button;
    }

    QLabel *SettingsCard::addNote(const QString &text) {
        addSeparator();
        auto *note = new QLabel(text, m_card);
        note->setObjectName("fsntRowNote");
        note->setWordWrap(true);
        note->setContentsMargins(kCardSidePadding, 10, kCardSidePadding, 12);
        m_rows->addWidget(note);
        return note;
    }
} // namespace Fsnt
