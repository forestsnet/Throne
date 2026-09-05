#pragma once

#include <QDialog>
#include <QList>

#include "include/ui/fsnt/Diagnostics.hpp"

class QLabel;
class QPushButton;
class QVBoxLayout;

// Окно самопроверки: прогоняет проверки по очереди и показывает, что именно
// сломано и чем это чинится.
//
// Проверки идут в рабочем потоке — часть ходит в сеть и запускает внешние
// программы, и на этом нельзя держать интерфейс. Строки появляются сразу все,
// со статусом «идёт», и заполняются по мере готовности: так видно, что окно
// работает, а не зависло.
class FsntDiagnosticsDialog : public QDialog {
    Q_OBJECT

public:
    explicit FsntDiagnosticsDialog(QWidget *parent = nullptr);

private:
    struct Row {
        QLabel *status = nullptr;
        QLabel *detail = nullptr;
        QPushButton *fix = nullptr;
    };

    void runNext();
    void applyResult(int index, const Fsnt::CheckResult &result);
    void finish();

    QList<Fsnt::Check> m_checks;
    QList<Row> m_rows;
    int m_current = 0;
    QLabel *m_summary = nullptr;
    QPushButton *m_again = nullptr;
};
