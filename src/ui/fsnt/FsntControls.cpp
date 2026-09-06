#include "include/ui/fsnt/FsntControls.h"

#include <QAbstractItemView>
#include <QSvgRenderer>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QBrush>
#include <QTransform>
#include <QtMath>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QVariantAnimation>

#include "include/global/Utils.hpp"
#include "include/ui/fsnt/FsntPalette.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"

namespace {
    constexpr int kCtlSwitchWidth = 46;
    constexpr int kCtlSwitchHeight = 26;
    constexpr int kCtlSwitchPadding = 3;
    constexpr int kCtlSlideMs = 160;

    constexpr int kCtlSelectHeight = 38;
    constexpr int kCtlSelectPadding = 12;
    constexpr int kCtlPopupRowHeight = 34;

    // Выше этого сообщение прокручивается внутри окна, а не растит окно.
    constexpr int kCtlNoticeMaxHeight = 340;


    constexpr int kCtlIconButtonSide = 36;

    // --- значки путями ---

    void paintGlyphImpl(QPainter *painter, const Fsnt::Glyph glyph, const QRectF &box,
                        const QColor &color) {
        const QPointF c = box.center();
        const qreal r = qMin(box.width(), box.height()) / 2.0;
        // Кисть вызывающего сохраняем: сердечко у избранного залито, у прочих — контур.
        const QBrush incoming = painter->brush();
        QPen pen(color, qMax(1.6, r * 0.17), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);

        switch (glyph) {
            case Fsnt::Glyph::Plus:
                painter->drawLine(QPointF(c.x() - r * 0.62, c.y()), QPointF(c.x() + r * 0.62, c.y()));
                painter->drawLine(QPointF(c.x(), c.y() - r * 0.62), QPointF(c.x(), c.y() + r * 0.62));
                break;

            case Fsnt::Glyph::Refresh: {
                // Дуга на 300° от трёх часов против часовой; разрыв остаётся
                // снизу справа, и в него смотрит наконечник.
                const qreal radius = r * 0.66;
                const QRectF arc(c.x() - radius, c.y() - radius, radius * 2, radius * 2);
                painter->drawArc(arc, 0, 300 * 16);

                // Треугольник на конце дуги, остриём вниз: так стрелка читается
                // как ход по часовой. Раньше наконечник строился от точки на
                // другом краю дуги и в рисунок просто не попадал.
                QPainterPath head;
                head.moveTo(c.x() + radius - r * 0.26, c.y() - r * 0.04);
                head.lineTo(c.x() + radius + r * 0.26, c.y() - r * 0.04);
                head.lineTo(c.x() + radius, c.y() + r * 0.40);
                head.closeSubpath();
                painter->setPen(Qt::NoPen);
                painter->setBrush(color);
                painter->drawPath(head);
                break;
            }

            case Fsnt::Glyph::Gear: {
                // Зубцы объединяем с телом в один силуэт и вычитаем отверстие.
                // Восемь отдельных тонких лучей вокруг тонкого кольца читались
                // не как шестерёнка, а как снежинка.
                QPainterPath gear;
                gear.addEllipse(c, r * 0.64, r * 0.64);
                for (int i = 0; i < 8; ++i) {
                    QPainterPath tooth;
                    tooth.addRoundedRect(QRectF(c.x() - r * 0.19, c.y() - r * 0.97,
                                                r * 0.38, r * 0.42),
                                         r * 0.09, r * 0.09);
                    QTransform rotate;
                    rotate.translate(c.x(), c.y());
                    rotate.rotate(i * 45.0);
                    rotate.translate(-c.x(), -c.y());
                    gear = gear.united(rotate.map(tooth));
                }
                QPainterPath hole;
                hole.addEllipse(c, r * 0.29, r * 0.29);

                painter->setPen(Qt::NoPen);
                painter->setBrush(color);
                painter->drawPath(gear.subtracted(hole));
                break;
            }

            case Fsnt::Glyph::Bolt: {
                QPainterPath bolt;
                bolt.moveTo(c.x() + r * 0.32, c.y() - r * 0.94);
                bolt.lineTo(c.x() - r * 0.52, c.y() + r * 0.10);
                bolt.lineTo(c.x() - r * 0.02, c.y() + r * 0.10);
                bolt.lineTo(c.x() - r * 0.30, c.y() + r * 0.94);
                bolt.lineTo(c.x() + r * 0.54, c.y() - r * 0.12);
                bolt.lineTo(c.x() + r * 0.04, c.y() - r * 0.12);
                bolt.closeSubpath();
                painter->setPen(Qt::NoPen);
                painter->setBrush(color);
                painter->drawPath(bolt);
                break;
            }

            case Fsnt::Glyph::Search:
                painter->drawEllipse(QPointF(c.x() - r * 0.16, c.y() - r * 0.16), r * 0.48, r * 0.48);
                painter->drawLine(QPointF(c.x() + r * 0.22, c.y() + r * 0.22),
                                  QPointF(c.x() + r * 0.66, c.y() + r * 0.66));
                break;

            case Fsnt::Glyph::Bell: {
                // Колокол силуэтом, как шестерёнка рядом: обводкой он на 18 px
                // получался проволочным и рядом с залитыми соседями выглядел
                // недорисованным.
                QPainterPath bell;
                bell.moveTo(c.x() - r * 0.78, c.y() + r * 0.30);
                bell.cubicTo(c.x() - r * 0.56, c.y() + r * 0.16,
                             c.x() - r * 0.50, c.y() - r * 0.12,
                             c.x() - r * 0.50, c.y() - r * 0.30);
                bell.cubicTo(c.x() - r * 0.50, c.y() - r * 0.70,
                             c.x() + r * 0.50, c.y() - r * 0.70,
                             c.x() + r * 0.50, c.y() - r * 0.30);
                bell.cubicTo(c.x() + r * 0.50, c.y() - r * 0.12,
                             c.x() + r * 0.56, c.y() + r * 0.16,
                             c.x() + r * 0.78, c.y() + r * 0.30);
                bell.closeSubpath();

                // Ушко сверху и язычок снизу рисуем отдельными фигурами: в одном
                // контуре они дают перетяжки, которые на глаз читаются как грязь.
                QPainterPath cap;
                cap.addEllipse(QPointF(c.x(), c.y() - r * 0.72), r * 0.14, r * 0.14);

                QPainterPath clapper;
                clapper.moveTo(c.x() - r * 0.26, c.y() + r * 0.42);
                clapper.arcTo(QRectF(c.x() - r * 0.26, c.y() + r * 0.16, r * 0.52, r * 0.52), 180, 180);
                clapper.closeSubpath();

                painter->setPen(Qt::NoPen);
                painter->setBrush(color);
                painter->drawPath(bell.united(cap));
                painter->drawPath(clapper);
                break;
            }

            case Fsnt::Glyph::More:
                painter->setPen(Qt::NoPen);
                painter->setBrush(color);
                for (int dot = -1; dot <= 1; ++dot) {
                    painter->drawEllipse(QPointF(c.x() + dot * r * 0.52, c.y()), r * 0.14, r * 0.14);
                }
                break;

            case Fsnt::Glyph::Logs:
                // Три строки разной длины: узнаваемее листа с загнутым углом
                // и не превращается в кашу на 16 px.
                painter->drawLine(QPointF(c.x() - r * 0.62, c.y() - r * 0.46),
                                  QPointF(c.x() + r * 0.62, c.y() - r * 0.46));
                painter->drawLine(QPointF(c.x() - r * 0.62, c.y()),
                                  QPointF(c.x() + r * 0.62, c.y()));
                painter->drawLine(QPointF(c.x() - r * 0.62, c.y() + r * 0.46),
                                  QPointF(c.x() + r * 0.16, c.y() + r * 0.46));
                break;

            case Fsnt::Glyph::Close:
                painter->drawLine(QPointF(c.x() - r * 0.52, c.y() - r * 0.52),
                                  QPointF(c.x() + r * 0.52, c.y() + r * 0.52));
                painter->drawLine(QPointF(c.x() + r * 0.52, c.y() - r * 0.52),
                                  QPointF(c.x() - r * 0.52, c.y() + r * 0.52));
                break;

            case Fsnt::Glyph::Heart: {
                painter->setBrush(incoming);
                QPainterPath heart;
                heart.moveTo(c.x(), c.y() + r * 0.68);
                heart.cubicTo(c.x() - r * 1.30, c.y() - r * 0.16,
                              c.x() - r * 0.44, c.y() - r * 1.06, c.x(), c.y() - r * 0.34);
                heart.cubicTo(c.x() + r * 0.44, c.y() - r * 1.06,
                              c.x() + r * 1.30, c.y() - r * 0.16, c.x(), c.y() + r * 0.68);
                painter->drawPath(heart);
                break;
            }
        }
    }

