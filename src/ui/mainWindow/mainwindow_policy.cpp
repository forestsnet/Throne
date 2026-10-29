#include "include/ui/mainwindow.h"

#include <QMessageBox>
#include <QPushButton>

#include "include/engine/app/ExitController.h"
#include "include/engine/connection/ConnectionManager.h"
#include "include/sys/KillSwitch.hpp"
#include "include/ui/profile/dialog_vpn_auth.h"
#include "include/ui/utils/RestartPrompt.h"

#ifdef Q_OS_LINUX
#include "include/sys/linux/LinuxCap.h"
#endif
#ifdef Q_OS_MACOS
#include "include/sys/macos/CorePrivilege.h"
#include "include/sys/macos/MacOS.h"
#endif

void MainWindow::geoAssetMissingCategory(const QString &contextName, const QString &needed, const QString &whichFile) {
    MessageBoxWarning(
        tr("Geo asset missing category"),
        tr("The Xray config \"%1\" needs \"%2\", but the installed %3 does "
           "not contain it.\n\n"
           "Re-downloading from the same source will not fix this — the data "
           "file does not include that category. Set the GeoIP/GeoSite asset "
           "URL in Settings to a source that provides \"%2\", then delete %3 "
           "from the app folder and download it again.")
            .arg(contextName, needed, whichFile));
}

bool MainWindow::confirmGeoAssetDownload(const QString &contextName) {
    return QMessageBox::question(this, tr("Geo asset files required"),
            tr("The Xray config \"%1\" uses geoip/geosite routing rules, but the "
               "required data files (geoip.dat / geosite.dat) are not installed.\n\n"
               "Download them now?").arg(contextName)) == QMessageBox::Yes;
}

void MainWindow::geoAssetDownloadFinished(const QString &error) {
    if (!error.isEmpty()) {
        MessageBoxWarning(tr("Geo asset download failed"), error);
    } else {
        QMessageBox::information(this, tr("Geo assets installed"),
            tr("Geo data files were downloaded successfully.\n\n"
               "Please try again."));
    }
}

void MainWindow::killSwitchProblem() {
    auto *killSwitch = Sys::KillSwitch::instance();
    if (killSwitch->state() != Sys::KillSwitch::State::Failed) return;
    const bool privilege = killSwitch->failedForPrivileges();
    const bool identity = killSwitch->failedForIdentity();
    if (m_killSwitchDialog) {
        if (m_killSwitchDialog->property("privilege").toBool() == privilege && m_killSwitchDialog->property("identity").toBool() == identity) {
            m_killSwitchDialog->setText(killSwitch->failureText());
            return;
        }
        m_killSwitchDialog->close();
    }

    auto *box = new QMessageBox(QMessageBox::Warning, identity ? tr("Kill switch is blocking traffic") : tr("Kill switch is not active"),
                                killSwitch->failureText(), QMessageBox::NoButton, this);
    box->setTextFormat(Qt::PlainText);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setProperty("privilege", privilege);
    box->setProperty("identity", identity);
    // The guard still blocks and the core cannot join its group: neither a retry nor more privileges changes that.
    QPushButton *fix = nullptr;
    if (!identity) {
#ifdef Q_OS_WIN
        fix = box->addButton(privilege ? tr("Restart as Administrator") : tr("Retry"), QMessageBox::AcceptRole);
#else
        fix = box->addButton(privilege ? tr("Grant Privileges") : tr("Retry"), QMessageBox::AcceptRole);
#endif
    }
    auto *disable = box->addButton(tr("Disable Kill Switch"), QMessageBox::DestructiveRole);
    auto *cancel = box->addButton(QMessageBox::Cancel);
    box->setDefaultButton(fix != nullptr ? fix : cancel);
    box->setEscapeButton(cancel);
    m_killSwitchDialog = box;

    // Custom-button result codes changed in Qt 6.5, so only the clicked button pointer is trusted.
    connect(box, &QMessageBox::finished, this, [this, box, fix, disable, privilege] {
        const auto *clicked = box->clickedButton();
        if (m_killSwitchDialog == box) m_killSwitchDialog = nullptr;
        if (clicked == disable) {
            connection()->disableKillSwitch();
            return;
        }
        if (fix == nullptr || clicked != fix) return;
        if (!privilege) {
            Sys::KillSwitch::instance()->apply();
            return;
        }
#ifdef Q_OS_WIN
        exitController()->requestExit(ExitReason::RestartElevated, false);
#else
        // Otherwise the core restarts once it is privileged, and onCoreStarted retries.
        if (connection()->ensureTunPrivileges(true)) Sys::KillSwitch::instance()->apply();
#endif
    });
    box->open();
}

void MainWindow::killSwitchProblemResolved() {
    if (m_killSwitchDialog) m_killSwitchDialog->close();
}

void MainWindow::extraCoreNotPermitted() {
    ShowPassiveWarning(tr("Kill switch"),
                       tr("The kill switch could not be updated to allow this profile's extra core, so the profile was not started. See the log for details."));
}

