#pragma once

#include <QAbstractButton>
#include <QComboBox>
#include <QIcon>
#include <QPixmap>

class QPainter;
class QWidget;
class QVariantAnimation;

namespace Fsnt {
    // Значки рисуются путями, а не буквами и не эмодзи. Глиф из шрифта в каждой
    // системе своей ширины и сидит на своей высоте, а цветная эмодзи вроде ⚡
    // вообще игнорирует палитру и выбивается из оформления.
    enum class Glyph {
        Plus,
        Refresh,
        Gear,
        Bolt,
        Search,
        Heart,
        Close,
        Logs,
    };

    // Значок в виде QIcon — для мест, где виджет принимает только её
    // (действия внутри QLineEdit, кнопка очистки поиска).
    QIcon GlyphIcon(Glyph glyph, int size, const QColor &color);

    // Нарисовать значок прямо в переданный прямоугольник.
    void PaintGlyph(QPainter *painter, Glyph glyph, const QRectF &box, const QColor &color);

    // Фирменный знак FN под плотность экрана, перекрашенный в цвет текста.
    // Исходный SVG одноцветный и почти белый (#FCF8F6): как есть он исчезает
    // на светлой теме.
    QPixmap BrandMark(int size, qreal devicePixelRatio);

    // Вопрос «да/нет» в оформлении простого режима. QMessageBox рисуется
    // стилем платформы и посреди клиентского окна выглядит чужим.
    bool Confirm(QWidget *parent, const QString &title, const QString &text,
                 const QString &acceptText);
} // namespace Fsnt

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

// Квадратная кнопка со значком: та же карточка, что у полей, и нарисованный знак.
class FsntIconButton : public QAbstractButton {
    Q_OBJECT

public:
    explicit FsntIconButton(Fsnt::Glyph glyph, QWidget *parent = nullptr);

    QSize sizeHint() const override;
    // Без рамки и подложки — для значков внутри шапки.
    void setFlat(bool flat);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Fsnt::Glyph m_glyph;
    bool m_flat = false;
    bool m_hovered = false;
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
