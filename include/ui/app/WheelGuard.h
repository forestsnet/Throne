#pragma once

#include <QObject>

class QCoreApplication;

// The mouse wheel scrolls the page it is over and never changes the value of a
// field it passes. Installed on the application, see GuiStartup::InstallWheelGuard.
//
// The advanced dialogs are taller than their window and therefore scroll, and a
// combo box or a spin box takes the wheel for itself: scrolling the page changed
// whatever field the pointer happened to be over, and OK then wrote that choice
// down with the rest of them. Nothing said it had happened, and the field that
// moved is not the one being looked at.
//
// It sits on the application rather than on each dialog. There are thirty-odd
// such fields across a dozen forms, the profile editors are mounted at runtime
// from any of thirty .ui files, and a form added later would otherwise arrive
// unguarded.
class WheelGuard : public QObject {
public:
    using QObject::QObject;

    // Parented to the application, nothing to keep hold of.
    static void InstallOn(QCoreApplication &app);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};