void MainWindow::startFailed(const StartRequest &, StartOutcome, const QString &title, const QString &error) {
    MessageBoxWarning(title, error);
}

void MainWindow::strictRouteUnavailable(const QString &error) {
    MessageBoxWarning(
        tr("Strict routing unavailable"),
        tr("Windows could not enable strict routing. Open Tun Settings, "
           "disable Strict Route, and start the profile again.\n\n"
           "Disabling Strict Route may cause DNS leaks.\n\nError: %1").arg(error));
}

bool MainWindow::offerCoreReset(const QString &error) {
    QMessageBox msg(
        QMessageBox::Information,
        tr("Tun device misbehaving"),
        tr("If you have trouble starting VPN, you can force reset Core process here and then try starting the profile again. The error is %1").arg(error),
        QMessageBox::NoButton,
        this
    );
    auto reset = msg.addButton(tr("Reset"), QMessageBox::ActionRole);
    auto cancel = msg.addButton(tr("Cancel"), QMessageBox::ActionRole);

    msg.setDefaultButton(cancel);
    msg.setEscapeButton(cancel);

    msg.exec();
    return msg.clickedButton() == reset;
}

bool MainWindow::offerLeftoverTunAdapterRetry(const QString &error) {
    QMessageBox msg(
        QMessageBox::Information,
        tr("Leftover Tun adapter"),
        tr("Windows was still holding the Tun adapter from an earlier session that did not close cleanly. "
           "Starting the profile again usually works; if it keeps failing, restart Windows.\n\nError: %1").arg(error),
        QMessageBox::NoButton,
        this
    );
    auto startAgain = msg.addButton(tr("Start Again"), QMessageBox::AcceptRole);
    auto cancel = msg.addButton(tr("Cancel"), QMessageBox::RejectRole);

    msg.setDefaultButton(startAgain);
    msg.setEscapeButton(cancel);

    msg.exec();
    return msg.clickedButton() == startAgain;
}

void MainWindow::stopFailed(const QString &error) {
    MessageBoxWarning(tr("Stop return error"), error);
}

Engine::SlowOperation MainWindow::slowOperation(Engine::SlowOperationKind, int delayMs) {
    const QPointer<RestartPrompt> restartPrompt =
        new RestartPrompt(this, tr("If there is no response for a long time, it is recommended to restart the software."), delayMs);
    return Engine::SlowOperation([restartPrompt] { if (restartPrompt) restartPrompt->dismiss(); });
}