    // Галочка рисуется путём, а не шрифтом: символ ✓ в разных системах разной
    // ширины и по-разному сидит по вертикали.
    void drawCheck(QPainter *painter, const QPointF &centre, const qreal size, const QColor &color) {
        QPainterPath path;
        path.moveTo(centre.x() - size * 0.45, centre.y() + size * 0.02);
        path.lineTo(centre.x() - size * 0.12, centre.y() + size * 0.34);
        path.lineTo(centre.x() + size * 0.46, centre.y() - size * 0.34);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(path);
    }

    void drawChevron(QPainter *painter, const QPointF &centre, const qreal size, const QColor &color) {
        QPainterPath path;
        path.moveTo(centre.x() - size, centre.y() - size * 0.45);
        path.lineTo(centre.x(), centre.y() + size * 0.45);
        path.lineTo(centre.x() + size, centre.y() - size * 0.45);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(path);
    }

    // Строка выпадающего списка: скруглённая подсветка и галочка у текущего.
    class PopupDelegate final : public QStyledItemDelegate {
    public:
        using QStyledItemDelegate::QStyledItemDelegate;

        QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
            Q_UNUSED(option)
            Q_UNUSED(index)
            return {0, kCtlPopupRowHeight};
        }

        void paint(QPainter *painter, const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override {
            const Fsnt::Palette p = Fsnt::CurrentPalette();
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);

            const QRect row = option.rect.adjusted(4, 1, -4, -1);
            const bool active = option.state & (QStyle::State_Selected | QStyle::State_MouseOver);
            if (active) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(p.accentSoft);
                painter->drawRoundedRect(row, 7, 7);
            }

