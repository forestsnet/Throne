#include "include/ui/fsnt/DesktopNotice.hpp"

#include <QCursor>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QVariantAnimation>

#include "include/ui/fsnt/FsntControls.h"
#include "include/ui/fsnt/FsntPalette.hpp"

namespace {
    constexpr int kNoticeWidth = 380;
    constexpr int kNoticeHeight = 96;
    constexpr int kShadow = 18;   // поле под тень: окно шире карточки на эту величину
    constexpr int kMargin = 20;   // отступ от края экрана

    QPointer<Fsnt::DesktopNotice> g_current;
}

namespace Fsnt {
    DesktopNotice::DesktopNotice(QWidget *parent) : QWidget(parent) {
        // Qt::Tool на маке прячется вместе с приложением, когда человек уходит в
        // другую программу, — а карточка нужна именно там. Обычное окно без рамки
        // остаётся на экране и не забирает фокус.
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                       Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_DeleteOnClose);
        setMouseTracking(true);
        setFixedSize(kNoticeWidth + kShadow * 2, kNoticeHeight + kShadow * 2);

        m_fade = new QVariantAnimation(this);
        m_fade->setDuration(180);
        connect(m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            setWindowOpacity(value.toReal());
        });

        m_life = new QVariantAnimation(this);
        m_life->setStartValue(0.0);
        m_life->setEndValue(1.0);
        connect(m_life, &QVariantAnimation::valueChanged, this, [this] { update(); });
        connect(m_life, &QVariantAnimation::finished, this, &DesktopNotice::dismiss);
    }

    DesktopNotice *DesktopNotice::Show(const QString &title, const QString &text, int milliseconds) {
        if (g_current.isNull()) g_current = new DesktopNotice();
        g_current->present(title, text, milliseconds);
        return g_current;
    }

    void DesktopNotice::present(const QString &title, const QString &text, int milliseconds) {
        m_title = title;
        m_text = text;

        // Экран выбираем по курсору: у человека может быть три монитора, и
        // карточка должна выйти там, где он сейчас смотрит.
        const QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (screen == nullptr) screen = QGuiApplication::primaryScreen();
        if (screen != nullptr) {
            const QRect area = screen->availableGeometry();
            move(area.right() - width() + kShadow - kMargin, area.top() - kShadow + kMargin);
        }

        setWindowOpacity(0.0);
        show();
        raise();

        m_fade->stop();
        m_fade->setStartValue(windowOpacity());
        m_fade->setEndValue(1.0);
        m_fade->start();

        m_life->stop();
        m_life->setDuration(qMax(2000, milliseconds));
        m_life->start();
        update();
    }

    void DesktopNotice::dismiss() {
        m_life->stop();
        m_fade->stop();
        m_fade->setStartValue(windowOpacity());
        m_fade->setEndValue(0.0);
        connect(m_fade, &QVariantAnimation::finished, this, &QWidget::close, Qt::UniqueConnection);
        m_fade->start();
    }

    QRect DesktopNotice::closeRect() const {
        return QRect(width() - kShadow - 34, kShadow + 10, 24, 24);
    }

    void DesktopNotice::paintEvent(QPaintEvent *) {
        const auto palette = Fsnt::CurrentPalette();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QRectF card(kShadow, kShadow, kNoticeWidth, kNoticeHeight);

        // Тень собираем слоями: окно прозрачное, системной тени у него нет, а без
        // тени карточка на светлых обоях сливается с фоном.
        painter.setBrush(Qt::NoBrush);
        for (int layer = 1; layer <= kShadow; ++layer) {
            const int alpha = 32 - layer * 2;
            if (alpha <= 0) break;
            painter.setPen(QPen(QColor(0, 0, 0, alpha), 1.0));
            painter.drawRoundedRect(card.adjusted(-layer, -layer + 2, layer, layer + 2), 16 + layer, 16 + layer);
        }

        painter.setBrush(palette.surface);
        painter.setPen(QPen(palette.border, 1));
        painter.drawRoundedRect(card, 16, 16);

        // Значок в кружке акцентной подложки — тот же колокольчик, что в шапке
        // окна: человек связывает карточку с кнопкой, на которую ему жать.
        const QRectF badge(card.left() + 16, card.top() + 26, 44, 44);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette.accentSoft);
        painter.drawEllipse(badge);
        Fsnt::PaintGlyph(&painter, Fsnt::Glyph::Bell, badge.adjusted(12, 12, -12, -12), palette.accent);

        const int textLeft = static_cast<int>(badge.right()) + 14;
        const int textRight = width() - kShadow - 16;

        QFont titleFont = font();
        titleFont.setPointSizeF(titleFont.pointSizeF() + 0.5);
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.setPen(palette.text);
        const QRect titleBox(textLeft, kShadow + 24, textRight - textLeft - 24, 20);
        painter.drawText(titleBox, Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(m_title, Qt::ElideRight, titleBox.width()));

        QFont bodyFont = font();
        painter.setFont(bodyFont);
        painter.setPen(palette.textMuted);
        const QRect bodyBox(textLeft, kShadow + 46, textRight - textLeft, 34);
        painter.drawText(bodyBox, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_text);

        // Крестик и полоска оставшегося времени: без них карточка выглядит так,
        // будто повисла навсегда.
        const QRect close = closeRect();
        painter.setPen(QPen(m_closeHovered ? palette.text : palette.textMuted, 1.6, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(close.center() + QPoint(-4, -4), close.center() + QPoint(4, 4));
        painter.drawLine(close.center() + QPoint(4, -4), close.center() + QPoint(-4, 4));

        const qreal left = 1.0 - m_life->currentValue().toReal();
        if (left > 0.0) {
            QColor line = palette.accent;
            line.setAlphaF(0.55);
            painter.setPen(Qt::NoPen);
            painter.setBrush(line);
            const qreal railWidth = (card.width() - 32) * left;
            painter.drawRoundedRect(QRectF(card.left() + 16, card.bottom() - 8, railWidth, 3), 1.5, 1.5);
        }
    }

    void DesktopNotice::mousePressEvent(QMouseEvent *event) {
        if (closeRect().contains(event->pos())) {
            dismiss();
            return;
        }
        emit activated();
        dismiss();
    }

    void DesktopNotice::mouseMoveEvent(QMouseEvent *event) {
        const bool hovered = closeRect().contains(event->pos());
        if (hovered != m_closeHovered) {
            m_closeHovered = hovered;
            update();
        }
    }

    void DesktopNotice::enterEvent(QEnterEvent *) {
        // Пока курсор на карточке, отсчёт стоит: человек читает.
        m_life->pause();
    }

    void DesktopNotice::leaveEvent(QEvent *) {
        m_closeHovered = false;
        if (m_life->state() == QAbstractAnimation::Paused) m_life->resume();
        update();
    }
} // namespace Fsnt
