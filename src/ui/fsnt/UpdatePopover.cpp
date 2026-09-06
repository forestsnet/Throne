#include "include/ui/fsnt/UpdatePopover.hpp"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include "include/ui/fsnt/FsntPalette.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"

namespace {
    constexpr int kWidth = 340;
    constexpr int kShadow = 16;   // поле под тень вокруг карточки
    constexpr int kRadius = 16;
    constexpr int kGap = 8;       // просвет между значком и карточкой
}

namespace Fsnt {
    UpdatePopover::UpdatePopover(QWidget *anchor, const QString &title, const QString &text)
        : QWidget(anchor, Qt::Popup), m_anchor(anchor) {
        // Qt::Popup сам закрывается по щелчку мимо и по Esc — ровно то поведение,
        // которого ждут от карточки под кнопкой.
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        setStyleSheet(Fsnt::BuildStyleSheet());
        setFixedWidth(kWidth + kShadow * 2);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(kShadow + 18, kShadow + 16, kShadow + 18, kShadow + 16);
        layout->setSpacing(6);

        auto *heading = new QLabel(title, this);
        heading->setObjectName(QStringLiteral("fsntPopoverTitle"));
        heading->setWordWrap(true);
        layout->addWidget(heading);

        auto *body = new QLabel(text, this);
        body->setObjectName(QStringLiteral("fsntDialogHint"));
        body->setWordWrap(true);
        layout->addWidget(body);
        layout->addSpacing(8);

        auto *row = new QHBoxLayout;
        row->setSpacing(8);

        auto *update = new QPushButton(tr("Update"), this);
        update->setObjectName(QStringLiteral("fsntPrimary"));
        update->setCursor(Qt::PointingHandCursor);
        connect(update, &QPushButton::clicked, this, [this] {
            emit updateRequested();
            close();
        });
        row->addWidget(update);

        auto *later = new QPushButton(tr("Later"), this);
        later->setObjectName(QStringLiteral("fsntGhost"));
        later->setCursor(Qt::PointingHandCursor);
        connect(later, &QPushButton::clicked, this, [this] {
            emit postponed();
            close();
        });
        row->addWidget(later);
        row->addStretch();

        // Ссылкой, а не третьей кнопкой: список изменений — дело добровольное,
        // и в ряду равных кнопок он тянул бы внимание наравне с «Обновить».
        auto *notes = new QPushButton(tr("What's new"), this);
        notes->setObjectName(QStringLiteral("fsntLinkButton"));
        notes->setCursor(Qt::PointingHandCursor);
        notes->setFlat(true);
        connect(notes, &QPushButton::clicked, this, [this] {
            emit notesRequested();
            close();
        });
        row->addWidget(notes);

        layout->addLayout(row);
    }

    void UpdatePopover::popup() {
        adjustSize();
        if (m_anchor != nullptr) {
            // Правый край карточки идёт по правому краю значка: так она висит
            // под ним, а не лезет за границу окна.
            QPoint corner = m_anchor->mapToGlobal(QPoint(m_anchor->width(), m_anchor->height()));
            QPoint target(corner.x() - width() + kShadow, corner.y() - kShadow + kGap);
            if (const QScreen *screen = QGuiApplication::screenAt(corner)) {
                const QRect area = screen->availableGeometry();
                target.setX(qBound(area.left(), target.x(), area.right() - width()));
            }
            move(target);
        }
        show();
    }

    void UpdatePopover::paintEvent(QPaintEvent *) {
        const auto palette = Fsnt::CurrentPalette();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QRectF card(kShadow, kShadow, width() - kShadow * 2, height() - kShadow * 2);

        painter.setPen(Qt::NoPen);
        for (int layer = kShadow; layer > 0; layer -= 3) {
            painter.setBrush(QColor(0, 0, 0, 7));
            painter.drawRoundedRect(card.adjusted(-layer, -layer + 2, layer, layer + 2),
                                    kRadius + layer, kRadius + layer);
        }

        painter.setBrush(palette.surface);
        painter.setPen(QPen(palette.border, 1));
        painter.drawRoundedRect(card, kRadius, kRadius);
    }
} // namespace Fsnt
