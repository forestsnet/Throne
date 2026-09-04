#include "include/ui/fsnt/FsntTheme.hpp"

#include <algorithm>

#include <QHash>
#include <QStringList>

#include "include/ui/fsnt/FsntPalette.hpp"

namespace {
    QString hex(const QColor &c) { return c.name(QColor::HexRgb); }

    QString rgba(const QColor &c) {
        return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    }
}

namespace Fsnt {
    QString BuildStyleSheet() {
        const Palette p = CurrentPalette();

        QString sheet = QString(R"(
            /* Сброс наследства общесистемной темы. Наш лист — дополнение к ней,
               а не замена: свойства, которых мы не задали, приходят из qdarkstyle.
               Он красит фон подписям и кнопкам, и на более тёмном фоне простого
               режима эти прямоугольники становятся видны. Правила по id ниже
               перебивают этот сброс по специфичности, порядок значения не имеет. */
            QLabel, QCheckBox, QRadioButton, QToolButton, QPushButton {
                background: transparent;
            }

            QWidget#fsntRoot { background: @bg; color: @text; }
            QWidget#fsntBody { background: @bg; }

            QWidget#fsntHeader {
                background: @surface;
                border-bottom: 1px solid @border;
            }
            QLabel#fsntTitle { color: @text; font-size: 14px; font-weight: 600; }
            QLabel#fsntLogo { background: transparent; }
            QLabel#fsntSectionLabel {
                color: @muted;
                font-size: 11px;
                font-weight: 600;
                text-transform: uppercase;
            }

            QWidget#fsntServerPanel { background: @bg; border-right: 1px solid @border; }
            QWidget#fsntSidePanel { background: @bg; }

