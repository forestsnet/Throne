#include "include/ui/fsnt/WindowChrome.hpp"

#include <QApplication>
#include <QEvent>
#include <QWidget>

#include "include/ui/fsnt/FsntPalette.hpp"
#include "include/ui/setting/ThemeManager.hpp"

#ifdef Q_OS_WIN
#include "include/sys/windows/guihelper.h"
#endif

namespace {
    // Наблюдатель за показом окон. Красить только при создании мало: диалоги
    // рождаются по ходу работы, и каждый пришёл бы со светлым заголовком.
    class WindowChromeWatcher : public QObject {
    public:
        using QObject::QObject;

    protected:
        bool eventFilter(QObject *watched, QEvent *event) override {
            if (event->type() == QEvent::Show) {
                if (auto *widget = qobject_cast<QWidget *>(watched);
                    widget != nullptr && widget->isWindow()) {
                    Fsnt::ApplyWindowChrome(widget);
                }
            }
            return QObject::eventFilter(watched, event);
        }
    };
}

namespace Fsnt {
    void ApplyWindowChrome(QWidget *window) {
#ifdef Q_OS_WIN
        if (window == nullptr) return;
        Windows_SetDarkTitleBar(window, CurrentPalette().dark);
#else
        Q_UNUSED(window)
#endif
    }

    void RefreshWindowChrome() {
#ifdef Q_OS_WIN
        for (QWidget *window : QApplication::topLevelWidgets()) {
            // Скрытому окну ставить нечего: winId() создал бы ему нативный
            // дескриптор раньше времени, а при показе мы придём сюда снова.
            if (window->isVisible()) ApplyWindowChrome(window);
        }
#endif
    }

    void InstallWindowChromeWatcher() {
#ifdef Q_OS_WIN
        static WindowChromeWatcher *watcher = nullptr;
        if (watcher != nullptr) return;
        watcher = new WindowChromeWatcher(qApp);
        qApp->installEventFilter(watcher);
        QObject::connect(themeManager(), &ThemeManager::themeChanged, qApp,
                         [] { RefreshWindowChrome(); });
#endif
    }
} // namespace Fsnt
