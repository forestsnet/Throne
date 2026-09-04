#pragma once

#include <QString>

class FsntSwitch;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace Fsnt {
    // Карточка настроек: подпись раздела сверху, под ней строки с волосяными
    // разделителями.
    //
    // Раньше здесь был QFormLayout, и получалось криво: пустая колонка подписи
    // съедала половину ширины, поля вставали на разном расстоянии от края, а
    // кнопки растягивались в стопку одинаковых серых полос. Строка «подпись
    // слева, контрол справа» решает всё это разом.
    class SettingsCard {
    public:
        SettingsCard(QVBoxLayout *column, QWidget *host, const QString &title);

        // Строка с подписью слева и готовым контролом справа.
        void addControl(const QString &label, QWidget *control);

        // Переключатель: текст слева, тумблер справа.
        FsntSwitch *addToggle(const QString &label, bool checked);

        // Строка-действие: подпись и шеврон, нажимается целиком.
        QPushButton *addAction(const QString &label);

        // Пояснение мелким шрифтом, без разделителя сверху.
        void addNote(const QString &text);

    private:
        void addSeparator();

        QWidget *m_card = nullptr;
        QVBoxLayout *m_rows = nullptr;
        bool m_empty = true;
    };
} // namespace Fsnt
