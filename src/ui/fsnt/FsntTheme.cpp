#include "include/ui/fsnt/FsntTheme.hpp"

#include <algorithm>

#include <QHash>
#include <QStringList>

#include "include/ui/setting/ThemeManager.hpp"

namespace Fsnt {
    QString BuildStyleSheet(const ThemeTokens &t) {
        const auto c = [](const QColor &color) { return color.name(QColor::HexRgb); };

        QString sheet = QString(R"(
            QWidget#fsntRoot { background: @surface; color: @onSurface; }

            QWidget#fsntHeader {
                background: @surface;
                border-bottom: 1px solid @border;
            }
            QLabel#fsntTitle { color: @onSurface; font-size: 13px; }
            QLabel#fsntLogo { background: transparent; }

            QWidget#fsntServerPanel { border-right: 1px solid @border; }

            QToolButton#fsntIconButton {
                border: none;
                border-radius: @rowRadiuspx;
                padding: 4px 8px;
                color: @onSurface;
            }
            QToolButton#fsntIconButton:hover { background: @hover; }

            QLabel#fsntPlaceholder { color: @muted; font-size: 13px; }

            QComboBox#fsntGroupSwitch, QLineEdit#fsntSearch {
                background: @fill;
                border: 1px solid @border;
                border-radius: @rowRadiuspx;
                padding: 6px 9px;
                color: @onSurface;
                font-size: 13px;
            }
            QComboBox#fsntGroupSwitch::drop-down { border: none; width: 18px; }
            QLineEdit#fsntSearch { background: @fill; }

            QPushButton#fsntIconSquare {
                background: @fill;
                border: 1px solid @border;
                border-radius: @rowRadiuspx;
                color: @onSurface;
                font-size: 15px;
            }
            QPushButton#fsntIconSquare:hover { border-color: @accent; }

            QPushButton#fsntTab {
                background: transparent;
                border: none;
                border-radius: @rowRadiuspx;
                padding: 5px 10px;
                color: @muted;
                font-size: 12px;
            }
            QPushButton#fsntTab:checked { background: @fill; color: @onSurface; }
            QPushButton#fsntTab:hover { color: @onSurface; }

            QListWidget#fsntServerList {
                background: transparent;
                border: none;
                outline: none;
            }

            QWidget#fsntSubscriptionCard {
                background: @fill;
                border-radius: @cardRadiuspx;
            }
            QLabel#fsntSubName { color: @onSurface; font-size: 13px; }
            QLabel#fsntSubMeta { color: @muted; font-size: 11px; }
            QProgressBar#fsntTrafficBar {
                background: @surface;
                border: none;
                border-radius: 3px;
            }
            QProgressBar#fsntTrafficBar::chunk {
                background: @accent;
                border-radius: 3px;
            }

            QLabel#fsntElapsed { color: @onSurface; font-size: 26px; }
            QLabel#fsntStatus { color: @muted; font-size: 13px; }
            QLabel#fsntCurrentServer { color: @onSurface; font-size: 14px; }

            QPushButton#fsntPowerButton {
                border: 2px solid @border;
                border-radius: 60px;
                background: transparent;
                color: @muted;
                font-size: 34px;
            }
            QPushButton#fsntPowerButton:hover { border-color: @accent; }
            QPushButton#fsntPowerButton[connected="true"] {
                border-color: @success;
                color: @success;
            }
        )")
            ;

        // Именованные подстановки вместо %N: многоаргументный QString::arg раскладывает
        // значения по тем плейсхолдерам, что реально есть в строке. Стоит убрать
        // единственное использование одного номера — и все старшие молча съезжают.
        const QHash<QString, QString> vars = {
            {"@surface",    c(t.surface)},
            {"@onSurface",  c(t.onSurface)},
            {"@border",     c(t.borderSubtle)},
            {"@accent",     c(t.accent)},
            {"@hover",      c(t.hoverFill)},
            {"@fill",       c(t.hoverFill)},
            {"@muted",      c(t.muted)},
            {"@success",    c(t.success)},
            {"@danger",     c(t.danger)},
            {"@rowRadius",  QString::number(kRowRadius)},
            {"@cardRadius", QString::number(kCardRadius)},
        };

        // Длинные имена первыми: иначе @fill съел бы начало другого токена.
        QStringList names = vars.keys();
        std::sort(names.begin(), names.end(),
                  [](const QString &a, const QString &b) { return a.size() > b.size(); });
        for (const QString &name : names) sheet.replace(name, vars.value(name));

        return sheet;    // 11
    }
}