            const bool current = index.data(Qt::UserRole + 77).toBool();
            const QRect textRect = row.adjusted(12, 0, -30, 0);
            painter->setFont(option.font);
            painter->setPen(p.text);
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                              QFontMetrics(option.font).elidedText(
                                  index.data(Qt::DisplayRole).toString(),
                                  Qt::ElideRight, textRect.width()));

            if (current) {
                drawCheck(painter, QPointF(row.right() - 15, row.center().y() + 1), 9, p.accent);
            }
            painter->restore();
        }
    };
}

// ------------------------------------------------------------------ FsntSwitch

FsntSwitch::FsntSwitch(QWidget *parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setAttribute(Qt::WA_Hover, true);

    m_slide = new QVariantAnimation(this);
    m_slide->setDuration(kCtlSlideMs);
    m_slide->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_slide, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_position = value.toReal();
        update();
    });

    connect(this, &QAbstractButton::toggled, this, [this](const bool on) {
        m_slide->stop();
        m_slide->setStartValue(m_position);
        m_slide->setEndValue(on ? 1.0 : 0.0);
        m_slide->start();
    });
}

QSize FsntSwitch::sizeHint() const {
    return {kCtlSwitchWidth, kCtlSwitchHeight};
}

void FsntSwitch::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    const Fsnt::Palette p = Fsnt::CurrentPalette();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF track(0, (height() - kCtlSwitchHeight) / 2.0, kCtlSwitchWidth, kCtlSwitchHeight);
    const qreal radius = track.height() / 2.0;

    QColor off = p.dark ? p.cardHover : p.border;
    if (m_hovered) off = off.lighter(p.dark ? 118 : 96);
    // Цвет дорожки смешиваем вручную: так тумблер догоняет ползунок,
    // а не перекрашивается скачком в конце анимации.
    const QColor trackColor = QColor::fromRgbF(
        off.redF() + (p.accent.redF() - off.redF()) * m_position,
        off.greenF() + (p.accent.greenF() - off.greenF()) * m_position,
        off.blueF() + (p.accent.blueF() - off.blueF()) * m_position);

    painter.setPen(Qt::NoPen);
    painter.setBrush(trackColor);
    painter.drawRoundedRect(track, radius, radius);

    const qreal knobSize = kCtlSwitchHeight - kCtlSwitchPadding * 2;
    const qreal travel = kCtlSwitchWidth - knobSize - kCtlSwitchPadding * 2;
    const QRectF knob(track.left() + kCtlSwitchPadding + travel * m_position,
                      track.top() + kCtlSwitchPadding, knobSize, knobSize);

    painter.setBrush(QColor("#FFFFFF"));
    painter.drawEllipse(knob);
}

