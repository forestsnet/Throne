#include "include/ui/fsnt/UpdatePopover.hpp"

#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
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
        // которого ждут от карточки под кнопкой. Системную тень гасим: macOS
        // рисует её по прямоугольнику всего окна, вместе с прозрачными полями,
        // и вокруг карточки появляется отчётливая тёмная кайма.
        setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        setStyleSheet(Fsnt::BuildStyleSheet());
        setFixedWidth(kWidth + kShadow * 2);

        // Карточка живёт отдельным виджетом: настоящую размытую тень умеет только
        // графический эффект, а нарисованные вручную кольца на тёмном фоне читаются
        // как лишняя рамка вокруг окна.
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(kShadow, kShadow, kShadow, kShadow);

        auto *card = new QFrame(this);
        card->setObjectName(QStringLiteral("fsntPopoverCard"));
        outer->addWidget(card);

        auto *shadow = new QGraphicsDropShadowEffect(card);
        shadow->setBlurRadius(34);
        shadow->setOffset(0, 8);
        shadow->setColor(QColor(0, 0, 0, Fsnt::CurrentPalette().dark ? 150 : 60));
        card->setGraphicsEffect(shadow);

        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(18, 16, 18, 16);
        layout->setSpacing(6);

        auto *heading = new QLabel(title, card);
        heading->setObjectName(QStringLiteral("fsntPopoverTitle"));
        heading->setWordWrap(true);
        layout->addWidget(heading);

        auto *body = new QLabel(text, card);
        body->setObjectName(QStringLiteral("fsntDialogHint"));
        body->setWordWrap(true);
        layout->addWidget(body);
        layout->addSpacing(8);

        auto *row = new QHBoxLayout;
        row->setSpacing(8);

        auto *update = new QPushButton(tr("Update"), card);
        update->setObjectName(QStringLiteral("fsntPrimary"));
        update->setCursor(Qt::PointingHandCursor);
        connect(update, &QPushButton::clicked, this, [this] {
            emit updateRequested();
            close();
        });
        row->addWidget(update);

        auto *later = new QPushButton(tr("Later"), card);
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
        auto *notes = new QPushButton(tr("What's new"), card);
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

} // namespace Fsnt
