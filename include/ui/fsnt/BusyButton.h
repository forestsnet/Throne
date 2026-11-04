#pragma once

#include <QAbstractButton>

#include "include/ui/fsnt/FsntControls.h"

class QVariantAnimation;

// Квадратная кнопка действия, которая умеет показывать, что работа идёт.
//
// Пока замер пинга или обновление подписки в процессе, обычная кнопка выглядит
// ровно как до нажатия, и непонятно, началось ли вообще что-нибудь. В занятом
// состоянии кнопка гасит свой знак и рисует вращающуюся дугу.
//
// Занятость и отключённость — разные вещи, и раньше они были склеены. Обновлению
// подписки повторное нажатие и правда ни к чему, а вот замер им останавливают:
// на подписке в сотню серверов иначе остаётся только ждать. Кнопка замера этот
// обрыв и предлагала, но ветка была недостижима — отключённая кнопка не получает
// мышиных событий и не испускает clicked. Поэтому отключение теперь задаётся
// отдельно от занятости.
class BusyButton : public QAbstractButton {
    Q_OBJECT

public:
    explicit BusyButton(Fsnt::Glyph glyph, QWidget *parent = nullptr);

    QSize sizeHint() const override;

    // disableWhileBusy == false: кнопка рисует дугу, но остаётся нажимаемой, и
    // нажатие во время работы приходит вызывающему как обычный clicked.
    void setBusy(bool busy);
    bool isBusy() const { return m_busy; }
    void setDisableWhileBusy(bool disable) { m_disableWhileBusy = disable; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Fsnt::Glyph m_glyph;
    bool m_busy = false;
    bool m_disableWhileBusy = true;
    bool m_hovered = false;
    qreal m_spin = 0.0;
    QVariantAnimation *m_spinAnim = nullptr;
};