void FsntSwitch::enterEvent(QEnterEvent *event) {
    Q_UNUSED(event)
    m_hovered = true;
    update();
}

void FsntSwitch::leaveEvent(QEvent *event) {
    Q_UNUSED(event)
    m_hovered = false;
    update();
}

// ------------------------------------------------------------------ FsntSelect

FsntSelect::FsntSelect(QWidget *parent) : QComboBox(parent) {
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);

    auto *list = new QListView(this);
    list->setUniformItemSizes(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    setView(list);
    view()->setItemDelegate(new PopupDelegate(view()));

    // Непустой лист обязателен: на macOS QComboBox без него открывает нативное
    // меню, мимо нашего делегата и палитры.
    setStyleSheet(QStringLiteral("FsntSelect { border: 0; }"));
}

QSize FsntSelect::sizeHint() const {
    return {qMax(160, QComboBox::sizeHint().width() + 40), kCtlSelectHeight};
}

void FsntSelect::showPopup() {
    // Делегат не знает, какой пункт выбран: State_Selected в списке означает
    // подсветку под курсором. Помечаем текущий отдельной ролью.
    for (int i = 0; i < count(); ++i) {
        setItemData(i, i == currentIndex(), Qt::UserRole + 77);
    }

    const Fsnt::Palette p = Fsnt::CurrentPalette();
    view()->setStyleSheet(QString(
        "QAbstractItemView { background: %1; border: 1px solid %2; border-radius: %3px;"
        " outline: none; padding: 4px; }")
        .arg(p.card.name(QColor::HexRgb), p.border.name(QColor::HexRgb))
        .arg(Fsnt::kRowRadius));

    QComboBox::showPopup();

    // Qt ставит список поверх текущего пункта — привычка нативного меню macOS.
    // Роняем его под поле: так это читается как список поля, а не как меню,
    // и поле остаётся видно.
    if (auto *container = view()->window(); container != nullptr && container != window()) {
        // У контейнера своя рамка поверх нашей: гасим, скругление даёт сам список.
        container->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        container->resize(qMax(width(), container->width()), container->height());
        container->move(mapToGlobal(QPoint(0, height() + 4)));
    }
}

void FsntSelect::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    const Fsnt::Palette p = Fsnt::CurrentPalette();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setBrush(p.card);
    painter.setPen(QPen(m_hovered || hasFocus() ? p.accent : p.border, 1.0));
    painter.drawRoundedRect(frame, Fsnt::kRowRadius, Fsnt::kRowRadius);

    const QRect textRect = rect().adjusted(kCtlSelectPadding, 0, -(kCtlSelectPadding + 20), 0);
    painter.setPen(p.text);
    painter.setFont(font());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                     QFontMetrics(font()).elidedText(currentText(), Qt::ElideRight,
                                                     textRect.width()));

    drawChevron(&painter, QPointF(rect().right() - kCtlSelectPadding - 4, rect().center().y() + 1),
                5, p.textMuted);
}

void FsntSelect::enterEvent(QEnterEvent *event) {
    Q_UNUSED(event)
    m_hovered = true;
    update();
}

