#pragma once

#include <QAbstractButton>

#include "include/ui/fsnt/FsntControls.h"

class QVariantAnimation;

// Квадратная кнопка действия, которая умеет показывать, что работа идёт.
//
// Пока замер пинга или обновление подписки в процессе, обычная кнопка выглядит
// ровно как до нажатия, и непонятно, началось ли вообще что-нибудь. В занятом
// состоянии кнопка гасит свой знак и рисует вращающуюся дугу.
class BusyButton : public QAbstractButton {
    Q_OBJECT

public:
    explicit BusyButton(Fsnt::Glyph glyph, QWidget *parent = nullptr);

    QSize sizeHint() const override;

    void setBusy(bool busy);
    bool isBusy() const { return m_busy; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Fsnt::Glyph m_glyph;
    bool m_busy = false;
    bool m_hovered = false;
    qreal m_spin = 0.0;
    QVariantAnimation *m_spinAnim = nullptr;
};
