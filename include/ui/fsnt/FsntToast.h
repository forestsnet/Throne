#pragma once

#include <QWidget>

class QVariantAnimation;

// Всплывающее уведомление поверх окна.
//
// Обновление подписки показывало модальный список всех профилей — двадцать две
// строки вида «[+] [VLESS (Xray)] …», которые надо было закрыть руками. Клиенту
// нужен факт, а не перечень: короткая строка сверху, которая уходит сама.
//
// Оставшееся время видно по рамке: она обегает карточку и укорачивается, а её
// цвет гаснет к цвету границы. Прогресс-полоса заняла бы отдельную строку.
class FsntToast : public QWidget {
    Q_OBJECT

public:
    explicit FsntToast(QWidget *parent = nullptr);

    // Показывает уведомление и заново запускает отсчёт. Повторный вызов не
    // копит окна: карточка одна на окно и просто меняет текст.
    void show(const QString &text, int milliseconds = 5000);

    // Пересчитать размер и позицию, не трогая отсчёт. Нужна при изменении
    // размеров окна: show() заново запустил бы таймер.
    void relayout();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect closeRect() const;

    QString m_text;
    qreal m_progress = 0.0;   // 0 только что показано, 1 время вышло
    bool m_closeHovered = false;
    QVariantAnimation *m_countdown = nullptr;
};
