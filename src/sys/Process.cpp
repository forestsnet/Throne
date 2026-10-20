#include "include/sys/Process.hpp"
#include "include/global/Configs.hpp"
#include "include/global/Logger.hpp"
#include "include/sys/KillSwitch.hpp"
#include "include/sys/ServiceManager.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {
    // Ядро, поднятое с правами root (полный туннель на macOS и Linux), создаёт
    // cache.db во владении root. Следующий запуск уже без прав падает с
    // «initialize cache-file: open cache.db: permission denied» — человеку эта
    // строка не говорит ничего, а туннель просто не поднимается. Внутри файла
    // только кэш fakeip и DNS, терять там нечего: удаляем, ядро создаст заново.
    void coreDropUnusableCacheFile() {
        const QString path = QDir(Configs::GetBasePath()).filePath("cache.db");
        const QFileInfo info(path);
        if (!info.exists() || info.isWritable()) return;
        // Если у ядра стоит setuid, оно всё равно пойдёт под root и файл ему
        // доступен — тогда чужой владелец не мешает и кэш стоит сохранить.
        if (Configs::isSetuidSet(Configs::FindCoreRealPath().toStdString())) return;
        if (QFile::remove(path)) {
            LOG_WARN("cache.db was owned by another user (core ran as root before), removed");
        } else {
            LOG_WARN("cache.db is not writable and could not be removed: " + path);
        }
    }
}

namespace Configs_sys {
    namespace {
        std::atomic<bool> g_processIsolateChildren{false};

#ifdef Q_OS_WIN
        // CREATE_NEW_PROCESS_GROUP and CREATE_NO_WINDOW from <winbase.h>, which this portable TU does not include.
        constexpr unsigned long kProcessNewProcessGroup = 0x00000200;
        constexpr unsigned long kProcessNoWindow = 0x08000000;
#endif
    }

    void SetIsolatedChildren(bool enabled) { g_processIsolateChildren.store(enabled); }

    void IsolateChild(QProcess *process) {
#ifdef Q_OS_UNIX
        // Runs in the child between fork and exec: async-signal-safe calls only.
        if (g_processIsolateChildren.load()) process->setChildProcessModifier([] { ::setpgid(0, 0); });
#elif defined(Q_OS_WIN)
        // Qt adds CREATE_NO_WINDOW only while we have no console: after AttachConsole a Ctrl+C or a console close would
        // reach the core and the guard (which disarms) before our ordered teardown.
        if (g_processIsolateChildren.load()) {
            process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
                args->flags |= kProcessNewProcessGroup | kProcessNoWindow;
            });
        }
#else
        Q_UNUSED(process);
#endif
    }

    void KillChildGroup(QProcess *process) {
#ifdef Q_OS_UNIX
        const auto pid = static_cast<pid_t>(process->processId());
        // A group id is not reused while any member lives; ESRCH (setpgid not run yet, or all gone) is harmless.
        if (g_processIsolateChildren.load() && pid > 0) ::kill(-pid, SIGKILL);
#else
        Q_UNUSED(process);
#endif
    }

    CoreProcess::~CoreProcess() {
    }

    void CoreProcess::Kill(int waitMs) {
        if (state() == QProcess::NotRunning) return;
#ifndef Q_OS_WIN
        // Сначала просьба, и только потом SIGKILL. Перехватить SIGKILL нельзя,
        // поэтому ядро не получало возможности снять туннель, его маршруты,
        // подменённый системный DNS и дочерние процессы: всё это оставалось в
        // системе, а человек — без сети. Штатно откат делает RPC Stop выше по
        // стеку; сигнал нужен там, куда он не доехал.
        terminate();
        if (waitForFinished(qMin(waitMs, 8000))) return;
        qWarning() << "Core did not exit on request, killing it";
#else
        // В Windows terminate() шлёт WM_CLOSE, а у консольного ядра окон нет —
        // ждать его бессмысленно.
#endif
        KillChildGroup(this);
        kill();
        waitForFinished(waitMs);
    }

    CoreProcess::CoreProcess(const QString &core_path, const QString &socketName, bool debugMode)
        : m_socketName(socketName), m_debugMode(debugMode) {
        program = core_path;
    }

    void CoreProcess::Start() {
        if (started) return;
        started = true;
        ++generation;
        failed_to_start = false;

        auto env = Sys::ServiceManager::ChildEnvironment();
        env.insert("THRONE_CORE_SOCKET", m_socketName);
        // Turns an unrecovered Go panic into a real abort, so all goroutine stacks are dumped and WER captures the core too.
        env.insert("GOTRACEBACK", "crash");
        if (m_debugMode) env.insert("THRONE_CORE_DEBUG", "1");
        // Points Xray's asset loader at our writable config dir, so a geoip.dat/geosite.dat downloaded later is found with no core restart.
        env.insert("XRAY_LOCATION_ASSET", Configs::GetBasePath());
        // Never inherited: only guard_identity decides, or an identity exit would repeat with the flag already handled.
        env.remove("THRONE_GUARD");
        guard_identity = Configs::dataManager->settingsRepo->kill_switch && !Sys::KillSwitch::instance()->identityUnsupported();
        if (guard_identity) env.insert("THRONE_GUARD", "1");
        setProcessEnvironment(env);
        IsolateChild(this);
        start(program, {});
    }

    void CoreProcess::Restart() {
        restarting = true;
        KillChildGroup(this);
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
