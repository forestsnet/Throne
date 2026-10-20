#include "include/ui/fsnt/SubscriptionSettings.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "include/database/GroupsRepo.h"
#include "include/global/Configs.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/fsnt/FsntControls.h"
#include "include/ui/fsnt/FsntTheme.hpp"

namespace Fsnt {
    void EditSubscription(QWidget *parent, int gid) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (group == nullptr) return;

        QDialog dialog(parent);
        dialog.setObjectName(QStringLiteral("fsntDialog"));
        dialog.setWindowTitle(software_name);
        dialog.setModal(true);
        dialog.setStyleSheet(BuildStyleSheet());
        dialog.setMinimumWidth(460);

        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(24, 22, 24, 20);
        layout->setSpacing(10);

        auto *heading = new QLabel(QObject::tr("Subscription settings"), &dialog);
        heading->setObjectName(QStringLiteral("fsntDialogTitle"));
        layout->addWidget(heading);

        auto *nameLabel = new QLabel(QObject::tr("Name"), &dialog);
        nameLabel->setObjectName(QStringLiteral("fsntDialogHint"));
        layout->addWidget(nameLabel);

        auto *name = new QLineEdit(group->name, &dialog);
        name->setObjectName(QStringLiteral("fsntInput"));
        layout->addWidget(name);

        auto *linkLabel = new QLabel(QObject::tr("Link"), &dialog);
        linkLabel->setObjectName(QStringLiteral("fsntDialogHint"));
        layout->addWidget(linkLabel);

        auto *linkRow = new QHBoxLayout;
        linkRow->setSpacing(6);
        auto *link = new QLineEdit(group->url, &dialog);
        link->setObjectName(QStringLiteral("fsntInput"));
        // Адрес показываем целиком и даём поправить: провайдеры иногда выдают
        // новую ссылку взамен исчерпанной, и заводить подписку заново незачем.
        linkRow->addWidget(link, 1);
        auto *copy = new QPushButton(QObject::tr("Copy"), &dialog);
        copy->setObjectName(QStringLiteral("fsntGhost"));
        copy->setCursor(Qt::PointingHandCursor);
        QObject::connect(copy, &QPushButton::clicked, &dialog, [link] {
            QApplication::clipboard()->setText(link->text());
        });
        linkRow->addWidget(copy);
        layout->addLayout(linkRow);

        // Что известно о подписке: когда обновлялась и сколько в ней серверов.
        const QString updated = group->sub_last_update > 0
                                    ? QDateTime::fromSecsSinceEpoch(group->sub_last_update)
                                          .toString("dd.MM.yyyy HH:mm")
                                    : QObject::tr("never");
        auto *stats = new QLabel(QObject::tr("%n server(s), updated %1", nullptr, group->Profiles().size())
                                     .arg(updated),
                                 &dialog);
        stats->setObjectName(QStringLiteral("fsntDialogHint"));
        layout->addWidget(stats);

        auto *autoRow = new QHBoxLayout;
        auto *autoLabel = new QLabel(QObject::tr("Update automatically"), &dialog);
        autoRow->addWidget(autoLabel);
        autoRow->addStretch();
        auto *autoUpdate = new FsntSwitch(&dialog);
        autoUpdate->setChecked(!group->skip_auto_update);
        autoRow->addWidget(autoUpdate);
        layout->addLayout(autoRow);

        layout->addSpacing(6);

        auto *buttons = new QHBoxLayout;
        buttons->addStretch();
        auto *cancel = new QPushButton(QObject::tr("Cancel"), &dialog);
        cancel->setObjectName(QStringLiteral("fsntGhost"));
        cancel->setCursor(Qt::PointingHandCursor);
        QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
        buttons->addWidget(cancel);

        auto *save = new QPushButton(QObject::tr("Save"), &dialog);
        save->setObjectName(QStringLiteral("fsntPrimary"));
        save->setCursor(Qt::PointingHandCursor);
        save->setDefault(true);
        QObject::connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
        buttons->addWidget(save);
        layout->addLayout(buttons);

        if (dialog.exec() != QDialog::Accepted) return;

        const QString newName = name->text().trimmed();
        const QString newUrl = link->text().trimmed();
        group->name = newName.isEmpty() ? group->name : newName;
        group->url = newUrl;
        group->skip_auto_update = !autoUpdate->isChecked();
        Configs::dataManager->groupsRepo->Save(group);
        MW_dialog_message(MwMessage::GroupsChanged, {});
    }
} // namespace Fsnt
