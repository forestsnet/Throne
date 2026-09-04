#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QFormLayout;
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

signals:
    void advancedModeRequested();

private:
    void save();

    // Секция = подпись сверху и карточка с полями. Возвращает форму карточки.
    QFormLayout *addSection(QVBoxLayout *column, QWidget *host, const QString &title);

    void buildConnection(QVBoxLayout *column, QWidget *host);
    void buildSubscriptions(QVBoxLayout *column, QWidget *host);
    void buildApplication(QVBoxLayout *column, QWidget *host);
    void buildSupport(QVBoxLayout *column, QWidget *host);

    // Дёргает действие расширенного режима по имени. Так переиспользуются его
    // проверки — и код, который правит upstream, остаётся нетронутым.
    void triggerMainWindowAction(const char *actionName);

    QComboBox *m_transport = nullptr;
    QComboBox *m_language = nullptr;
    QComboBox *m_route = nullptr;
    QComboBox *m_subAutoUpdate = nullptr;
    QCheckBox *m_startMinimal = nullptr;
    QCheckBox *m_autoConnect = nullptr;
    QCheckBox *m_allowLan = nullptr;
    QCheckBox *m_autoRun = nullptr;
    QComboBox *m_theme = nullptr;
};
