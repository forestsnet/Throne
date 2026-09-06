#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

// Добавление подписки без похода в расширенный режим.
//
// Раньше это было единственное, ради чего клиенту приходилось переключать
// интерфейс: в простом окне подписку добавить было нельзя вообще.
class AddSubscriptionDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddSubscriptionDialog(QWidget *parent = nullptr);

private:
    void submit();
    void pasteFromClipboard();
    void setBusy(bool busy);
    void fail(const QString &message);
    // Почему подписка не открылась — словами, которые человеку что-то говорят.
    static QString subscriptionFailureHint();

    QLineEdit *m_input = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_cancel = nullptr;
    // Обновление идёт в фоне и о неуспехе молчит: без сторожа диалог завис бы
    // в состоянии «добавляем» навсегда.
    QTimer *m_guard = nullptr;
    bool m_busy = false;
};
