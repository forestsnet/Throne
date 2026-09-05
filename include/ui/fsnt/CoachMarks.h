#pragma once

#include <QList>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QVariantAnimation;

namespace Fsnt {
    // Экскурсия по живому окну: затемняем всё, кроме одного элемента, и
    // рассказываем про него карточкой рядом.
    //
    // Мастер со слайдами объяснял интерфейс в отрыве от него: пользователь
    // читал про список серверов, глядя на пустой диалог, а потом искал этот
    // список сам. Здесь наоборот — подсвечивается именно та кнопка, о которой
    // речь, и после экскурсии её уже знаешь в лицо.
    class CoachMarks : public QWidget {
        Q_OBJECT

    public:
        struct Step {
            // Пустая цель — карточка по центру и без выреза: так показываем то,
            // у чего нет своего виджета (например, значок в трее).
            QPointer<QWidget> target;
            QString title;
            QString text;
        };

        // Родитель — окно, поверх которого ложимся; оверлей занимает его целиком.
        explicit CoachMarks(QWidget *host);

        void setSteps(const QList<Step> &steps);
        // Показывает первый шаг. Пустой список закрывает экскурсию сразу.
        void start();

    signals:
        // true — дошли до конца, false — нажали «Пропустить» или закрыли.
        void finished(bool completed);

    protected:
        void paintEvent(QPaintEvent *event) override;
        // Клик мимо карточки не должен проваливаться в интерфейс под нами:
        // во время экскурсии он там ничего не значит и только сбивает.
        void mousePressEvent(QMouseEvent *event) override;
        void keyPressEvent(QKeyEvent *event) override;
        bool eventFilter(QObject *watched, QEvent *event) override;

    private:
        void goTo(int index);
        void stop(bool completed);
        // Прямоугольник подсветки в координатах оверлея.
        QRect holeFor(const Step &step) const;
        // Радиус выреза: у близкой к квадрату цели — полное скругление, иначе
        // обычное. Круглую кнопку питания прямоугольная рамка обводила грубо.
        qreal holeRadius() const;
        void placeCard();
        void buildCard();

        QList<Step> m_steps;
        int m_index = -1;

        QRect m_hole;               // текущий вырез, между шагами анимируется
        QVariantAnimation *m_move = nullptr;

        QWidget *m_card = nullptr;
        QLabel *m_step = nullptr;
        QLabel *m_title = nullptr;
        QLabel *m_text = nullptr;
        QWidget *m_dots = nullptr;
        QPushButton *m_skip = nullptr;
        QPushButton *m_back = nullptr;
        QPushButton *m_next = nullptr;
    };
} // namespace Fsnt
