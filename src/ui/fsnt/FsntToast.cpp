#include "include/ui/fsnt/FsntToast.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

#include "include/ui/fsnt/FsntControls.h"
#include "include/ui/fsnt/FsntPalette.hpp"

namespace {
    constexpr int kHeight = 46;
    constexpr int kRadius = 12;
    constexpr int kSidePadding = 16;
    constexpr int kCloseSide = 24;
    constexpr qreal kBorderWidth = 2.0;
    constexpr int kTopMargin = 12;
    constexpr int kMaxWidth = 520;
    constexpr int kTickMs = 40;
}

FsntToast::FsntToast(QWidget *parent) : QWidget(parent) {
    setVisible(false);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setMouseTracking(true);
    setFixedHeight(kHeight);

    m_countdown = new QVariantAnimation(this);
    m_countdown->setStartValue(0.0);
    m_countdown->setEndValue(1.0);
    // Кадры редкие: рамка укорачивается плавно и на 25 к/с, а чаще будить
    // перерисовку окна ради уведомления незачем.
    m_countdown->setDuration(5000);
    connect(m_countdown, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_progress = value.toReal();
        update();
    });
    connect(m_countdown, &QVariantAnimation::finished, this, [this] { QWidget::hide(); });
}

void FsntToast::show(const QString &text, const int milliseconds) {
    m_text = text;
    m_progress = 0.0;
    relayout();

    m_countdown->stop();
    m_countdown->setDuration(qMax(kTickMs, milliseconds));
    m_countdown->start();

    raise();
    QWidget::show();
    update();
}

void FsntToast::relayout() {
    if (parentWidget() == nullptr) return;

    const QFontMetrics fm(font());
    const int wanted = fm.horizontalAdvance(m_text) + kSidePadding * 2 + kCloseSide + 12;
    const int width = qBound(220, wanted, qMin(kMaxWidth, parentWidget()->width() - 40));

    setFixedWidth(width);
    move((parentWidget()->width() - width) / 2, kTopMargin);
}

QRect FsntToast::closeRect() const {
    return {width() - kSidePadding - kCloseSide, (height() - kCloseSide) / 2, kCloseSide, kCloseSide};
}

void FsntToast::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    const Fsnt::Palette p = Fsnt::CurrentPalette();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card = QRectF(rect()).adjusted(kBorderWidth, kBorderWidth,
                                                -kBorderWidth, -kBorderWidth);

    painter.setPen(Qt::NoPen);
    painter.setBrush(p.card);
    painter.drawRoundedRect(card, kRadius, kRadius);

    // Рамка-отсчёт. Частичный контур скруглённого прямоугольника рисуем штриховым
    // пером: длина штриха — остаток, длина пропуска — всё остальное. Считать
    // точки пути вручную здесь незачем.
    QPainterPath ring;
    ring.addRoundedRect(card, kRadius, kRadius);
    const qreal perimeter = ring.length();
    const qreal remaining = perimeter * (1.0 - m_progress);

    if (remaining > 1.0) {
        // Цвет гаснет от акцента к границе: время видно и краем глаза, без
        // считывания длины.
        const qreal fade = m_progress;
        const QColor tint = QColor::fromRgbF(
            p.accent.redF() + (p.border.redF() - p.accent.redF()) * fade,
            p.accent.greenF() + (p.border.greenF() - p.accent.greenF()) * fade,
            p.accent.blueF() + (p.border.blueF() - p.accent.blueF()) * fade);

        QPen pen(tint, kBorderWidth, Qt::CustomDashLine, Qt::FlatCap);
        // Шаблон задаётся в толщинах пера, отсюда деление.
        pen.setDashPattern({remaining / kBorderWidth,
                            qMax(0.01, (perimeter - remaining) / kBorderWidth)});
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(ring);
    }

    const QRect textRect(kSidePadding, 0,
                         width() - kSidePadding * 2 - kCloseSide - 8, height());
    painter.setPen(p.text);
    painter.setFont(font());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                     QFontMetrics(font()).elidedText(m_text, Qt::ElideRight, textRect.width()));

    const QRect close = closeRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_closeHovered ? p.danger : QColor(p.danger.red(), p.danger.green(),
                                                        p.danger.blue(), 46));
    painter.drawEllipse(close);
    Fsnt::PaintGlyph(&painter, Fsnt::Glyph::Close, QRectF(close).adjusted(7, 7, -7, -7),
                     m_closeHovered ? QColor("#FFFFFF") : p.danger);
}

void FsntToast::mousePressEvent(QMouseEvent *event) {
    if (closeRect().contains(event->pos())) {
        m_countdown->stop();
        QWidget::hide();
        return;
    }
    QWidget::mousePressEvent(event);
}

void FsntToast::mouseMoveEvent(QMouseEvent *event) {
    const bool hovered = closeRect().contains(event->pos());
    if (hovered != m_closeHovered) {
        m_closeHovered = hovered;
        setCursor(hovered ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void FsntToast::leaveEvent(QEvent *event) {
    Q_UNUSED(event)
    m_closeHovered = false;
    update();
}
