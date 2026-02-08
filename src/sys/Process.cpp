#include "include/sys/Process.hpp"
#include "include/global/Configs.hpp"

#include <QTimer>
#include <QDir>
#include <QApplication>



#include "include/ui/mainwindow.h"

namespace Configs_sys {
    CoreProcess::~CoreProcess() {
    }

    void CoreProcess::Kill() {
        if (state() == QProcess::NotRunning) {
            qDebug() << "CoreProcess already not running";
            return;
        }
        
        qDebug() << "Sending kill signal to core process...";
        kill();
        
        // Ждём с таймаутом
        if (!waitForFinished(2000)) {
            qDebug() << "Core process did not stop gracefully, forcing terminate...";
            terminate();
            
            // Ещё одна попытка
            if (!waitForFinished(1000)) {
                qDebug() << "[Warning] Core process may still be running";
            } else {
                qDebug() << "Core process terminated successfully";
            }
        } else {
            qDebug() << "Core process stopped gracefully";
        }
    }

    CoreProcess::CoreProcess(const QString &core_path, const QStringList &args) {
        program = core_path;
        arguments = args;

        connect(this, &QProcess::readyReadStandardOutput, this, [&]() {
            auto log = readAllStandardOutput();
            if (!Configs::dataManager->settingsRepo->core_running) {
                if (log.contains("Core listening at")) {
                    // The core really started
                    Configs::dataManager->settingsRepo->core_running = true;
                    MW_dialog_message("ExternalProcess", "CoreStarted," + Int2String(start_profile_when_core_is_up));
                    start_profile_when_core_is_up = -1;
                } else if (log.contains("failed to serve")) {
                    // The core failed to start
                    kill();
                }
            }
            if (log.contains("Extra process exited unexpectedly"))
            {
                MW_show_log("Extra Core exited, stopping profile...");
                MW_dialog_message("ExternalProcess", "Crashed");
            }
            if (logCounter.fetchAndAddRelaxed(log.count("\n")) > Configs::dataManager->settingsRepo->max_log_line) return;
            MW_show_log(log);
        });
        connect(this, &QProcess::readyReadStandardError, this, [&]() {
            auto log = readAllStandardError().trimmed();
            MW_show_log(log);
        });
        connect(this, &QProcess::errorOccurred, this, [&](ProcessError error) {
            if (error == FailedToStart) {
                failed_to_start = true;
                MW_show_log("start core error occurred: " + errorString() + "\n");
            }
        });
        connect(this, &QProcess::stateChanged, this, [&](ProcessState state) {
            if (state == NotRunning) {
                Configs::dataManager->settingsRepo->core_running = false;
                qDebug() << "Core stated changed to not running";
            }

            if (!Configs::dataManager->settingsRepo->prepare_exit && state == NotRunning) {
                if (failed_to_start) return; // no retry
                if (restarting) return;

                MW_show_log("[Fatal] " + QObject::tr("Core exited, cleaning up..."));

                GetMainWindow()->profile_stop(true, true);

                // Retry rate limit
                if (coreRestartTimer.isValid()) {
                    if (coreRestartTimer.restart() < 10 * 1000) {
                        coreRestartTimer = QElapsedTimer();
                        MW_show_log("[ERROR] " + QObject::tr("Core exits too frequently, stop automatic restart this profile."));
                        return;
                    }
                } else {
                    coreRestartTimer.start();
                }

                // Restart
                start_profile_when_core_is_up = Configs::dataManager->settingsRepo->started_id;
                MW_show_log("[Warn] " + QObject::tr("Restarting the core ..."));
                setTimeout([=,this] { Restart(); }, this, 200);
            }
        });
    }

    void CoreProcess::Start() {
        if (started) return;
        started = true;

        setEnvironment(QProcessEnvironment::systemEnvironment().toStringList());
        start(program, arguments);
    }

    void CoreProcess::Restart() {
        restarting = true;
        qDebug() << "Restarting core process...";
        
        kill();
        if (!waitForFinished(2000)) {
            qDebug() << "Core did not stop during restart, forcing terminate...";
            terminate();
            waitForFinished(1000);
        }
        
        // Дополнительная задержка для освобождения портов и файлов
        QThread::msleep(500);
        
        started = false;
        Start();
        restarting = false;
        qDebug() << "Core restart sequence complete";
    }

} // namespace Configs_sys
