#include "include/ui/fsnt/FsntSettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "include/database/SettingsRepo.h"
#include "include/global/Configs.hpp"

FsntSettingsDialog::FsntSettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    setMinimumWidth(380);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    form->setSpacing(10);

    m_transport = new QComboBox(this);
    m_transport->addItem(tr("Full tunnel (TUN)"), 0);
    m_transport->addItem(tr("System proxy"), 1);
    m_transport->setCurrentIndex(
        qBound(0, Configs::dataManager->settingsRepo->simple_transport, 1));
    form->addRow(tr("Connection mode"), m_transport);

    m_language = new QComboBox(this);
    // Индексы совпадают со switch в main.cpp: 0 системный, 1 English, 2 中文, 3 فارسی, 4 русский.
    m_language->addItem(tr("System"), 0);
    m_language->addItem("English", 1);
    m_language->addItem("简体中文", 2);
    m_language->addItem("فارسی", 3);
    m_language->addItem("Русский", 4);
    m_language->setCurrentIndex(
        qBound(0, Configs::dataManager->settingsRepo->language, 4));
    form->addRow(tr("Language"), m_language);

    m_startMinimal = new QCheckBox(tr("Start minimized to tray"), this);
    m_startMinimal->setChecked(Configs::dataManager->settingsRepo->start_minimal);
    form->addRow(QString(), m_startMinimal);

    layout->addLayout(form);

    auto *hint = new QLabel(
        tr("Language changes apply after restarting the application."), this);
    hint->setWordWrap(true);
    hint->setObjectName("fsntSubMeta");
    layout->addWidget(hint);

    auto *advanced = new QPushButton(tr("Open advanced mode"), this);
    connect(advanced, &QPushButton::clicked, this, [this] {
        save();
        emit advancedModeRequested();
        accept();
    });
    layout->addWidget(advanced);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, [this] { save(); accept(); });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);
}

void FsntSettingsDialog::save() {
    auto &settings = Configs::dataManager->settingsRepo;
    settings->simple_transport = m_transport->currentData().toInt();
    settings->language = m_language->currentData().toInt();
    settings->start_minimal = m_startMinimal->isChecked();
    settings->Save();
}
