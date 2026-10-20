#pragma once

#include <QWidget>

class QVariantAnimation;

namespace Fsnt {
    // Уведомление поверх всех окон, своё собственное.
    //
    // QSystemTrayIcon::showMessage на macOS уходит в NSUserNotificationCenter —
    // API, которого в системе больше нет: приложение даже не появляется в списке
    // «Уведомления» в настройках, и баннер не показывается никогда. Поэтому на
    // маке карточку рисуем сами: она видна из любой программы, потому что живёт
    // отдельным окном в углу экрана, а не внутри клиента.
    class DesktopNotice : public QWidget {
        Q_OBJECT

    public:
        // Показывает карточку в правом верхнем углу экрана, где сейчас курсор.
        // Карточка на экране одна: повторный вызов меняет текст, а не копит окна.
        static DesktopNotice *Show(const QString &title, const QString &text, int milliseconds = 12000);

    signals:
        // Щелчок по карточке (но не по крестику).
        void activated();

    protected:
        void paintEvent(QPaintEvent *event) override;
        void mousePressEvent(QMouseEvent *event) override;
        void mouseMoveEvent(QMouseEvent *event) override;
        void leaveEvent(QEvent *event) override;
        void enterEvent(QEnterEvent *event) override;

    private:
        explicit DesktopNotice(QWidget *parent = nullptr);

        void present(const QString &title, const QString &text, int milliseconds);
        void dismiss();
        QRect closeRect() const;

        QString m_title;
        QString m_text;
        bool m_closeHovered = false;
        QVariantAnimation *m_fade = nullptr;
        QVariantAnimation *m_life = nullptr;
    };
} // namespace Fsnt
