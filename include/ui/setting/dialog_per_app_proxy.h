#pragma once

#include <QDialog>
#include <QIcon>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

class FsntSwitch;
class QComboBox;
class QLineEdit;
class QListWidget;

namespace Configs { class RouteProfile; }

// Выбор приложений для проксирования поверх существующих правил по процессам.
// Механизм маршрутизации уже умеет processName:, диалог лишь избавляет от ручного ввода.
class DialogPerAppProxy : public QDialog {
    Q_OBJECT

public:
    explicit DialogPerAppProxy(QWidget *parent = nullptr);

private:
    enum Mode { None = 0, Proxy = 1, Direct = 2 };

    // Одно приложение, а не один процесс.
    //
    // Electron и браузеры порождают выводок процессов из одного бандла:
    // у Claude это Claude, Claude Helper, Claude Helper (Renderer) и
    // chrome_crashpad_handler. По процессу на строку список превращался в
    // четыре одинаковых «Claude», а маршрутизировать надо их все разом.
    struct AppEntry {
        QString name;             // отображаемое имя: ярлык из «Пуска» или имя бандла
        QString iconPath;         // .app или исполняемый файл — с него берётся иконка
        QStringList processes;    // все имена процессов этого приложения
        bool userApp = false;     // приложение пользователя, а не системная служба
    };

    // Не только запущенное: установленное приложение попадает в список, даже
    // если сейчас закрыто. Иначе настроить маршрут для браузера можно было бы
    // только при запущенном браузере.
    static QList<AppEntry> discoverApplications();
    static QIcon iconFor(const AppEntry &entry);

    void buildList(const QMap<QString, int> &known);
    void applyFilter();
    void save();

    QList<AppEntry> m_entries;
    QList<QComboBox *> m_modes;   // по переключателю на строку, в порядке m_entries
    QListWidget *m_list = nullptr;
    QLineEdit *m_filter = nullptr;
    FsntSwitch *m_showSystem = nullptr;
    std::shared_ptr<Configs::RouteProfile> chain;
};