            /* ---- кнопки ---- */
            QToolButton#fsntIconButton {
                border: none;
                border-radius: @rowRadiuspx;
                padding: 6px 10px;
                color: @muted;
                font-size: 13px;
            }
            QToolButton#fsntIconButton:hover { background: @cardHover; color: @text; }

            QToolButton#fsntGearButton {
                border: 1px solid @border;
                border-radius: @rowRadiuspx;
                background: @card;
                color: @text;
                font-size: 19px;
            }
            QToolButton#fsntGearButton:hover { border-color: @accent; color: @accent; }

            QPushButton#fsntPrimary {
                background: @accent;
                border: none;
                border-radius: @rowRadiuspx;
                padding: 9px 18px;
                color: #FFFFFF;
                font-size: 13px;
                font-weight: 600;
            }
            QPushButton#fsntPrimary:hover { background: @accentHover; }
            QPushButton#fsntPrimary:disabled { background: @border; color: @muted; }

            QPushButton#fsntGhost {
                background: transparent;
                border: 1px solid @border;
                border-radius: @rowRadiuspx;
                padding: 8px 16px;
                color: @text;
                font-size: 13px;
            }
            QPushButton#fsntGhost:hover { border-color: @accent; color: @accent; }

            QPushButton#fsntIconSquare {
                background: @card;
                border: 1px solid @border;
                border-radius: @rowRadiuspx;
                color: @muted;
                font-size: 15px;
            }
            QPushButton#fsntIconSquare:hover { border-color: @accent; color: @accent; }

            /* ---- поля ---- */
            QComboBox#fsntGroupSwitch, QLineEdit#fsntSearch, QLineEdit#fsntInput,
            QComboBox#fsntSelect {
                background: @card;
                border: 1px solid @border;
                border-radius: @rowRadiuspx;
                padding: 8px 11px;
                color: @text;
                font-size: 13px;
                selection-background-color: @accent;
            }
            QLineEdit#fsntSearch:focus, QLineEdit#fsntInput:focus,
            QComboBox#fsntGroupSwitch:focus, QComboBox#fsntSelect:focus {
                border-color: @accent;
            }
            QComboBox#fsntGroupSwitch::drop-down, QComboBox#fsntSelect::drop-down {
                border: none;
                width: 22px;
            }
            QComboBox QAbstractItemView {
                background: @card;
                border: 1px solid @border;
                border-radius: @rowRadiuspx;
                color: @text;
                selection-background-color: @accentSoft;
                outline: none;
                padding: 4px;
            }

            QCheckBox { color: @text; font-size: 13px; spacing: 8px; }
            QCheckBox::indicator {
                width: 17px; height: 17px;
                border: 1px solid @border;
                border-radius: 5px;
                background: @card;
            }
            QCheckBox::indicator:hover { border-color: @accent; }
            QCheckBox::indicator:checked { background: @accent; border-color: @accent; }

            /* ---- вкладки-пилюли ---- */
            QWidget#fsntTabStrip {
                background: @card;
                border: 1px solid @border;
                border-radius: @rowRadiuspx;
            }
            QPushButton#fsntTab {
                background: transparent;
                border: none;
                border-radius: 6px;
                padding: 6px 12px;
                color: @muted;
                font-size: 12px;
                font-weight: 600;
            }
            QPushButton#fsntTab:checked { background: @accent; color: #FFFFFF; }
            QPushButton#fsntTab:hover:!checked { color: @text; }

            /* ---- список серверов ---- */
            QListWidget#fsntServerList {
                background: transparent;
                border: none;
                outline: none;
            }
            QScrollBar:vertical {
                background: transparent;
                width: 9px;
                margin: 0;
            }
            QScrollBar::handle:vertical {
                background: @border;
                border-radius: 4px;
                min-height: 32px;
            }
            QScrollBar::handle:vertical:hover { background: @muted; }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
            QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }

            /* ---- карточки ---- */
            QWidget#fsntCard, QWidget#fsntSubscriptionCard {
                background: @card;
                border: 1px solid @border;
                border-radius: @cardRadiuspx;
            }
            QLabel#fsntSubName { color: @text; font-size: 14px; font-weight: 600; }
            QLabel#fsntSubMeta { color: @muted; font-size: 11px; }
            QLabel#fsntSubStrong { color: @text; font-size: 12px; }
            QLabel#fsntAnnounce {
                color: @text;
                font-size: 11px;
                background: @accentSoft;
                border-radius: 7px;
                padding: 7px 9px;
            }

            QProgressBar#fsntTrafficBar {
                background: @border;
                border: none;
                border-radius: 3px;
            }
            QProgressBar#fsntTrafficBar::chunk {
                background: @accent;
                border-radius: 3px;
            }

            /* ---- панель подключения ---- */
            QLabel#fsntElapsed {
                color: @text;
                font-size: 34px;
                font-weight: 300;
            }
            QLabel#fsntStatus { color: @muted; font-size: 12px; font-weight: 600; }
            QLabel#fsntStatus[tone="ok"] { color: @success; }
            QLabel#fsntStatus[tone="busy"] { color: @accent; }
            QLabel#fsntCurrentServer { color: @text; font-size: 14px; font-weight: 600; }
            QLabel#fsntPlaceholder { color: @muted; font-size: 13px; }

            /* ---- диалоги простого режима ---- */
            QDialog#fsntDialog { background: @bg; }
            QDialog#fsntDialog QLabel { color: @text; font-size: 13px; }
            QLabel#fsntDialogTitle { color: @text; font-size: 19px; font-weight: 600; }
            QLabel#fsntDialogHint { color: @muted; font-size: 12px; }
        )");

        // Именованные подстановки вместо %N: многоаргументный QString::arg раскладывает
        // значения по тем плейсхолдерам, что реально есть в строке. Стоит убрать
        // единственное использование одного номера — и все старшие молча съезжают.
        const QHash<QString, QString> vars = {
            {"@bg", hex(p.bg)},
            {"@surface", hex(p.surface)},
            {"@cardHover", hex(p.cardHover)},
            {"@card", hex(p.card)},
            {"@border", hex(p.border)},
            {"@textMuted", hex(p.textMuted)},
            {"@text", hex(p.text)},
            {"@muted", hex(p.textMuted)},
            {"@accentSoft", rgba(p.accentSoft)},
            {"@accentHover", hex(p.dark ? p.accent.lighter(115) : p.accent.darker(112))},
            {"@accent", hex(p.accent)},
            {"@success", hex(p.success)},
            {"@danger", hex(p.danger)},
            {"@warn", hex(p.warn)},
            {"@rowRadius", QString::number(kRowRadius)},
            {"@cardRadius", QString::number(kCardRadius)},
        };

        // Длинные имена первыми: иначе @card съел бы начало @cardHover,
        // а @accent — начало @accentSoft.
        QStringList names = vars.keys();
        std::sort(names.begin(), names.end(),
                  [](const QString &a, const QString &b) { return a.size() > b.size(); });
        for (const QString &name : names) sheet.replace(name, vars.value(name));

        return sheet;
    }
} // namespace Fsnt