void FsntSelect::leaveEvent(QEvent *event) {
    Q_UNUSED(event)
    m_hovered = false;
    update();
}

// ---------------------------------------------------------------- значки

namespace Fsnt {
    void PaintGlyph(QPainter *painter, const Glyph glyph, const QRectF &box, const QColor &color) {
        paintGlyphImpl(painter, glyph, box, color);
    }

    QIcon GlyphIcon(const Glyph glyph, const int size, const QColor &color) {
        QPixmap pixmap(QSize(size, size) * 2);   // с запасом под Retina
        pixmap.setDevicePixelRatio(2.0);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        paintGlyphImpl(&painter, glyph, QRectF(0, 0, size, size), color);
        painter.end();
        return QIcon(pixmap);
    }
} // namespace Fsnt

// -------------------------------------------------------------- FsntIconButton

FsntIconButton::FsntIconButton(const Fsnt::Glyph glyph, QWidget *parent)
    : QAbstractButton(parent), m_glyph(glyph) {
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::NoFocus);
}

QSize FsntIconButton::sizeHint() const {
    return {kCtlIconButtonSide, kCtlIconButtonSide};
}

void FsntIconButton::setFlat(const bool flat) {
    m_flat = flat;
    update();
}

void FsntIconButton::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    update();
}

void FsntIconButton::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    const Fsnt::Palette p = Fsnt::CurrentPalette();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (!m_flat) {
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setBrush(m_active ? p.accentSoft : p.card);
        painter.setPen(QPen(m_hovered || m_active ? p.accent : p.border, 1.0));
        painter.drawRoundedRect(frame, Fsnt::kRowRadius, Fsnt::kRowRadius);
    } else if (m_hovered) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(p.cardHover);
        painter.drawRoundedRect(rect(), Fsnt::kRowRadius, Fsnt::kRowRadius);
    }

    const qreal inset = m_flat ? 9.0 : 10.0;
    paintGlyphImpl(&painter, m_glyph, QRectF(rect()).adjusted(inset, inset, -inset, -inset),
               m_hovered || m_active ? p.accent : p.textMuted);
}

void FsntIconButton::enterEvent(QEnterEvent *event) {
    Q_UNUSED(event)
    m_hovered = true;
    update();
}

void FsntIconButton::leaveEvent(QEvent *event) {
    Q_UNUSED(event)
    m_hovered = false;
    update();
}

// ---------------------------------------------------------------- подтверждение

namespace Fsnt {
    QPixmap BrandMark(const int size, const qreal devicePixelRatio) {
        // Вектор рисуем под плотность экрана: на Retina иначе получится мыло.
        QPixmap pixmap(QSize(size, size) * devicePixelRatio);
        pixmap.setDevicePixelRatio(devicePixelRatio);
        pixmap.fill(Qt::transparent);

        QSvgRenderer renderer(QStringLiteral(":/brand/fn-logo.svg"));
        if (!renderer.isValid()) return pixmap;

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&painter, QRectF(0, 0, size, size));

