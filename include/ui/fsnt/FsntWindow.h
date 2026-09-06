#pragma once

#include <QMainWindow>
#include <QPixmap>

#include "include/global/Utils.hpp"
#include "include/ui/fsnt/UpdateWatcher.hpp"

class FsntIconButton;
class ConnectPanel;
class FsntLogDialog;
class FsntToast;
class ServerListPanel;
class SubscriptionCard;
class QLabel;
class QVBoxLayout;

namespace Fsnt { class CoachMarks; class TunnelProbe; }

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

    // Вторая подписка меняет правила игры: серверы теперь лежат в двух местах,
    // и переключатель, на который раньше не было причин смотреть, становится
    // главным. Показываем его один раз — в тот момент, когда это случилось.
    void maybeHintSubscriptionSwitch();

    // Нашлась новая версия: зажигаем колокольчик и, если человек не запретил,
    // один раз показываем системное уведомление.
    void onUpdateFound(const QString &tag, const QString &notes);
    void showUpdateCard();
    void runTour();
    // Туннель поднят, но трафик не идёт: предлагаем совместимый стек.
    void offerCompatibleTunnel();

    ConnectPanel *m_connectPanel = nullptr;
    ServerListPanel *m_serverList = nullptr;
    SubscriptionCard *m_subscriptionCard = nullptr;
    FsntToast *m_toast = nullptr;
    QLabel *m_logo = nullptr;
    // Кнопки шапки держим не ради обработчиков, а как цели подсветки.
    // Колокольчик появляется только когда есть что сказать: постоянный значок,
    // который девять дней из десяти ничего не значит, глаз перестаёт замечать.
    FsntIconButton *m_bell = nullptr;
    QWidget *m_bellDot = nullptr;
    Fsnt::UpdateWatcher *m_updates = nullptr;
    QString m_updateTag;
    QString m_updateNotes;

    QWidget *m_gear = nullptr;
    QWidget *m_logs = nullptr;
    QWidget *m_more = nullptr;
    Fsnt::CoachMarks *m_tour = nullptr;
    Fsnt::TunnelProbe *m_probe = nullptr;
    // Предлагаем сменить стек один раз за запуск: если человек отказался,
    // повторять при каждом переподключении значило бы навязываться.
    bool m_compatibleOffered = false;
    QVBoxLayout *m_serverLayout = nullptr;
    QVBoxLayout *m_sideLayout = nullptr;
};
