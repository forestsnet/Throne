#pragma once

#include <QPushButton>

class QVariantAnimation;

// Квадратная кнопка действия, которая умеет показывать, что работа идёт.
//
// Пока замер пинга или обновление подписки в процессе, обычная кнопка выглядит
// ровно как до нажатия, и непонятно, началось ли вообще что-нибудь. В занятом
// состоянии кнопка гасит свой знак и рисует вращающуюся дугу.
class BusyButton : public QPushButton {
    Q_OBJECT

public:
    explicit BusyButton(const QString &glyph, QWidget *parent = nullptr);

    void setBusy(bool busy);
    bool isBusy() const { return m_busy; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_glyph;
    bool m_busy = false;
    qreal m_spin = 0.0;
    QVariantAnimation *m_spinAnim = nullptr;
};
