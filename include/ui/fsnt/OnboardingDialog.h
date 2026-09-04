#pragma once

#include <QDialog>

class FsntSwitch;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTimer;
class QWidget;

// Мастер первого запуска: знакомство, подписка, режим подключения, готово.
//
// Без него первый запуск выглядел как пустой список без объяснений: откуда
// брать серверы и чем TUN отличается от системного прокси, знать неоткуда.
class OnboardingDialog : public QDialog {
    Q_OBJECT

public:
    explicit OnboardingDialog(QWidget *parent = nullptr);

    // Мастер нужен только новому пользователю. У того, кто обновился с уже
    // заведёнными подписками, знакомство только отняло бы время.
    static bool ShouldRun();
    static void MarkDone();

private:
    QWidget *buildWelcome();
    QWidget *buildSubscription();
    QWidget *buildTransport();
    QWidget *buildFinish();

    void goTo(int page);
    void submitSubscription();
    void selectTransport(int transport);
    void finish();
    void refreshDots();

    QPixmap renderLogo(int size) const;

    QStackedWidget *m_pages = nullptr;
    QWidget *m_dots = nullptr;

    QLineEdit *m_subInput = nullptr;
    QLabel *m_subStatus = nullptr;
    QPushButton *m_subAdd = nullptr;
    QPushButton *m_subSkip = nullptr;
    QTimer *m_subGuard = nullptr;
    bool m_subBusy = false;

    QPushButton *m_tun = nullptr;
    QPushButton *m_proxy = nullptr;
    FsntSwitch *m_autoRun = nullptr;
};
