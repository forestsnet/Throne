#include "include/ui/fsnt/CoachMarks.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include "include/ui/fsnt/FsntPalette.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"

namespace {
    constexpr int kCoachHolePadding = 6;
    constexpr int kCoachHoleRadius = 10;
    // Во сколько раз стороны выреза могут отличаться, чтобы считать его
    // квадратным и скруглить полностью.
    constexpr qreal kCoachRoundAspect = 1.2;
    // И до какого размера. Круг уместен вокруг кнопки, а вокруг целой области
    // списка он выглядит нелепо: это уже не элемент, а часть окна.
    constexpr qreal kCoachRoundMaxSide = 280.0;
    constexpr int kCoachCardWidth = 320;
    constexpr int kCoachCardGap = 14;      // зазор между вырезом и карточкой
    constexpr int kCoachEdgeMargin = 16;   // карточка не липнет к краю окна
    constexpr int kCoachArrow = 9;
    constexpr int kCoachMoveMs = 260;

    // Индикатор шагов: точки, у текущей — вытянутая пилюля. Отдельный виджет,
    // потому что рисовать это меткой пришлось бы символами, а они в разных
    // системах разной ширины.
    class CoachStepDots : public QWidget {
    public:
        explicit CoachStepDots(QWidget *parent) : QWidget(parent) { setFixedHeight(8); }

        void setState(const int count, const int current) {
            m_count = count;
            m_current = current;
            setFixedWidth(count <= 0 ? 0 : count * 6 + (count - 1) * 4 + 8);
            update();
        }

    protected:
        void paintEvent(QPaintEvent *) override {
            if (m_count <= 0) return;
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            const auto palette = Fsnt::CurrentPalette();
            qreal x = 0;
            for (int i = 0; i < m_count; ++i) {
                const bool active = i == m_current;
                const qreal w = active ? 14 : 6;
                QColor tint = active ? palette.accent : palette.textMuted;
                if (!active) tint.setAlpha(90);
                painter.setBrush(tint);
                painter.drawRoundedRect(QRectF(x, 1, w, 6), 3, 3);
                x += w + 4;
            }
        }

    private:
        int m_count = 0;
        int m_current = 0;
    };
}

namespace Fsnt {
    CoachMarks::CoachMarks(QWidget *host) : QWidget(host) {
        setFocusPolicy(Qt::StrongFocus);
        setStyleSheet(BuildStyleSheet());
        buildCard();
        hide();

        m_move = new QVariantAnimation(this);
        m_move->setDuration(kCoachMoveMs);
        m_move->setEasingCurve(QEasingCurve::InOutCubic);
        connect(m_move, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_hole = value.toRect();
            placeCard();
            update();
        });

