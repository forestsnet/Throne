#include "include/ui/fsnt/PowerButton.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QVariantAnimation>

#include "include/ui/fsnt/FsntPalette.hpp"

namespace {
    constexpr int kSide = 176;      // сторона виджета
    constexpr qreal kRingWidth = 5.0;
    constexpr qreal kGlyphRadius = 21.0;
    constexpr int kSpinPeriodMs = 1400;
    constexpr int kPulsePeriodMs = 2400;
    // Длина бегущей дуги при подключении, в градусах.
    constexpr int kArcSpanDeg = 110;
}

PowerButton::PowerButton(QWidget *parent) : QWidget(parent) {
    setFixedSize(kSide, kSide);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);

    m_spinAnim = new QVariantAnimation(this);
    m_spinAnim->setStartValue(0.0);
    m_spinAnim->setEndValue(360.0);
    m_spinAnim->setDuration(kSpinPeriodMs);
    m_spinAnim->setLoopCount(-1);
    connect(m_spinAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_spin = v.toReal();
        update();
    });

    m_pulseAnim = new QVariantAnimation(this);
    m_pulseAnim->setStartValue(0.0);
    m_pulseAnim->setEndValue(1.0);
    m_pulseAnim->setDuration(kPulsePeriodMs);
    m_pulseAnim->setEasingCurve(QEasingCurve::InOutSine);
    m_pulseAnim->setLoopCount(-1);
    connect(m_pulseAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        // Треугольная волна: анимация идёт 0->1, а свечение должно дышать туда и обратно.
        const qreal x = v.toReal();
        m_pulse = x < 0.5 ? x * 2.0 : (1.0 - x) * 2.0;
        update();
    });
}

void PowerButton::setState(const State state) {
    if (m_state == state) return;
    m_state = state;
    retuneAnimations();
    update();
}

void PowerButton::retuneAnimations() {
    const bool spinning = m_state == State::Connecting || m_state == State::Stopping;
    const bool pulsing = m_state == State::Connected;

    if (spinning && m_spinAnim->state() != QAbstractAnimation::Running) {
        m_spinAnim->start();
    } else if (!spinning && m_spinAnim->state() == QAbstractAnimation::Running) {
        m_spinAnim->stop();
        m_spin = 0.0;
    }

    if (pulsing && m_pulseAnim->state() != QAbstractAnimation::Running) {
        m_pulseAnim->start();
    } else if (!pulsing && m_pulseAnim->state() == QAbstractAnimation::Running) {
        m_pulseAnim->stop();
        m_pulse = 0.0;
    }
}

QColor PowerButton::stateColor() const {
    const auto p = Fsnt::CurrentPalette();
    switch (m_state) {
        case State::Connected: return p.success;
        case State::Connecting:
        case State::Stopping: return p.accent;
        case State::Off: break;
    }
    return m_hovered ? p.accent : p.textMuted;
}

void PowerButton::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    const auto p = Fsnt::CurrentPalette();
    const QColor tint = stateColor();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPointF centre(width() / 2.0, height() / 2.0);
    // При нажатии кнопка чуть проседает — единственная обратная связь на клик,
    // пока ядро ещё не ответило сменой состояния.
    const qreal squeeze = m_pressed ? 0.97 : 1.0;
    const qreal outer = (kSide / 2.0 - 12.0) * squeeze;

    // Свечение: заметно только когда есть что показывать, иначе фон грязнится.
    if (m_state != State::Off || m_hovered) {
        const qreal strength = m_state == State::Connected ? 0.35 + 0.25 * m_pulse
                             : m_state == State::Off       ? 0.18
                                                           : 0.30;
        QRadialGradient glow(centre, outer * 1.55);
        QColor inner = tint;
        inner.setAlphaF(strength);
        glow.setColorAt(0.55, Qt::transparent);
        glow.setColorAt(0.80, inner);
        glow.setColorAt(1.0, Qt::transparent);
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        painter.drawEllipse(centre, outer * 1.55, outer * 1.55);
    }

    const QRectF ring(centre.x() - outer, centre.y() - outer, outer * 2, outer * 2);

    // Дорожка кольца — всегда целиком, чтобы бегущая дуга читалась как прогресс.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(p.border, kRingWidth, Qt::SolidLine, Qt::RoundCap));
    painter.drawEllipse(ring);

    QPen activePen(tint, kRingWidth, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(activePen);
    switch (m_state) {
        case State::Connected:
            painter.drawEllipse(ring);
            break;
        case State::Connecting:
        case State::Stopping:
            // drawArc считает в 1/16 градуса и против часовой стрелки; знак минус
            // разворачивает бег дуги по часовой, как ждёт глаз.
            painter.drawArc(ring, static_cast<int>(-m_spin * 16), -kArcSpanDeg * 16);
            break;
        case State::Off:
            if (m_hovered) painter.drawEllipse(ring);
            break;
    }

    // Внутренний диск: отделяет знак от кольца и даёт кнопке объём.
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_hovered && m_state == State::Off ? p.cardHover : p.card);
    painter.drawEllipse(centre, outer - kRingWidth * 1.6, outer - kRingWidth * 1.6);

    // Знак питания: разомкнутая сверху окружность плюс вертикальная черта.
    const QRectF glyph(centre.x() - kGlyphRadius, centre.y() - kGlyphRadius + 2,
                       kGlyphRadius * 2, kGlyphRadius * 2);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(tint, 3.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(glyph, 125 * 16, 290 * 16);
    painter.drawLine(QPointF(centre.x(), centre.y() - kGlyphRadius - 4),
                     QPointF(centre.x(), centre.y() + 1));
}

void PowerButton::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_pressed = true;
    update();
}

void PowerButton::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const bool wasPressed = m_pressed;
    m_pressed = false;
    update();
    if (wasPressed && rect().contains(event->pos())) emit clicked();
}

void PowerButton::enterEvent(QEnterEvent *event) {
    Q_UNUSED(event)
    m_hovered = true;
    update();
}

void PowerButton::leaveEvent(QEvent *event) {
    Q_UNUSED(event)
    m_hovered = false;
    m_pressed = false;
    update();
}
