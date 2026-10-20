#pragma once

#include <QStyledItemDelegate>

#include "include/ui/fsnt/FsntPalette.hpp"

// Строка сервера: имя слева, пинг справа. Рисуем сами, чтобы строка выглядела
// карточкой, а не ячейкой таблицы.
class ServerItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    // Ширина зоны сердечка справа. Живёт здесь, потому что по ней панель ловит
    // клик: разъедься эти два числа — и попадание перестанет совпадать с рисунком.
    static constexpr int kHeartZone = 34;
    // Порог, за которым пинг перестаёт быть хорошим, и второй — за которым плохим.
    static constexpr int kGoodLatencyMs = 120;
    static constexpr int kFairLatencyMs = 250;

    explicit ServerItemDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    // Имя и подпись под ним. Вынесено из paint(): тот и без того длинный,
    // а здесь своя развилка на «с подписью» и «без».
    static void drawNameAndSubtitle(QPainter *painter, const QStyleOptionViewItem &option,
                                    const QModelIndex &index, const QRect &box,
                                    const Fsnt::Palette &palette);
};
