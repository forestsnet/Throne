#pragma once

#include <QDialog>
#include <QMap>
#include <QString>

class QTableWidget;

namespace Configs { class RouteProfile; }

// Выбор приложений для проксирования поверх существующих правил по процессам.
// Механизм маршрутизации уже умеет processName:, диалог лишь избавляет от ручного ввода.
class DialogPerAppProxy : public QDialog {
    Q_OBJECT

public:
    explicit DialogPerAppProxy(QWidget *parent = nullptr);

private:
    enum Mode { None = 0, Proxy = 1, Direct = 2 };

    static QStringList runningProcesses();
    void buildTable(const QMap<QString, int> &known);
    void save();

    QTableWidget *table = nullptr;
    std::shared_ptr<Configs::RouteProfile> chain;
};