        // Перекрашиваем в цвет текста, сохраняя альфу: знак одноцветный и почти
        // белый, на светлой теме он сливался с фоном шапки до невидимости.
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(QRectF(0, 0, size, size), CurrentPalette().text);
        return pixmap;
    }

    bool Confirm(QWidget *parent, const QString &title, const QString &text,
                 const QString &acceptText, const QString &rejectText) {
        QDialog dialog(parent);
        dialog.setObjectName(QStringLiteral("fsntDialog"));
        dialog.setWindowTitle(software_name);
        dialog.setModal(true);
        dialog.setStyleSheet(BuildStyleSheet());

        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(24, 22, 24, 20);
        layout->setSpacing(12);

        auto *heading = new QLabel(title, &dialog);
        heading->setObjectName(QStringLiteral("fsntDialogTitle"));
        layout->addWidget(heading);

        auto *body = new QLabel(text, &dialog);
        body->setObjectName(QStringLiteral("fsntDialogHint"));
        body->setWordWrap(true);
        // Короткому вопросу хватает узкого окна, а объяснение в несколько
        // абзацев в колонке 320 px превращается в столбик.
        body->setMinimumWidth(text.length() > 160 ? 420 : 320);
        layout->addWidget(body);
        layout->addSpacing(6);

        auto *cancel = new QPushButton(rejectText.isEmpty() ? QObject::tr("Cancel") : rejectText, &dialog);
        cancel->setObjectName(QStringLiteral("fsntGhost"));
        cancel->setCursor(Qt::PointingHandCursor);
        QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

        auto *accept = new QPushButton(acceptText, &dialog);
        accept->setObjectName(QStringLiteral("fsntPrimary"));
        accept->setCursor(Qt::PointingHandCursor);
        accept->setDefault(true);
        QObject::connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);

        auto *buttons = new QHBoxLayout;
        buttons->addStretch();
        buttons->addWidget(cancel);
        buttons->addWidget(accept);
        layout->addLayout(buttons);

        return dialog.exec() == QDialog::Accepted;
    }

    bool ConfirmSubscription(QWidget *parent, const QString &name, const QString &url,
                             bool &autoUpdate) {
        QDialog dialog(parent);
        dialog.setObjectName(QStringLiteral("fsntDialog"));
        dialog.setWindowTitle(QObject::tr("Add subscription"));
        dialog.setModal(true);
        dialog.setStyleSheet(BuildStyleSheet());
        dialog.setMinimumWidth(460);

        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(24, 22, 24, 20);
        layout->setSpacing(12);

        auto *heading = new QLabel(QObject::tr("Add subscription"), &dialog);
        heading->setObjectName(QStringLiteral("fsntDialogTitle"));
        layout->addWidget(heading);

        auto *hint = new QLabel(QObject::tr("This link came from outside the app. Check the address: "
                                            "it is what gives the client your servers."), &dialog);
        hint->setObjectName(QStringLiteral("fsntDialogHint"));
        hint->setWordWrap(true);
        layout->addWidget(hint);

        auto *card = new QWidget(&dialog);
        card->setObjectName(QStringLiteral("fsntCard"));
        auto *cardBox = new QVBoxLayout(card);
        cardBox->setContentsMargins(14, 12, 14, 12);
        cardBox->setSpacing(6);

        auto *nameLabel = new QLabel(name, card);
        nameLabel->setObjectName(QStringLiteral("fsntRowLabel"));
        nameLabel->setWordWrap(true);
        cardBox->addWidget(nameLabel);

        // Длинный адрес переносим по символам: в ссылке нет пробелов, и без
        // этого карточка растягивает окно на всю ширину экрана.
        auto *urlLabel = new QLabel(url, card);
        urlLabel->setObjectName(QStringLiteral("fsntRowNote"));
        urlLabel->setWordWrap(true);
        urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        cardBox->addWidget(urlLabel);

        layout->addWidget(card);

        auto *toggleRow = new QHBoxLayout;
        auto *toggleLabel = new QLabel(QObject::tr("Keep this subscription updated"), &dialog);
        toggleLabel->setObjectName(QStringLiteral("fsntRowLabel"));
        auto *toggle = new FsntSwitch(&dialog);
        toggle->setChecked(autoUpdate);
        toggleRow->addWidget(toggleLabel, 1);
        toggleRow->addWidget(toggle);
        layout->addLayout(toggleRow);

        layout->addSpacing(4);

        auto *cancel = new QPushButton(QObject::tr("Cancel"), &dialog);
        cancel->setObjectName(QStringLiteral("fsntGhost"));
        cancel->setCursor(Qt::PointingHandCursor);
        QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

        auto *accept = new QPushButton(QObject::tr("Add"), &dialog);
        accept->setObjectName(QStringLiteral("fsntPrimary"));
        accept->setCursor(Qt::PointingHandCursor);
        accept->setDefault(true);
        QObject::connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);

        auto *buttons = new QHBoxLayout;
        buttons->addStretch();
        buttons->addWidget(cancel);
        buttons->addWidget(accept);
        layout->addLayout(buttons);

        const bool accepted = dialog.exec() == QDialog::Accepted;
        if (accepted) autoUpdate = toggle->isChecked();
        return accepted;
    }

    int Choose(QWidget *parent, const QString &title, const QString &text, const QStringList &actions) {
        QDialog dialog(parent);
        dialog.setObjectName(QStringLiteral("fsntDialog"));
        dialog.setWindowTitle(software_name);
        dialog.setModal(true);
        dialog.setStyleSheet(BuildStyleSheet());
        dialog.setMinimumWidth(460);

        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(24, 22, 24, 20);
        layout->setSpacing(12);

        auto *heading = new QLabel(title, &dialog);
        heading->setObjectName(QStringLiteral("fsntDialogTitle"));
        layout->addWidget(heading);

        auto *body = new QLabel(text, &dialog);
        body->setObjectName(QStringLiteral("fsntDialogHint"));
        body->setWordWrap(true);
        body->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        body->setTextInteractionFlags(Qt::TextSelectableByMouse);

        // Список изменений в релизе бывает длинным, поэтому текст прокручивается
        // внутри окна, а не растит его во весь экран.
        auto *scroll = new QScrollArea(&dialog);
        scroll->setWidget(body);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setMaximumHeight(kCtlNoticeMaxHeight);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        scroll->viewport()->setAutoFillBackground(false);
        scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
        layout->addWidget(scroll);
        layout->addSpacing(4);

        int chosen = -1;
        auto *buttons = new QHBoxLayout;
        buttons->addStretch();
        // Идём с конца: основное действие стоит первым в списке, а на экране
        // ему место справа, у большого пальца.
        for (int i = actions.size() - 1; i >= 0; --i) {
            auto *button = new QPushButton(actions.at(i), &dialog);
            button->setObjectName(i == 0 ? QStringLiteral("fsntPrimary") : QStringLiteral("fsntGhost"));
            button->setCursor(Qt::PointingHandCursor);
            if (i == 0) button->setDefault(true);
            QObject::connect(button, &QPushButton::clicked, &dialog, [&dialog, &chosen, i] {
                chosen = i;
                dialog.accept();
            });
            buttons->addWidget(button);
        }
        layout->addLayout(buttons);

        dialog.exec();
        return chosen;
    }

    void Notice(QWidget *parent, const QString &title, const QString &text) {
        QDialog dialog(parent);
        dialog.setObjectName(QStringLiteral("fsntDialog"));
        dialog.setWindowTitle(software_name);
        dialog.setModal(true);
        dialog.setStyleSheet(BuildStyleSheet());

        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(24, 22, 24, 20);
        layout->setSpacing(12);

        auto *heading = new QLabel(title, &dialog);
        heading->setObjectName(QStringLiteral("fsntDialogTitle"));
        heading->setWordWrap(true);
        layout->addWidget(heading);

        auto *body = new QLabel(text, &dialog);
        body->setObjectName(QStringLiteral("fsntDialogHint"));
        body->setWordWrap(true);
        body->setMinimumWidth(340);
        body->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        // Технические строки от ядра приходят одной длинной фразой; дать их
        // выделить полезнее, чем заставлять переписывать с экрана.
        body->setTextInteractionFlags(Qt::TextSelectableByMouse);

        // Текст сюда приходит и от ядра, и от провайдера, и длину его никто не
        // ограничивает. Без прокрутки такое окно вырастает выше экрана, и
        // кнопка закрытия оказывается там, куда не дотянуться.
        auto *scroll = new QScrollArea(&dialog);
        scroll->setObjectName(QStringLiteral("fsntNoticeScroll"));
        scroll->setWidget(body);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setMaximumHeight(kCtlNoticeMaxHeight);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        scroll->viewport()->setAutoFillBackground(false);
        scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
        layout->addWidget(scroll);
        layout->addSpacing(6);

        auto *close = new QPushButton(QObject::tr("Close"), &dialog);
        close->setObjectName(QStringLiteral("fsntPrimary"));
        close->setCursor(Qt::PointingHandCursor);
        close->setDefault(true);
        QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);

        auto *buttons = new QHBoxLayout;
        buttons->addStretch();
        buttons->addWidget(close);
        layout->addLayout(buttons);

        dialog.exec();
    }
} // namespace Fsnt
