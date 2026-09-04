#pragma once

#include <QAbstractButton>
#include <QComboBox>

class QVariantAnimation;

// Современные замены штатным виджетам.
//
// QCheckBox и выпадающий список QComboBox рисуются стилем платформы, и никакой
// QSS их не спасает: индикатор флажка и стрелка приходят картинками из стиля, а
// всплывающий список на macOS вообще нативный. Рядом с нарисованной вручную
// кнопкой подключения они выглядят как из другого приложения. Поэтому оба
// нарисованы сами.

// Переключатель-тумблер вместо квадратного флажка.
class FsntSwitch : public QAbstractButton {
    Q_OBJECT

public:
    explicit FsntSwitch(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    qreal m_position = 0.0;   // 0 выключен, 1 включён
    bool m_hovered = false;
    QVariantAnimation *m_slide = nullptr;
};

// Выпадающий список с собственной отрисовкой поля и списка.
class FsntSelect : public QComboBox {
    Q_OBJECT

public:
    explicit FsntSelect(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showPopup() override;

private:
    bool m_hovered = false;
};
