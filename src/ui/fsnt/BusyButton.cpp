#include "include/ui/fsnt/BusyButton.h"

#include <QPainter>
#include <QVariantAnimation>

#include "include/ui/fsnt/FsntPalette.hpp"

namespace {
    constexpr int kSpinPeriodMs = 900;
    constexpr int kArcSpanDeg = 100;
    constexpr qreal kArcWidth = 2.0;
}

BusyButton::BusyButton(const QString &glyph, QWidget *parent)
    : QPushButton(glyph, parent), m_glyph(glyph) {
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

void BusyButton::setBusy(const bool busy) {
    if (m_busy == busy) return;
    m_busy = busy;

    // Знак убираем, а не рисуем поверх: иначе дуга и глиф наезжают друг на друга.
    setText(busy ? QString() : m_glyph);
    // Повторный запуск во время работы только сбил бы уже идущий замер.
    setEnabled(!busy);

    if (busy) m_spinAnim->start();
    else m_spinAnim->stop();

    update();
}

void BusyButton::paintEvent(QPaintEvent *event) {
    QPushButton::paintEvent(event);
    if (!m_busy) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = qMin(width(), height()) / 2.0 - 8.0;
    const QPointF centre(width() / 2.0, height() / 2.0);
    const QRectF ring(centre.x() - radius, centre.y() - radius, radius * 2, radius * 2);

    // drawArc считает в 1/16 градуса против часовой стрелки; минус разворачивает
    // бег по часовой, как ждёт глаз.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Fsnt::CurrentPalette().accent, kArcWidth, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(ring, static_cast<int>(-m_spin * 16), -kArcSpanDeg * 16);
}
