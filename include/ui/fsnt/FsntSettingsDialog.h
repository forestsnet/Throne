#pragma once

#include <QDialog>

class FsntSwitch;
class FsntSelect;
class QLineEdit;
class QLabel;
class QVBoxLayout;

// Настройки простого режима.
//
// Раньше здесь было три поля, и за всем остальным — автозапуском, маршрутами,
// per-app прокси, отчётом для поддержки — приходилось идти в расширенный режим.
// Теперь тут всё, что нужно конечному клиенту; в расширенном остаётся то,
// что нужно инженеру.
class FsntSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit FsntSettingsDialog(QWidget *parent = nullptr);

private:
    void save();

    void buildConnection(QVBoxLayout *column, QWidget *host);
    void buildSubscriptions(QVBoxLayout *column, QWidget *host);
    void buildDns(QVBoxLayout *column, QWidget *host);
    void buildApplication(QVBoxLayout *column, QWidget *host);

    // Дёргает действие расширенного режима по имени. Так переиспользуются его
    // проверки — и код, который правит upstream, остаётся нетронутым.
    void triggerMainWindowAction(const char *actionName);

    FsntSelect *m_transport = nullptr;
    FsntSelect *m_language = nullptr;
    FsntSelect *m_route = nullptr;
    // Стек TUN. Только на Windows: там он ломается о чужие сетевые фильтры.
    FsntSelect *m_tunStack = nullptr;
    // Подпись под выбором: объясняет выбранную схему словами.
    QLabel *m_routeNote = nullptr;
    void updateRouteNote();
    FsntSelect *m_subAutoUpdate = nullptr;
    FsntSelect *m_pingKind = nullptr;
    QLineEdit *m_pingUrl = nullptr;
    FsntSwitch *m_startMinimal = nullptr;
    FsntSwitch *m_autoConnect = nullptr;
    FsntSwitch *m_allowLan = nullptr;
    FsntSwitch *m_autoRun = nullptr;
    FsntSelect *m_theme = nullptr;
    FsntSelect *m_remoteDns = nullptr;
    FsntSelect *m_directDns = nullptr;
};