bool MainWindow::requestCorePrivileges() {
#ifdef NKR_ELEVATION_HINT
    MessageBoxWarning(software_name, tr("This installation cannot grant the core privileges by itself.") + "\n\n" + NKR_ELEVATION_HINT);
    return false;
#endif
#ifdef Q_OS_LINUX
    if (!Linux_HavePkexec()) {
        MessageBoxWarning(software_name, "Please install \"pkexec\" first.");
        return false;
    }
    auto n = QMessageBox::warning(GetMessageBoxParent(), software_name, tr("Please give the core root privileges"), QMessageBox::Yes | QMessageBox::No);
    if (n == QMessageBox::Yes) {
        runOnNewThread([=,this]
        {
            const auto corePath = Configs::FindCoreRealPath();
            const QStringList chownArgs{QStringLiteral("root:root"), corePath};
            auto ret = Linux_Run_Command("chown", chownArgs);
            if (ret != 0) {
                MW_show_log(QString("Failed to run chown %1 code is %2").arg(chownArgs.join(' ')).arg(ret));
            }
            const QStringList chmodArgs{QStringLiteral("u+s"), corePath};
            ret = Linux_Run_Command("chmod", chmodArgs);
            if (ret == 0) {
                StopVPNProcess();
            } else {
                MW_show_log(QString("Failed to run chmod %1").arg(chmodArgs.join(' ')));
            }
        });
        return false;
    }
#endif
#ifdef Q_OS_WIN
    auto n = QMessageBox::warning(GetMessageBoxParent(), software_name, tr("Please run Throne as admin"), QMessageBox::Yes | QMessageBox::No);
    if (n == QMessageBox::Yes) {
        this->exit_reason = ExitReason::RestartWithTun;
        on_menu_exit_triggered();
    }
#endif

#ifdef Q_OS_MACOS
    const auto corePath = Configs::FindCoreRealPath();
    if (Configs::isSetuidSet(corePath.toStdString()))
    {
        StopVPNProcess();
        return true;
    }

    const auto prompt = tr("FSNT Client needs your password to let the tunnel start.");

    // Служба следит за этим ядром, но setuid ещё не вернулся: обновление
    // только что подменило файл. Будим службу и ждём её, иначе возобновление
    // сразу после обновления падает на ровном месте, и человек получает запрос
    // пароля там, где обещано его не спрашивать.
    if (Sys::MacCorePrivilege::WatchesCore(corePath)) {
        if (Sys::MacCorePrivilege::Reapply(corePath, 10000)) {
            StopVPNProcess();
            return true;
        }
        MW_show_log(QStringLiteral("[Tun] The tunnel permission service watches the core, but the core did not become setuid"));
    }

    // Почему спрашиваем вообще и почему можно спросить в последний раз. Цена
    // постоянного разрешения названа здесь же: человек соглашается не на
    // «меньше окон», а на то, что запись в бандл даёт root.
    QMessageBox box(QMessageBox::Warning, software_name,
                    tr("The tunnel needs the core to run with system privileges. Every update installs a new "
                       "core, and the permission is lost together with the old one, so it has to be granted "
                       "again — including after an update that installs itself while you are away.\n\n"
                       "It can be granted for good instead: a system service puts the permission back whenever "
                       "the core changes, and you are not asked again. While that service is installed, anyone "
                       "who can write inside this application's folder gains system privileges without a "
                       "password."),
                    QMessageBox::NoButton, GetMessageBoxParent());
    auto *forGood = box.addButton(tr("Grant for good"), QMessageBox::AcceptRole);
    auto *once = box.addButton(tr("Only this time"), QMessageBox::ActionRole);
    auto *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(forGood);
    box.setEscapeButton(cancel);
    box.exec();
    const auto *clicked = box.clickedButton();
    if (clicked != forGood && clicked != once) return false;

    if (clicked == forGood) {
        int code = 0;
        if (Sys::MacCorePrivilege::Install(corePath, prompt, &code)) {
            StopVPNProcess();
            return true;
        }
        if (code == kMacAuthCancelled) return false;
        // Не тупик: разрешение на один раз всё ещё выдаётся ниже, и туннель
        // поднимется. О том, что постоянного не вышло, пишем в журнал.
        MW_show_log(QStringLiteral("Failed to install the tunnel permission service, code %1; granting this time only")
                        .arg(code));
    }

    // Без sudo: do shell script … with administrator privileges уже выполняется
    // от root, а sudo без терминала только мешает. Кавычки — одинарные, путь
    // может содержать пробелы.
    const auto quoted = QString(corePath).replace('\'', QStringLiteral("'\\''"));
    const auto command = QStringLiteral("chown root:wheel '%1' && chmod u+s '%1'").arg(quoted);
    const auto ret = Mac_Run_Command(command, prompt);
    if (ret != 0) {
        // Отмена в окне пароля — не ошибка, молчим о ней.
        if (ret != kMacAuthCancelled) {
            MW_show_log(QStringLiteral("Failed to grant the core privileges, code %1").arg(ret));
        }
        return false;
    }

    // Проверяем итог, а не код возврата: прав могло и не прибавиться.
    if (!Configs::isSetuidSet(corePath.toStdString())) {
        MessageBoxWarning(software_name, tr("The permission was not applied. Try again, or grant it by hand."));
        return false;
    }
    // Продолжаем сами: заставлять человека нажимать «Подключиться» заново
    // незачем, разрешение уже есть.
    StopVPNProcess();
    return true;
#endif
    return false;
}

void MainWindow::systemProxyNeedsMixedInbound() {
    MessageBoxWarning("Invalid Operation", "Cannot set system proxy when mixed inbound is disabled.");
}

bool MainWindow::vpnPromptOpen() const {
    return m_vpnAuthDialog != nullptr;
}

void MainWindow::vpnChallenge(const VpnAuthChallenge &challenge) {
    auto *dialog = new DialogVpnAuth(this, vpnAuth(), challenge);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_vpnAuthDialog = dialog;
    dialog->show();
    ActivateWindow(dialog);
}

void MainWindow::vpnCredentialsRejected(int, const VpnAuthChallenge &challenge,
                                        const std::function<void(const QString &username, const QString &password)> &retryWith) {
    auto *dialog = new DialogVpnAuth(this, vpnAuth(), challenge, true);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_vpnAuthDialog = dialog;
    connect(dialog, &QDialog::accepted, this, [dialog, retryWith] {
        retryWith(dialog->enteredUsername(), dialog->enteredPassword());
    });
    dialog->show();
    ActivateWindow(dialog);
}

void MainWindow::vpnChallengesCleared() {
    if (m_vpnAuthDialog != nullptr) m_vpnAuthDialog->close();
}

void MainWindow::settingsNeedTunRestart() {
    MessageBoxWarning(tr("Tun Settings changed"), tr("Restart Tun to take effect."));
}

bool MainWindow::confirmRestartProgram() {
    return QMessageBox::warning(GetMessageBoxParent(), tr("Settings changed"), tr("Restart the program to take effect."), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
}

void MainWindow::testSessionBusy(const QString &text) {
    MessageBoxWarning(software_name, text);
}

void MainWindow::exitProgram(ExitReason reason, bool unattended) {
    if (unattended) {
        request_unattended_exit(reason);
        return;
    }
    exit_reason = reason;
    on_menu_exit_triggered();
}
