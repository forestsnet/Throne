#include "include/ui/fsnt/FsntControls.h"

#include <QAbstractItemView>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QVariantAnimation>

#include "include/ui/fsnt/FsntPalette.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"

namespace {
    constexpr int kSwitchWidth = 46;
    constexpr int kSwitchHeight = 26;
    constexpr int kSwitchPadding = 3;
    constexpr int kSlideMs = 160;

    constexpr int kSelectHeight = 38;
    constexpr int kSelectPadding = 12;
    constexpr int kPopupRowHeight = 34;

    // Галочка рисуется путём, а не шрифтом: символ ✓ в разных системах разной
    // ширины и по-разному сидит по вертикали.
    void drawCheck(QPainter *painter, const QPointF &centre, const qreal size, const QColor &color) {
        QPainterPath path;
        path.moveTo(centre.x() - size * 0.45, centre.y() + size * 0.02);
        path.lineTo(centre.x() - size * 0.12, centre.y() + size * 0.34);
        path.lineTo(centre.x() + size * 0.46, centre.y() - size * 0.34);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(path);
    }

    void drawChevron(QPainter *painter, const QPointF &centre, const qreal size, const QColor &color) {
        QPainterPath path;
        path.moveTo(centre.x() - size, centre.y() - size * 0.45);
        path.lineTo(centre.x(), centre.y() + size * 0.45);
        path.lineTo(centre.x() + size, centre.y() - size * 0.45);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(path);
    }

    // Строка выпадающего списка: скруглённая подсветка и галочка у текущего.
    class PopupDelegate final : public QStyledItemDelegate {
    public:
        using QStyledItemDelegate::QStyledItemDelegate;

        QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
            Q_UNUSED(option)
            Q_UNUSED(index)
            return {0, kPopupRowHeight};
        }

        void paint(QPainter *painter, const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override {
            const Fsnt::Palette p = Fsnt::CurrentPalette();
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);

            const QRect row = option.rect.adjusted(4, 1, -4, -1);
            const bool active = option.state & (QStyle::State_Selected | QStyle::State_MouseOver);
            if (active) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(p.accentSoft);
                painter->drawRoundedRect(row, 7, 7);
            }

            const bool current = index.data(Qt::UserRole + 77).toBool();
            const QRect textRect = row.adjusted(12, 0, -30, 0);
            painter->setFont(option.font);
            painter->setPen(p.text);
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                              QFontMetrics(option.font).elidedText(
                                  index.data(Qt::DisplayRole).toString(),
                                  Qt::ElideRight, textRect.width()));

            if (current) {
                drawCheck(painter, QPointF(row.right() - 15, row.center().y() + 1), 9, p.accent);
            }
            painter->restore();
        }
    };
}

// ------------------------------------------------------------------ FsntSwitch

FsntSwitch::FsntSwitch(QWidget *parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setAttribute(Qt::WA_Hover, true);

    m_slide = new QVariantAnimation(this);
    m_slide->setDuration(kSlideMs);
    m_slide->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_slide, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_position = value.toReal();
        update();
    });

    connect(this, &QAbstractButton::toggled, this, [this](const bool on) {
        m_slide->stop();
        m_slide->setStartValue(m_position);
        m_slide->setEndValue(on ? 1.0 : 0.0);
        m_slide->start();
    });
}

QSize FsntSwitch::sizeHint() const {
    return {kSwitchWidth, kSwitchHeight};
}

