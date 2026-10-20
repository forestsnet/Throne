#pragma once

#include <QWidget>

namespace Fsnt {
    // Карточка «вышла новая версия», привязанная к колокольчику.
    //
    // Модальное окно посреди экрана требует ответа и обрывает работу; здесь
    // человеку ничего не должно быть должно: нажал мимо — карточка ушла.
    class UpdatePopover : public QWidget {
        Q_OBJECT

    public:
        UpdatePopover(QWidget *anchor, const QString &title, const QString &text);

        // Показать под якорем, прижав к его правому краю.
        void popup();

    signals:
        void updateRequested();
        void notesRequested();
        void postponed();

    private:
        QWidget *m_anchor = nullptr;
    };
} // namespace Fsnt
