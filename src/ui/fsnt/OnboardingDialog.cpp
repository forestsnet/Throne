#include "include/ui/fsnt/OnboardingDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QSvgRenderer>
#include <QTimer>
#include <QVBoxLayout>

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/database/GroupsRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/global/Configs.hpp"
#include "include/global/Utils.hpp"
#include "include/sys/AutoRun.hpp"
#include "include/ui/fsnt/FsntPalette.hpp"
#include "include/ui/fsnt/FsntControls.h"
#include "include/ui/fsnt/FsntTheme.hpp"

namespace {
    constexpr int kOnbPageCount = 4;
    constexpr int kOnbGuardMs = 45000;

    QLabel *heading(QWidget *parent, const QString &text) {
        auto *label = new QLabel(text, parent);
        label->setObjectName("fsntDialogTitle");
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        return label;
    }

    QLabel *caption(QWidget *parent, const QString &text) {
        auto *label = new QLabel(text, parent);
        label->setObjectName("fsntDialogHint");
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        return label;
    }

    // Крупная кнопка-карточка выбора.
    //
    // Текст кладём двумя подписями внутрь кнопки, а не в её setText: QPushButton
    // не переносит строки, и длинное пояснение обрезалось справа. Подписи
    // прозрачны для мыши, поэтому клик по ним доходит до самой кнопки.
    QPushButton *choiceCard(QWidget *parent, const QString &title, const QString &detail) {
        auto *button = new QPushButton(parent);
        button->setObjectName("fsntChoice");
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(84);

        auto *layout = new QVBoxLayout(button);
        layout->setContentsMargins(16, 12, 16, 12);
        layout->setSpacing(4);

        auto *head = new QLabel(title, button);
        head->setObjectName("fsntChoiceTitle");
        auto *sub = new QLabel(detail, button);
        sub->setObjectName("fsntChoiceDetail");
        sub->setWordWrap(true);

        for (QLabel *label : {head, sub}) {
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
            layout->addWidget(label);
        }
        return button;
    }
}

bool OnboardingDialog::ShouldRun() {
    if (Configs::dataManager->settingsRepo->onboarding_done) return false;

    // Считаем по серверам, а не по числу групп: у чистой установки есть пустая
    // группа Default, а у обновившегося пользователя может быть ровно одна
    // подписка — по количеству групп эти два случая не различить.
    for (const int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (group && !group->Profiles().isEmpty()) return false;
    }
    return true;
}

void OnboardingDialog::MarkDone() {
    Configs::dataManager->settingsRepo->onboarding_done = true;
    Configs::dataManager->settingsRepo->Save();
}

QPixmap OnboardingDialog::renderLogo(const int size) const {
    return Fsnt::BrandMark(size, devicePixelRatioF());
}

OnboardingDialog::OnboardingDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("fsntDialog");
    setWindowTitle("FSNT Client");
    setModal(true);
    setFixedSize(520, 560);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildWelcome());
    m_pages->addWidget(buildSubscription());
    m_pages->addWidget(buildTransport());
    m_pages->addWidget(buildFinish());
    layout->addWidget(m_pages, 1);

    // Точки-индикатор: без них непонятно, сколько шагов ещё впереди.
    m_dots = new QWidget(this);
    auto *dotRow = new QHBoxLayout(m_dots);
    dotRow->setContentsMargins(0, 0, 0, 20);
    dotRow->setSpacing(7);
    dotRow->addStretch();
    for (int i = 0; i < kOnbPageCount; ++i) {
        auto *dot = new QLabel(m_dots);
        dot->setObjectName("fsntDot");
        dot->setFixedSize(7, 7);
        dotRow->addWidget(dot);
    }
    dotRow->addStretch();
    layout->addWidget(m_dots);

    m_subGuard = new QTimer(this);
    m_subGuard->setSingleShot(true);
    m_subGuard->setInterval(kOnbGuardMs);
    connect(m_subGuard, &QTimer::timeout, this, [this] {
        m_subBusy = false;
        m_subAdd->setEnabled(true);
        m_subAdd->setText(tr("Continue"));
        m_subStatus->setText(tr("The provider did not answer. Check the link and your connection."));
    });

    setStyleSheet(Fsnt::BuildStyleSheet() + QString(R"(
        QPushButton#fsntChoice {
            background: %1;
            border: 1px solid %2;
            border-radius: %3px;
            color: %4;
            font-size: 13px;
            text-align: left;
            padding: 12px 16px;
        }
        QPushButton#fsntChoice:hover { border-color: %5; }
        QPushButton#fsntChoice:checked { border-color: %5; background: %6; }
        QLabel#fsntChoiceTitle { color: %4; font-size: 14px; font-weight: 600; }
        QLabel#fsntChoiceDetail { color: %7; font-size: 12px; }
        QLabel#fsntDot { border-radius: 3px; background: %2; }
        QLabel#fsntDot[active="true"] { background: %5; }
    )")
        .arg(Fsnt::CurrentPalette().card.name(QColor::HexRgb))
        .arg(Fsnt::CurrentPalette().border.name(QColor::HexRgb))
        .arg(Fsnt::kCardRadius)
        .arg(Fsnt::CurrentPalette().text.name(QColor::HexRgb))
        .arg(Fsnt::CurrentPalette().accent.name(QColor::HexRgb))
        .arg(QString("rgba(%1,%2,%3,%4)")
                 .arg(Fsnt::CurrentPalette().accentSoft.red())
                 .arg(Fsnt::CurrentPalette().accentSoft.green())
                 .arg(Fsnt::CurrentPalette().accentSoft.blue())
                 .arg(Fsnt::CurrentPalette().accentSoft.alpha()))
        .arg(Fsnt::CurrentPalette().textMuted.name(QColor::HexRgb)));

    refreshDots();
}

