#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QProgressBar;
class QPushButton;

// Окно «идёт работа» для действий, которые длятся дольше пары секунд.
//
// Сбор отчёта для поддержки опрашивает систему и упаковывает архив: на Windows
// это полминуты, и всё это время человек смотрел на закрывшееся меню. Нажал —
// и ничего, ни строки, ни полосы. Жал ещё раз, звонил в поддержку, закрывал
// клиент на середине.
//
// Шаги известны заранее, поэтому полоса не «бегунок», а настоящий прогресс:
// видно и что работа идёт, и сколько её осталось. Закончив, то же окно
// показывает итог — отдельное окно с путём к файлу человеку уже не нужно.
class FsntBusyDialog : public QDialog {
    Q_OBJECT

public:
    FsntBusyDialog(const QString &title, const QString &hint, int steps, QWidget *parent = nullptr);

    // Следующий шаг: подпись меняется, полоса подвигается. Вызывать из UI-потока.
    void setStage(const QString &text);

    // Работа закончена. openPath не пуст — рядом с «Закрыть» встаёт кнопка,
    // открывающая этот файл или папку в проводнике.
    void succeed(const QString &text, const QString &openPath = {});
    void fail(const QString &text);

protected:
    // Esc и крестик во время работы ничего не отменяют, а итог человеку нужен.

private:
    void settle(const QString &text, bool ok);
    // Высота переноса известна только при известной ширине, а окно, размер
    // которого посчитан по пустой подписи, само под текст не растёт: хвост
    // уезжает под кнопки. Поэтому после каждой смены текста считаем сами.
    void fitHeight();

    QLabel *m_stage = nullptr;
    QProgressBar *m_bar = nullptr;
    QPushButton *m_close = nullptr;
    QPushButton *m_reveal = nullptr;
    int m_steps = 1;
    int m_done = 0;
    bool m_settled = false;
};
