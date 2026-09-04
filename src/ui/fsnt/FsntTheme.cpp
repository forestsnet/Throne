#include "include/ui/fsnt/FsntTheme.hpp"

#include "include/ui/setting/ThemeManager.hpp"

namespace Fsnt {
    QString BuildStyleSheet(const ThemeTokens &t) {
        const auto c = [](const QColor &color) { return color.name(QColor::HexRgb); };

        return QString(R"(
            QWidget#fsntRoot { background: %1; color: %2; }

            QWidget#fsntHeader {
                background: %1;
                border-bottom: 1px solid %3;
            }
            QLabel#fsntTitle { color: %2; font-size: 13px; }
            QLabel#fsntLogo {
                background: %4;
                color: %5;
                border-radius: 6px;
                font-size: 11px;
                padding: 3px 5px;
            }

            QWidget#fsntServerPanel { border-right: 1px solid %3; }

            QToolButton#fsntIconButton {
                border: none;
                border-radius: %6px;
                padding: 4px 8px;
                color: %2;
            }
            QToolButton#fsntIconButton:hover { background: %7; }

            QLabel#fsntPlaceholder { color: %8; font-size: 13px; }
        )")
            .arg(c(t.surface),                    // 1
                 c(t.onSurface),                  // 2
                 c(t.borderSubtle),               // 3
                 c(t.accent),                     // 4
                 c(t.onAccent),                   // 5
                 QString::number(kRowRadius),     // 6
                 c(t.hoverFill),                  // 7
                 c(t.muted));                     // 8
    }
}