QWidget *OnboardingDialog::buildWelcome() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 44, 40, 20);
    layout->setSpacing(14);
    layout->addStretch();

    auto *logo = new QLabel(page);
    logo->setPixmap(renderLogo(88));
    logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);

    layout->addSpacing(6);
    layout->addWidget(heading(page, "FSNT Client"));
    layout->addWidget(caption(page, tr("Private and fast access to the internet.\n"
                                       "Setup takes about a minute.")));
    layout->addStretch();

    auto *start = new QPushButton(tr("Get started"), page);
    start->setObjectName("fsntPrimary");
    start->setCursor(Qt::PointingHandCursor);
    start->setMinimumHeight(40);
    connect(start, &QPushButton::clicked, this, [this] { goTo(1); });
    layout->addWidget(start);

    return page;
}

QWidget *OnboardingDialog::buildSubscription() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 44, 40, 20);
    layout->setSpacing(12);
    layout->addStretch();

    layout->addWidget(heading(page, tr("Add your subscription")));
    layout->addWidget(caption(page, tr("Paste the link your provider gave you. "
                                       "The server list will fill in automatically.")));
    layout->addSpacing(8);

    m_subInput = new QLineEdit(page);
    m_subInput->setObjectName("fsntInput");
    m_subInput->setPlaceholderText("https://");
    m_subInput->setClearButtonEnabled(true);
    m_subInput->setMinimumHeight(40);
    connect(m_subInput, &QLineEdit::returnPressed, this, &OnboardingDialog::submitSubscription);
    layout->addWidget(m_subInput);

    auto *paste = new QPushButton(tr("Paste from clipboard"), page);
    paste->setObjectName("fsntGhost");
    paste->setCursor(Qt::PointingHandCursor);
    connect(paste, &QPushButton::clicked, this, [this] {
        const QString text = QApplication::clipboard()->text().trimmed();
        if (text.isEmpty()) {
            m_subStatus->setText(tr("The clipboard is empty."));
            return;
        }
        m_subInput->setText(text);
        m_subStatus->clear();
    });
    auto *pasteRow = new QHBoxLayout;
    pasteRow->addStretch();
    pasteRow->addWidget(paste);
    pasteRow->addStretch();
    layout->addLayout(pasteRow);

    m_subStatus = caption(page, QString());
    layout->addWidget(m_subStatus);
    layout->addStretch();

    m_subAdd = new QPushButton(tr("Continue"), page);
    m_subAdd->setObjectName("fsntPrimary");
    m_subAdd->setCursor(Qt::PointingHandCursor);
    m_subAdd->setMinimumHeight(40);
    connect(m_subAdd, &QPushButton::clicked, this, &OnboardingDialog::submitSubscription);
    layout->addWidget(m_subAdd);

    m_subSkip = new QPushButton(tr("I'll do it later"), page);
    m_subSkip->setObjectName("fsntGhost");
    m_subSkip->setCursor(Qt::PointingHandCursor);
    connect(m_subSkip, &QPushButton::clicked, this, [this] { goTo(2); });
    layout->addWidget(m_subSkip);

    return page;
}

