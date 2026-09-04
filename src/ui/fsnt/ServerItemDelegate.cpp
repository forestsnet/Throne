#include "src/ui/fsnt/ServerItemDelegate.h"

#include <QPainter>

#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/ui/fsnt/ServerListPanel.h"
#include "include/ui/setting/ThemeManager.hpp"

namespace {
    // Порог, за которым пинг перестаёт быть хорошим. Ниже — успех, выше — предупреждение.
    constexpr int kGoodLatencyMs = 120;
}

ServerItemDelegate::ServerItemDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QSize ServerItemDelegate::sizeHint(const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const {
    Q_UNUSED(option)
    Q_UNUSED(index)
    return {0, 38};
}

void ServerItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                               const QModelIndex &index) const {
    const auto &t = themeManager()->tokens;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QRect row = option.rect.adjusted(0, 1, 0, -1);

    if (option.state & QStyle::State_Selected) {
        painter->setBrush(t.selectedFill);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(row, Fsnt::kRowRadius, Fsnt::kRowRadius);
    } else if (option.state & QStyle::State_MouseOver) {
        painter->setBrush(t.hoverFill);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(row, Fsnt::kRowRadius, Fsnt::kRowRadius);
    }

    const int latency = index.data(ServerListPanel::LatencyRole).toInt();
    const QString pingText = latency > 0 ? QString("%1 мс").arg(latency) : QString();

    QFont pingFont = option.font;
    pingFont.setPointSizeF(pingFont.pointSizeF() - 1);
    const int pingWidth = pingText.isEmpty() ? 0 : QFontMetrics(pingFont).horizontalAdvance(pingText) + 12;

    QRect nameRect = row.adjusted(10, 0, -(pingWidth + 10), 0);
    painter->setPen(t.onSurface);
    painter->setFont(option.font);
    painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
                      QFontMetrics(option.font).elidedText(index.data(Qt::DisplayRole).toString(),
                                                           Qt::ElideRight, nameRect.width()));

    if (!pingText.isEmpty()) {
        painter->setPen(latency <= kGoodLatencyMs ? t.success : t.tag);
        painter->setFont(pingFont);
        painter->drawText(row.adjusted(0, 0, -10, 0), Qt::AlignVCenter | Qt::AlignRight, pingText);
    }

    painter->restore();
}
