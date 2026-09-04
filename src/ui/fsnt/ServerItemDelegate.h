#pragma once

#include <QStyledItemDelegate>

// Строка сервера: имя слева, пинг справа. Рисуем сами, чтобы строка выглядела
// карточкой, а не ячейкой таблицы.
class ServerItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit ServerItemDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