QWidget *OnboardingDialog::buildTransport() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 44, 40, 20);
    layout->setSpacing(12);
    layout->addStretch();

    layout->addWidget(heading(page, tr("How should traffic be routed?")));
    layout->addWidget(caption(page, tr("You can change this later in settings.")));
    layout->addSpacing(8);

    m_tun = choiceCard(page, tr("Everything through the tunnel"),
                       tr("All apps. Needs administrator rights once."));
    m_proxy = choiceCard(page, tr("Browsers only"),
                         tr("System proxy. No extra rights needed."));
    layout->addWidget(m_tun);
    layout->addWidget(m_proxy);

    connect(m_tun, &QPushButton::clicked, this, [this] { selectTransport(0); });
    connect(m_proxy, &QPushButton::clicked, this, [this] { selectTransport(1); });
    selectTransport(qBound(0, Configs::dataManager->settingsRepo->simple_transport, 1));

    layout->addStretch();

    auto *next = new QPushButton(tr("Continue"), page);
    next->setObjectName("fsntPrimary");
    next->setCursor(Qt::PointingHandCursor);
    next->setMinimumHeight(40);
    connect(next, &QPushButton::clicked, this, [this] { goTo(3); });
    layout->addWidget(next);

    return page;
}

QWidget *OnboardingDialog::buildFinish() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 44, 40, 20);
    layout->setSpacing(14);
    layout->addStretch();

    auto *logo = new QLabel(page);
    logo->setPixmap(renderLogo(72));
    logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);

    layout->addSpacing(6);
    layout->addWidget(heading(page, tr("All set")));
    layout->addWidget(caption(page, tr("Pick a server on the left and press the power button.")));
    layout->addSpacing(10);

    m_autoRun = new FsntSwitch(page);
    m_autoRun->setChecked(AutoRun_IsEnabled());
    auto *autoRunLabel = new QLabel(tr("Launch FSNT Client at login"), page);
    autoRunLabel->setObjectName("fsntRowLabel");
    auto *checkRow = new QHBoxLayout;
    checkRow->setSpacing(10);
    checkRow->addStretch();
    checkRow->addWidget(autoRunLabel);
    checkRow->addWidget(m_autoRun);
    checkRow->addStretch();
    layout->addLayout(checkRow);

    layout->addStretch();

    auto *done = new QPushButton(tr("Start using"), page);
    done->setObjectName("fsntPrimary");
    done->setCursor(Qt::PointingHandCursor);
    done->setMinimumHeight(40);
    connect(done, &QPushButton::clicked, this, &OnboardingDialog::finish);
    layout->addWidget(done);

    return page;
}

void OnboardingDialog::selectTransport(const int transport) {
    m_tun->setChecked(transport == 0);
    m_proxy->setChecked(transport == 1);
    Configs::dataManager->settingsRepo->simple_transport = transport;
}

void OnboardingDialog::goTo(const int page) {
    m_pages->setCurrentIndex(qBound(0, page, kOnbPageCount - 1));
    refreshDots();
}

void OnboardingDialog::refreshDots() {
    const auto dots = m_dots->findChildren<QLabel *>("fsntDot");
    for (int i = 0; i < dots.size(); ++i) {
        dots[i]->setProperty("active", i == m_pages->currentIndex());
        dots[i]->style()->unpolish(dots[i]);
        dots[i]->style()->polish(dots[i]);
    }
}

void OnboardingDialog::submitSubscription() {
    if (m_subBusy) return;

    const QString text = m_subInput->text().trimmed();
    if (text.isEmpty()) {
        m_subStatus->setText(tr("Enter a link, or skip this step."));
        return;
    }

    if (text.startsWith("throne://")) {
        if (MW_handle_deeplink) MW_handle_deeplink(text);
        goTo(2);
        return;
    }

    m_subBusy = true;
    m_subAdd->setEnabled(false);
    m_subAdd->setText(tr("Adding…"));
    m_subStatus->setText(tr("Fetching the subscription…"));
    m_subGuard->start();

    const auto done = [this] {
        // Колбэк приходит из рабочего потока обновлятора — возвращаемся в UI.
        QMetaObject::invokeMethod(this, [this] {
            m_subGuard->stop();
            m_subBusy = false;
            m_subAdd->setEnabled(true);
            m_subAdd->setText(tr("Continue"));
            goTo(2);
        }, Qt::QueuedConnection);
    };

    if (text.startsWith("http://") || text.startsWith("https://")) {
        Subscription::updater()->SubscribeUrl(text, done);
    } else {
        Subscription::updater()->ImportText(text, -1, done);
    }
}

void OnboardingDialog::finish() {
    auto &settings = Configs::dataManager->settingsRepo;
    settings->onboarding_done = true;
    settings->Save();

    if (m_autoRun->isChecked() != AutoRun_IsEnabled()) {
        AutoRun_SetEnabled(m_autoRun->isChecked());
    }
    accept();
}
