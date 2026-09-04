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

            QComboBox#fsntGroupSwitch, QLineEdit#fsntSearch {
                background: %10;
                border: 1px solid %3;
                border-radius: %6px;
                padding: 6px 9px;
                color: %2;
                font-size: 13px;
            }
            QComboBox#fsntGroupSwitch::drop-down { border: none; width: 18px; }
            QLineEdit#fsntSearch { background: %10; }

            QPushButton#fsntIconSquare {
                background: %10;
                border: 1px solid %3;
                border-radius: %6px;
                color: %2;
                font-size: 15px;
            }
            QPushButton#fsntIconSquare:hover { border-color: %4; }

            QPushButton#fsntTab {
                background: transparent;
                border: none;
                border-radius: %6px;
                padding: 5px 10px;
                color: %8;
                font-size: 12px;
            }
            QPushButton#fsntTab:checked { background: %10; color: %2; }
            QPushButton#fsntTab:hover { color: %2; }

            QListWidget#fsntServerList {
                background: transparent;
                border: none;
                outline: none;
            }

            QWidget#fsntSubscriptionCard {
                background: %10;
                border-radius: %11px;
            }
            QLabel#fsntSubName { color: %2; font-size: 13px; }
            QLabel#fsntSubMeta { color: %8; font-size: 11px; }
            QProgressBar#fsntTrafficBar {
                background: %1;
                border: none;
                border-radius: 3px;
            }
            QProgressBar#fsntTrafficBar::chunk {
                background: %4;
                border-radius: 3px;
            }

            QLabel#fsntElapsed { color: %2; font-size: 26px; }
            QLabel#fsntStatus { color: %8; font-size: 13px; }
            QLabel#fsntCurrentServer { color: %2; font-size: 14px; }

            QPushButton#fsntPowerButton {
                border: 2px solid %3;
                border-radius: 60px;
                background: transparent;
                color: %8;
                font-size: 34px;
            }
            QPushButton#fsntPowerButton:hover { border-color: %4; }
            QPushButton#fsntPowerButton[connected="true"] {
                border-color: %9;
                color: %9;
            }
        )")
            .arg(c(t.surface),                    // 1
                 c(t.onSurface),                  // 2
                 c(t.borderSubtle),               // 3
                 c(t.accent),                     // 4
                 c(t.onAccent),                   // 5
                 QString::number(kRowRadius),     // 6
                 c(t.hoverFill),                  // 7
                 c(t.muted),                      // 8
                 c(t.success),                    // 9
                 c(t.hoverFill),                  // 10
                 QString::number(kCardRadius));    // 11
    }
}
