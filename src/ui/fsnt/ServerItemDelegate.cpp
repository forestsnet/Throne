#include "src/ui/fsnt/ServerItemDelegate.h"

#include <QPainter>
#include <QPainterPath>

#include "include/database/entities/Profile.h"
#include "include/ui/fsnt/FsntPalette.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/ui/fsnt/ServerListPanel.h"

namespace {
    constexpr int kRowHeight = 46;
    constexpr int kTextLeft = 14;
    // Толщина полоски-указателя у выбранной строки.
    constexpr int kMarkerWidth = 3;

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
    return {0, kRowHeight};
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
                                     kMarkerWidth, fullRow.height() - 16),
                              1.5, 1.5);
        painter->fillPath(marker, p.accent);
    }

    QRect row = fullRow;

    // --- сердечко ---
    const bool favorite = index.data(ServerListPanel::FavoriteRole).toBool();
    const QRect heartRect(row.right() - kHeartZone, row.top(), kHeartZone, row.height());
    QFont heartFont = option.font;
    heartFont.setPointSizeF(heartFont.pointSizeF() + 1);
    painter->setFont(heartFont);
    painter->setPen(favorite ? p.danger : withAlpha(p.textMuted, hovered ? 200 : 110));
    painter->drawText(heartRect, Qt::AlignCenter, favorite ? QString("♥") : QString("♡"));
    row = row.adjusted(0, 0, -kHeartZone, 0);

    // --- пинг таблеткой ---
    //
    // Показываем все четыре состояния. Раньше плашка рисовалась только при
    // latency > 0, и недоступный сервер выглядел ровно как неизмеренный —
    // а это разные вещи: в России пинга нет вообще, и об этом надо сказать.
    const int latency = index.data(ServerListPanel::LatencyRole).toInt();
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

    // --- имя ---
    const QRect nameRect = row.adjusted(kTextLeft, 0, -pingBlock, 0);
    painter->setFont(option.font);
    painter->setPen(p.text);
    painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
                      QFontMetrics(option.font).elidedText(index.data(Qt::DisplayRole).toString(),
                                                           Qt::ElideRight, nameRect.width()));

    painter->restore();
}
