#pragma once

#include <QDialog>
#include <QStringList>

class FsntSwitch;
class QLineEdit;
class QPlainTextEdit;

// Живой просмотр журнала с фильтром.
//
// Раньше единственным способом понять, что происходит, был расширенный режим.
// Окно немодальное: за логом смотрят как раз тогда, когда что-то нажимают.
class FsntLogDialog : public QDialog {
    Q_OBJECT

public:
    explicit FsntLogDialog(QWidget *parent = nullptr);

public slots:
    // Новая строка от ядра. Окно подключается к сигналу FsntWindow, а не
    // перехватывает MW_show_log само: перехват пришлось бы снимать при закрытии,
    // и любая ошибка в этом оставила бы висящий указатель на удалённый диалог.
    void appendLine(const QString &line);

private:
    bool passes(const QString &line) const;
    void rebuild();

    QStringList m_lines;
    QLineEdit *m_filter = nullptr;
    FsntSwitch *m_problemsOnly = nullptr;
    FsntSwitch *m_autoScroll = nullptr;
    QPlainTextEdit *m_view = nullptr;
};
