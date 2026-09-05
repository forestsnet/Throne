#pragma once

#include <QMainWindow>
#include <QPixmap>

#include "include/global/Utils.hpp"

class ConnectPanel;
class FsntLogDialog;
class FsntToast;
class ServerListPanel;
class SubscriptionCard;
class QLabel;
class QVBoxLayout;

namespace Fsnt { class CoachMarks; }

// Потребительское окно FSNT Client. В этом приросте панели пустые:
// список серверов, кнопку подключения и карточку подписки добавляют
// следующие приросты, каждый в своём файле.
class FsntWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit FsntWindow(QWidget *parent = nullptr);

signals:
    // Строка журнала. Окно перехватывает MW_show_log один раз и раздаёт
    // сигналом: снимать перехват при закрытии диалога было бы легко забыть,
    // а ошибка здесь оставила бы вызов по указателю на удалённое окно.
    void logLine(const QString &line);

protected:
    // Тост центрируем сами: он лежит поверх компоновки, и та его не двигает.
    void resizeEvent(QResizeEvent *event) override;
    // Крестик прячет окно в трей. Без этого закрытие окна гасило единственное
    // видимое окно приложения, Qt завершал программу — и туннель вместе с ней.
    void closeEvent(QCloseEvent *event) override;

    // Точки расширения для следующих приростов.
    QVBoxLayout *serverPanelLayout() const { return m_serverLayout; }
    QVBoxLayout *sidePanelLayout() const { return m_sideLayout; }

private:
    void chainCoreMessages();
    void chainLogLines();
    void onCoreMessage(MwMessage cmd, const QStringList &args);
    void refreshConnectionState();
    void refreshServerList();
    QPixmap renderLogo(int size);
    void buildHeader(QVBoxLayout *root);
    void buildPanels(QVBoxLayout *root);
    void applyTheme();
    void switchToAdvancedMode();
    // Меню «ещё» в шапке: очевидные действия, которые незачем искать в настройках.
    void showMainMenu(QWidget *anchor);
    // Дёргает именованное действие MainWindow: он остаётся движком, и повторять
    // его логику здесь было бы вторым местом, где её надо чинить.
    static void triggerAction(const char *actionName);
    // Диалог живёт здесь, а не в панелях: его открывают и список серверов,
    // и панель подключения, когда запускать нечего.
    void openAddSubscription();
    // Экскурсия по интерфейсу. Первый запуск показывает её сам, повторить
    // можно из меню «ещё».
    static bool shouldRunTour();
    void runTour();

    ConnectPanel *m_connectPanel = nullptr;
    ServerListPanel *m_serverList = nullptr;
    SubscriptionCard *m_subscriptionCard = nullptr;
    FsntToast *m_toast = nullptr;
    QLabel *m_logo = nullptr;
    // Кнопки шапки держим не ради обработчиков, а как цели подсветки.
    QWidget *m_gear = nullptr;
    QWidget *m_logs = nullptr;
    QWidget *m_more = nullptr;
    Fsnt::CoachMarks *m_tour = nullptr;
    QVBoxLayout *m_serverLayout = nullptr;
    QVBoxLayout *m_sideLayout = nullptr;
};
