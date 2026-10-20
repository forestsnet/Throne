#pragma once

#include <QWidget>

class QVariantAnimation;

// Круглая кнопка подключения: кольцо состояния, свечение и знак питания.
//
// Рисуется целиком вручную. QSS так не умеет: ему недоступны ни дуга по кругу,
// ни радиальное свечение, ни анимация, а именно они отличают живой клиент от
// формы с кнопкой.
class PowerButton : public QWidget {
    Q_OBJECT

public:
    enum class State {
        Off,
        Connecting,
        Connected,
        Stopping,
    };

    explicit PowerButton(QWidget *parent = nullptr);

    void setState(State state);
    State state() const { return m_state; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    // Цвет кольца и свечения для текущего состояния.
    QColor stateColor() const;
    // Перезапускает анимации под состояние: вращение для «подключается»,
    // дыхание для «подключено», покой для остальных.
    void retuneAnimations();

    State m_state = State::Off;
    bool m_hovered = false;
    bool m_pressed = false;
    qreal m_spin = 0.0;  // 0..360, поворот дуги
    qreal m_pulse = 0.0; // 0..1, амплитуда свечения

    QVariantAnimation *m_spinAnim = nullptr;
    QVariantAnimation *m_pulseAnim = nullptr;
};
