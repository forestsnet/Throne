#pragma once

#include <QDialog>
#include <QList>

#include <functional>

#include "include/ui/fsnt/Diagnostics.hpp"

class QLabel;
class QPushButton;
class QVBoxLayout;

// Окно самопроверки: прогоняет проверки по очереди и показывает, что именно
// сломано и чем это чинится.
//
// Проверки идут в рабочем потоке — часть ходит в сеть и запускает внешние
// программы, и на этом нельзя держать интерфейс. Строки появляются сразу все и
// заполняются по мере готовности: так видно, что окно работает, а не зависло.
// Но «идёт» достаётся только той строке, чья проверка действительно запущена:
// проверки выполняются по одной, на сломанном туннеле цепочка идёт больше
// двадцати секунд, и «идёт» у всех сразу было прямой неправдой.
class FsntDiagnosticsDialog : public QDialog {
    Q_OBJECT

public:
    explicit FsntDiagnosticsDialog(QWidget *parent = nullptr);

private:
    struct Row {
        QLabel *status = nullptr;
        QLabel *detail = nullptr;
        QPushButton *fix = nullptr;
        // Починка последнего результата этой строки. Кнопка подключается один
        // раз, при сборке окна, и берёт починку отсюда: подключать её заново
        // на каждом прогоне нельзя — Qt::UniqueConnection для лямбд не
        // действует, и после N прогонов одно нажатие запускало починку N раз.
        std::function<void(const Fsnt::CheckResult::FixDone &)> action;
    };

    void runAll();
    void runNext();
    void startFix(int index);
    void applyResult(int index, const Fsnt::CheckResult &result);
    void finish();

    QList<Fsnt::Check> m_checks;
    QList<Row> m_rows;
    int m_current = 0;
    // Номер прогона. Починку можно нажать, пока цепочка ещё идёт, и её
    // перезапуск проверок обгонял бы прежнюю цепочку: результаты двух прогонов
    // ложились бы в одни и те же строки. Запоздавший результат узнаётся по
    // номеру и выбрасывается.
    int m_run = 0;
    // Починка в работе. Пока она не доложила о конце, прогон проверок — её
    // дело: чужой переписал бы строку, которую она ещё меняет.
    bool m_fixing = false;
    QLabel *m_summary = nullptr;
    QPushButton *m_again = nullptr;
};