        host->installEventFilter(this);
    }

    void CoachMarks::buildCard() {
        m_card = new QWidget(this);
        m_card->setObjectName("fsntCard");
        m_card->setFixedWidth(kCoachCardWidth);

        auto *column = new QVBoxLayout(m_card);
        column->setContentsMargins(16, 14, 16, 14);
        column->setSpacing(8);

        auto *top = new QHBoxLayout;
        top->setSpacing(8);
        m_step = new QLabel(m_card);
        m_step->setObjectName("fsntCoachStep");
        top->addWidget(m_step);
        top->addStretch();
        m_skip = new QPushButton(tr("Skip"), m_card);
        m_skip->setObjectName("fsntCoachSkip");
        m_skip->setCursor(Qt::PointingHandCursor);
        connect(m_skip, &QPushButton::clicked, this, [this] { stop(false); });
        top->addWidget(m_skip);
        column->addLayout(top);

        m_title = new QLabel(m_card);
        m_title->setObjectName("fsntCoachTitle");
        m_title->setWordWrap(true);
        column->addWidget(m_title);

        m_text = new QLabel(m_card);
        m_text->setObjectName("fsntDialogHint");
        m_text->setWordWrap(true);
        column->addWidget(m_text);

        auto *bottom = new QHBoxLayout;
        bottom->setSpacing(8);
        m_dots = new CoachStepDots(m_card);
        bottom->addWidget(m_dots);
        bottom->addStretch();

        m_back = new QPushButton(tr("Back"), m_card);
        m_back->setObjectName("fsntGhost");
        m_back->setCursor(Qt::PointingHandCursor);
        connect(m_back, &QPushButton::clicked, this, [this] { goTo(m_index - 1); });
        bottom->addWidget(m_back);

        m_next = new QPushButton(m_card);
        m_next->setObjectName("fsntPrimary");
        m_next->setCursor(Qt::PointingHandCursor);
        connect(m_next, &QPushButton::clicked, this, [this] {
            if (m_index + 1 >= m_steps.size()) stop(true);
            else goTo(m_index + 1);
        });
        bottom->addWidget(m_next);
        column->addLayout(bottom);
    }

    void CoachMarks::setSteps(const QList<Step> &steps) { m_steps = steps; }

    void CoachMarks::start() {
        if (m_steps.isEmpty()) {
            stop(true);
            return;
        }
        resize(parentWidget()->size());
        raise();
        show();
        setFocus();
        // Первый шаг без анимации: ползти неоткуда, вырез просто появляется.
        m_index = 0;
        m_hole = holeFor(m_steps.first());
        goTo(0);
    }

    void CoachMarks::goTo(const int index) {
        if (index < 0 || index >= m_steps.size()) return;

        const QRect from = m_hole;
        m_index = index;
        const Step &step = m_steps[index];

        m_step->setText(tr("STEP %1 OF %2").arg(index + 1).arg(m_steps.size()));
        m_title->setText(step.title);
        m_text->setText(step.text);
        static_cast<CoachStepDots *>(m_dots)->setState(m_steps.size(), index);
        m_back->setVisible(index > 0);
        m_next->setText(index + 1 >= m_steps.size() ? tr("Got it") : tr("Next"));
        m_card->adjustSize();

        const QRect to = holeFor(step);
        if (from.isNull() || to.isNull() || from == to) {
            m_hole = to;
            placeCard();
            update();
            return;
        }
        m_move->stop();
        m_move->setStartValue(from);
        m_move->setEndValue(to);
        m_move->start();
    }

    QRect CoachMarks::holeFor(const Step &step) const {
        if (step.target.isNull() || !step.target->isVisible()) return {};
        const QPoint topLeft = step.target->mapTo(parentWidget(), QPoint(0, 0));
        return QRect(topLeft, step.target->size())
            .adjusted(-kCoachHolePadding, -kCoachHolePadding, kCoachHolePadding, kCoachHolePadding);
    }

    qreal CoachMarks::holeRadius() const {
        const qreal w = m_hole.width();
        const qreal h = m_hole.height();
        if (w <= 0 || h <= 0) return kCoachHoleRadius;
        const qreal small = qMin(w, h);
        if (small <= kCoachRoundMaxSide && qMax(w, h) / small < kCoachRoundAspect) {
            return small / 2.0;
        }
        return kCoachHoleRadius;
    }

    void CoachMarks::placeCard() {
        m_card->adjustSize();
        const QSize card = m_card->size();

        if (m_hole.isNull()) {
            m_card->move((width() - card.width()) / 2, (height() - card.height()) / 2);
            return;
        }

        const int maxX = qMax(kCoachEdgeMargin, width() - card.width() - kCoachEdgeMargin);
        const int maxY = qMax(kCoachEdgeMargin, height() - card.height() - kCoachEdgeMargin);
        const int centredX = qBound(kCoachEdgeMargin, m_hole.center().x() - card.width() / 2, maxX);

        // Под вырезом, если там помещаемся, иначе над ним: карточка не должна
        // закрывать то, о чём рассказывает.
        const int below = m_hole.bottom() + kCoachCardGap;
        if (below + card.height() <= height() - kCoachEdgeMargin) {
            m_card->move(centredX, below);
            return;
        }
        const int above = m_hole.top() - kCoachCardGap - card.height();
        if (above >= kCoachEdgeMargin) {
            m_card->move(centredX, above);
            return;
        }

        // Высокая цель — например весь список серверов — не оставляет места ни
        // сверху, ни снизу. Тогда становимся сбоку, иначе карточка легла бы
        // поверх подсвеченного.
        const int centredY = qBound(kCoachEdgeMargin, m_hole.center().y() - card.height() / 2, maxY);
        const int right = m_hole.right() + kCoachCardGap;
        if (right + card.width() <= width() - kCoachEdgeMargin) {
            m_card->move(right, centredY);
            return;
        }
        const int left = m_hole.left() - kCoachCardGap - card.width();
        if (left >= kCoachEdgeMargin) {
            m_card->move(left, centredY);
            return;
        }

        m_card->move(centredX, centredY);
    }

    void CoachMarks::paintEvent(QPaintEvent *) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const auto palette = CurrentPalette();

        QPainterPath dim;
        dim.addRect(rect());
        if (!m_hole.isNull()) {
            QPainterPath hole;
            hole.addRoundedRect(m_hole, holeRadius(), holeRadius());
            dim = dim.subtracted(hole);
        }
        painter.fillPath(dim, QColor(0, 0, 0, palette.dark ? 168 : 120));

        if (m_hole.isNull()) return;

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(palette.accent, 2));
        painter.drawRoundedRect(m_hole, holeRadius(), holeRadius());

        // Клювик от карточки к вырезу: без него связь между подсказкой и
        // подсвеченной кнопкой приходится додумывать.
        const QRect card = m_card->geometry();
        QPolygon arrow;
        if (card.top() >= m_hole.bottom()) {
            const int tip = qBound(card.left() + kCoachArrow * 2, m_hole.center().x(),
                                   card.right() - kCoachArrow * 2);
            arrow << QPoint(tip, card.top() - kCoachArrow)
                  << QPoint(tip - kCoachArrow, card.top() + 1)
                  << QPoint(tip + kCoachArrow, card.top() + 1);
        } else if (card.bottom() <= m_hole.top()) {
            const int tip = qBound(card.left() + kCoachArrow * 2, m_hole.center().x(),
                                   card.right() - kCoachArrow * 2);
            arrow << QPoint(tip, card.bottom() + kCoachArrow)
                  << QPoint(tip - kCoachArrow, card.bottom() - 1)
                  << QPoint(tip + kCoachArrow, card.bottom() - 1);
        } else if (card.left() >= m_hole.right()) {
            const int tip = qBound(card.top() + kCoachArrow * 2, m_hole.center().y(),
                                   card.bottom() - kCoachArrow * 2);
            arrow << QPoint(card.left() - kCoachArrow, tip)
                  << QPoint(card.left() + 1, tip - kCoachArrow)
                  << QPoint(card.left() + 1, tip + kCoachArrow);
        } else if (card.right() <= m_hole.left()) {
            const int tip = qBound(card.top() + kCoachArrow * 2, m_hole.center().y(),
                                   card.bottom() - kCoachArrow * 2);
            arrow << QPoint(card.right() + kCoachArrow, tip)
                  << QPoint(card.right() - 1, tip - kCoachArrow)
                  << QPoint(card.right() - 1, tip + kCoachArrow);
        } else {
            return;
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette.card);
        painter.drawPolygon(arrow);
    }

    void CoachMarks::mousePressEvent(QMouseEvent *event) { event->accept(); }

    void CoachMarks::keyPressEvent(QKeyEvent *event) {
        switch (event->key()) {
            case Qt::Key_Escape:
                stop(false);
                return;
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Right:
                if (m_index + 1 >= m_steps.size()) stop(true);
                else goTo(m_index + 1);
                return;
            case Qt::Key_Left:
                if (m_index > 0) goTo(m_index - 1);
                return;
            default:
                QWidget::keyPressEvent(event);
        }
    }

    bool CoachMarks::eventFilter(QObject *watched, QEvent *event) {
        if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible()) {
            resize(parentWidget()->size());
            m_move->stop();
            m_hole = m_index >= 0 && m_index < m_steps.size() ? holeFor(m_steps[m_index]) : QRect();
            placeCard();
            update();
        }
        return QWidget::eventFilter(watched, event);
    }

    void CoachMarks::stop(const bool completed) {
        m_move->stop();
        hide();
        emit finished(completed);
    }
} // namespace Fsnt
