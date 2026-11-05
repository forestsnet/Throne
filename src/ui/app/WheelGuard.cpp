#include "include/ui/app/WheelGuard.h"

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QWidget>

namespace {
    // The fields whose value the wheel changes. The simple window's own FsntSelect
    // refuses the wheel by itself as well, so it does not rely on this filter.
    // A scroll bar is not one of them, the wheel is exactly what it is for.
    bool wheelChangesValue(const QObject *object) {
        return qobject_cast<const QComboBox *>(object) != nullptr
               || qobject_cast<const QAbstractSpinBox *>(object) != nullptr;
    }
}

void WheelGuard::InstallOn(QCoreApplication &app) {
    app.installEventFilter(new WheelGuard(&app));
}

bool WheelGuard::eventFilter(QObject *watched, QEvent *event) {
    switch (event->type()) {
        case QEvent::Wheel:
            if (wheelChangesValue(watched)) {
                // Left unaccepted on purpose: Qt carries the wheel up the parent
                // chain for as long as nothing accepts it, so the page under the
                // field scrolls the way it was asked to. An open list is not
                // affected, it has a window of its own.
                event->ignore();
                return true;
            }
            break;
        case QEvent::Polish:
            // Focus is handed out by QApplication before the event is delivered,
            // so no filter can hold it back - only the field's own policy can.
            // Qt::WheelFocus means the wheel moves the keyboard into the field,
            // and then the next arrow key changes it again. A click and Tab still
            // focus it.
            if (auto *widget = qobject_cast<QWidget *>(watched);
                widget != nullptr && widget->focusPolicy() == Qt::WheelFocus && wheelChangesValue(widget)) {
                widget->setFocusPolicy(Qt::StrongFocus);
            }
            break;
        default:
            break;
    }
    return QObject::eventFilter(watched, event);
}
