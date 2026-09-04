#include "include/ui/fsnt/BusyButton.h"

#include <QPainter>
#include <QVariantAnimation>

#include "include/ui/fsnt/FsntPalette.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"

namespace {
    constexpr int kSpinPeriodMs = 900;
    constexpr int kArcSpanDeg = 100;
    constexpr qreal kArcWidth = 2.0;
    constexpr int kSide = 36;
}

BusyButton::BusyButton(const Fsnt::Glyph glyph, QWidget *parent)
    : QAbstractButton(parent), m_glyph(glyph) {
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::NoFocus);

    m_spinAnim = new QVariantAnimation(this);
    m_spinAnim->setStartValue(0.0);
    m_spinAnim->setEndValue(360.0);
    m_spinAnim->setDuration(kSpinPeriodMs);
    m_spinAnim->setLoopCount(-1);
    connect(m_spinAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_spin = v.toReal();
        update();
    });
}

QSize BusyButton::sizeHint() const {
    return {kSide, kSide};
}

void BusyButton::setBusy(const bool busy) {
    if (m_busy == busy) return;
    m_busy = busy;

    // Повторный запуск во время работы только сбил бы уже идущий замер.
    setEnabled(!busy);

    if (busy) m_spinAnim->start();
    else m_spinAnim->stop();

    update();
}

void BusyButton::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    const Fsnt::Palette p = Fsnt::CurrentPalette();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setBrush(p.card);
    painter.setPen(QPen(m_hovered && !m_busy ? p.accent : p.border, 1.0));
    painter.drawRoundedRect(frame, Fsnt::kRowRadius, Fsnt::kRowRadius);

    if (!m_busy) {
        Fsnt::PaintGlyph(&painter, m_glyph, QRectF(rect()).adjusted(10, 10, -10, -10),
                         m_hovered ? p.accent : p.textMuted);
        return;
    }

    // Знак не рисуем вовсе: дуга и глиф наезжали бы друг на друга.
    const qreal radius = qMin(width(), height()) / 2.0 - 10.0;
    const QPointF centre(width() / 2.0, height() / 2.0);
    const QRectF ring(centre.x() - radius, centre.y() - radius, radius * 2, radius * 2);

    // drawArc считает в 1/16 градуса против часовой стрелки; минус разворачивает
    // бег по часовой, как ждёт глаз.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(p.accent, kArcWidth, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(ring, static_cast<int>(-m_spin * 16), -kArcSpanDeg * 16);
}

void BusyButton::enterEvent(QEnterEvent *event) {
    Q_UNUSED(event)
    m_hovered = true;
    update();
}

void BusyButton::leaveEvent(QEvent *event) {
    Q_UNUSED(event)
    m_hovered = false;
    update();
}
