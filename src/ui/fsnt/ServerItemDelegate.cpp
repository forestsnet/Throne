#include "src/ui/fsnt/ServerItemDelegate.h"

#include <QDateTime>
#include <QPainter>

#include <cmath>
#include <QPainterPath>

#include "include/database/entities/Profile.h"
#include "include/ui/fsnt/FsntControls.h"
#include "include/ui/fsnt/FsntPalette.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/ui/fsnt/ServerListPanel.h"

namespace {
    constexpr int kRowRowHeight = 58;
    constexpr int kRowTextLeft = 14;
    // Толщина полоски-указателя у выбранной строки.
    constexpr int kRowMarkerWidth = 3;
    // Место под три точки «идёт замер» и период их пульсации.
    constexpr int kRowMeasuringZone = 40;
    constexpr int kRowDotCycleMs = 900;

    QColor latencyColor(const Fsnt::Palette &p, const int ms) {
        if (ms <= ServerItemDelegate::kGoodLatencyMs) return p.success;
        if (ms <= ServerItemDelegate::kFairLatencyMs) return p.warn;
        return p.danger;
    }

    QColor withAlpha(QColor c, const int alpha) {
        c.setAlpha(alpha);
        return c;
    }
}

ServerItemDelegate::ServerItemDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QSize ServerItemDelegate::sizeHint(const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const {
    Q_UNUSED(option)
    Q_UNUSED(index)
    return {0, kRowRowHeight};
}

void ServerItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                               const QModelIndex &index) const {
    const Fsnt::Palette p = Fsnt::CurrentPalette();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect fullRow = option.rect.adjusted(0, 2, 0, -2);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    if (selected || hovered) {
        painter->setBrush(selected ? p.accentSoft : p.cardHover);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(fullRow, Fsnt::kRowRadius, Fsnt::kRowRadius);
    }

    if (selected) {
        // Полоска слева: единственный способ отличить выбранную строку от наведённой,
        // когда обе залиты близкими оттенками.
        QPainterPath marker;
        marker.addRoundedRect(QRectF(fullRow.left() + 1, fullRow.top() + 8,
                                     kRowMarkerWidth, fullRow.height() - 16),
                              1.5, 1.5);
        painter->fillPath(marker, p.accent);
    }

    QRect row = fullRow;

    // --- сердечко ---
    const bool favorite = index.data(ServerListPanel::FavoriteRole).toBool();
    const QRect heartRect(row.right() - kHeartZone, row.top(), kHeartZone, row.height());
    const QColor heartColor = favorite ? p.danger : withAlpha(p.textMuted, hovered ? 200 : 110);
    // Символы ♥ и ♡ в разных системах разной ширины и сидят на разной высоте,
    // поэтому рисуем путём — и заодно получаем заливку у избранного.
    painter->setBrush(favorite ? QBrush(heartColor) : QBrush(Qt::NoBrush));
    Fsnt::PaintGlyph(painter, Fsnt::Glyph::Heart,
                     QRectF(heartRect).adjusted(8, 13, -8, -13), heartColor);
    row = row.adjusted(0, 0, -kHeartZone, 0);

    // --- пинг таблеткой ---
    //
    // Показываем все четыре состояния. Раньше плашка рисовалась только при
    // latency > 0, и недоступный сервер выглядел ровно как неизмеренный —
    // а это разные вещи: в России пинга нет вообще, и об этом надо сказать.
    const int latency = index.data(ServerListPanel::LatencyRole).toInt();

    // Пока сервер не ответил, вместо значения — три пульсирующие точки.
    // Замер идёт секундами, и без этого непонятно, работает ли кнопка вообще:
    // строка просто показывает прежнее число.
    if (index.data(ServerListPanel::MeasuringRole).toBool()) {
        const QRect zone(row.right() - kRowMeasuringZone, row.top(), kRowMeasuringZone, row.height());
        // Фазу берём из часов: делегат не хранит состояния, а панель лишь будит
        // перерисовку, поэтому все строки дышат согласованно.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        painter->setPen(Qt::NoPen);
        for (int dot = 0; dot < 3; ++dot) {
            const qreal phase = std::fmod(
                static_cast<qreal>(now % kRowDotCycleMs) / kRowDotCycleMs + dot * 0.22, 1.0);
            // Треугольная волна: точка разгорается и гаснет, а не мигает.
            const qreal wave = phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0;
            painter->setBrush(withAlpha(p.accent, 60 + static_cast<int>(150 * wave)));
            const QPointF centre(zone.left() + 7 + dot * 11, zone.center().y() + 1);
            painter->drawEllipse(centre, 2.6 + wave * 0.9, 2.6 + wave * 0.9);
        }

        const QRect nameZone = row.adjusted(kRowTextLeft, 0, -(kRowMeasuringZone + 8), 0);
        painter->setFont(option.font);
        painter->setPen(p.text);
        painter->drawText(nameZone, Qt::AlignVCenter | Qt::AlignLeft,
                          QFontMetrics(option.font).elidedText(
                              index.data(Qt::DisplayRole).toString(),
                              Qt::ElideRight, nameZone.width()));
        painter->restore();
        return;
    }

    QString pingText;
    QColor tint;
    bool filled = true;
    if (latency > 0) {
        pingText = QString("%1 %2").arg(latency).arg(tr("ms"));
        tint = latencyColor(p, latency);
    } else if (latency == Configs::kLatencyConnectOnly) {
        // Соединение установилось, но замерить задержку не удалось.
        pingText = tr("ok");
        tint = p.success;
    } else if (latency < 0) {
        pingText = tr("no reply");
        tint = p.danger;
    } else {
        pingText = QStringLiteral("—");
        tint = p.textMuted;
        filled = false;   // ещё не мерили — это не новость, кричать незачем
    }

    int pingBlock = 0;
    {
        QFont pingFont = option.font;
        pingFont.setPointSizeF(pingFont.pointSizeF() - 1.5);
        pingFont.setBold(filled);
        const QFontMetrics fm(pingFont);

        const int pillW = fm.horizontalAdvance(pingText) + 16;
        const int pillH = 20;
        const QRect pill(row.right() - pillW - 6, row.center().y() - pillH / 2 + 1, pillW, pillH);

        if (filled) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(withAlpha(tint, 38));
            painter->drawRoundedRect(pill, pillH / 2.0, pillH / 2.0);
        }

        painter->setFont(pingFont);
        painter->setPen(tint);
        painter->drawText(pill, Qt::AlignCenter, pingText);

        pingBlock = pillW + 12;
    }

    // --- имя и техническая подпись ---
    drawNameAndSubtitle(painter, option, index, row.adjusted(kRowTextLeft, 0, -pingBlock, 0), p);

    painter->restore();
}

