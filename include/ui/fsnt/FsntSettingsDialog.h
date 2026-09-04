#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;

// Настройки простого режима: только то, что нужно конечному клиенту.
// Всё остальное живёт в расширенном режиме.
class FsntSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit FsntSettingsDialog(QWidget *parent = nullptr);

signals:
    void advancedModeRequested();

private:
    void save();

    QComboBox *m_transport = nullptr;
    QComboBox *m_language = nullptr;
    QCheckBox *m_startMinimal = nullptr;
};