void FsntSwitch::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    const Fsnt::Palette p = Fsnt::CurrentPalette();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF track(0, (height() - kSwitchHeight) / 2.0, kSwitchWidth, kSwitchHeight);
    const qreal radius = track.height() / 2.0;

    QColor off = p.dark ? p.cardHover : p.border;
    if (m_hovered) off = off.lighter(p.dark ? 118 : 96);
    // Цвет дорожки смешиваем вручную: так тумблер догоняет ползунок,
    // а не перекрашивается скачком в конце анимации.
    const QColor trackColor = QColor::fromRgbF(
        off.redF() + (p.accent.redF() - off.redF()) * m_position,
        off.greenF() + (p.accent.greenF() - off.greenF()) * m_position,
        off.blueF() + (p.accent.blueF() - off.blueF()) * m_position);

    painter.setPen(Qt::NoPen);
    painter.setBrush(trackColor);
    painter.drawRoundedRect(track, radius, radius);

    const qreal knobSize = kSwitchHeight - kSwitchPadding * 2;
    const qreal travel = kSwitchWidth - knobSize - kSwitchPadding * 2;
    const QRectF knob(track.left() + kSwitchPadding + travel * m_position,
                      track.top() + kSwitchPadding, knobSize, knobSize);

    painter.setBrush(QColor("#FFFFFF"));
    painter.drawEllipse(knob);
}

void FsntSwitch::enterEvent(QEnterEvent *event) {
    Q_UNUSED(event)
    m_hovered = true;
    update();
}

void FsntSwitch::leaveEvent(QEvent *event) {
    Q_UNUSED(event)
    m_hovered = false;
    update();
}

// ------------------------------------------------------------------ FsntSelect

FsntSelect::FsntSelect(QWidget *parent) : QComboBox(parent) {
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);

    auto *list = new QListView(this);
    list->setUniformItemSizes(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    setView(list);
    view()->setItemDelegate(new PopupDelegate(view()));

    // Непустой лист обязателен: на macOS QComboBox без него открывает нативное
    // меню, мимо нашего делегата и палитры.
    setStyleSheet(QStringLiteral("FsntSelect { border: 0; }"));
}

QSize FsntSelect::sizeHint() const {
    return {qMax(160, QComboBox::sizeHint().width() + 40), kSelectHeight};
}

void FsntSelect::showPopup() {
    // Делегат не знает, какой пункт выбран: State_Selected в списке означает
    // подсветку под курсором. Помечаем текущий отдельной ролью.
    for (int i = 0; i < count(); ++i) {
        setItemData(i, i == currentIndex(), Qt::UserRole + 77);
    }

    const Fsnt::Palette p = Fsnt::CurrentPalette();
    view()->setStyleSheet(QString(
        "QAbstractItemView { background: %1; border: 1px solid %2; border-radius: %3px;"
        " outline: none; padding: 4px; }")
        .arg(p.card.name(QColor::HexRgb), p.border.name(QColor::HexRgb))
        .arg(Fsnt::kRowRadius));

    QComboBox::showPopup();

    // Qt ставит список поверх текущего пункта — привычка нативного меню macOS.
    // Роняем его под поле: так это читается как список поля, а не как меню,
    // и поле остаётся видно.
    if (auto *container = view()->window(); container != nullptr && container != window()) {
        // У контейнера своя рамка поверх нашей: гасим, скругление даёт сам список.
        container->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        container->resize(qMax(width(), container->width()), container->height());
        container->move(mapToGlobal(QPoint(0, height() + 4)));
    }
}

void FsntSelect::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    const Fsnt::Palette p = Fsnt::CurrentPalette();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setBrush(p.card);
    painter.setPen(QPen(m_hovered || hasFocus() ? p.accent : p.border, 1.0));
    painter.drawRoundedRect(frame, Fsnt::kRowRadius, Fsnt::kRowRadius);

    const QRect textRect = rect().adjusted(kSelectPadding, 0, -(kSelectPadding + 20), 0);
    painter.setPen(p.text);
    painter.setFont(font());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                     QFontMetrics(font()).elidedText(currentText(), Qt::ElideRight,
                                                     textRect.width()));

    drawChevron(&painter, QPointF(rect().right() - kSelectPadding - 4, rect().center().y() + 1),
                5, p.textMuted);
}

void FsntSelect::enterEvent(QEnterEvent *event) {
    Q_UNUSED(event)
    m_hovered = true;
    update();
}

void FsntSelect::leaveEvent(QEvent *event) {
    Q_UNUSED(event)
    m_hovered = false;
    update();
}