void ServerItemDelegate::drawNameAndSubtitle(QPainter *painter, const QStyleOptionViewItem &option,
                                             const QModelIndex &index, const QRect &box,
                                             const Fsnt::Palette &p) {
    const QString subtitle = index.data(ServerListPanel::SubtitleRole).toString();

    // Без подписи имя стоит по центру строки, иначе — двумя строками.
    if (subtitle.isEmpty()) {
        painter->setFont(option.font);
        painter->setPen(p.text);
        painter->drawText(box, Qt::AlignVCenter | Qt::AlignLeft,
                          QFontMetrics(option.font).elidedText(
                              index.data(Qt::DisplayRole).toString(), Qt::ElideRight, box.width()));
        return;
    }

    const QRect nameRect(box.left(), box.top() + 8, box.width(), box.height() / 2 - 4);
    painter->setFont(option.font);
    painter->setPen(p.text);
    painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
                      QFontMetrics(option.font).elidedText(index.data(Qt::DisplayRole).toString(),
                                                           Qt::ElideRight, nameRect.width()));

    QFont subFont = option.font;
    subFont.setPointSizeF(subFont.pointSizeF() - 2.0);
    const QRect subRect(box.left(), box.center().y() + 1, box.width(), box.height() / 2 - 6);
    // Незашифрованное соединение подписывается тревожным цветом: это тот случай,
    // когда мелкий серый текст обязан броситься в глаза.
    painter->setFont(subFont);
    painter->setPen(index.data(ServerListPanel::InsecureRole).toBool() ? p.warn : p.textMuted);
    painter->drawText(subRect, Qt::AlignVCenter | Qt::AlignLeft,
                      QFontMetrics(subFont).elidedText(subtitle, Qt::ElideRight, subRect.width()));
}
