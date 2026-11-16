#include "include/sys/TunDiagnostics.h"

#include <QObject>

namespace Sys::WinTunDiag {
    QString ProblemText(const quint32 problem) {
        switch (problem) {
            case 0: return QStringLiteral("none");
            case 1: return QStringLiteral("not configured");
            case kProblemNeedRestart: return QStringLiteral("needs a restart to finish installing");
            case 18: return QStringLiteral("needs reinstalling");
            case kProblemDisabled: return QStringLiteral("disabled");
            case 24: return QStringLiteral("not present");
            case kProblemFailedInstall: return QStringLiteral("driver not installed");
            case 29: return QStringLiteral("disabled by firmware");
            case kProblemFailedAdd: return QStringLiteral("driver could not be attached");
            case 32: return QStringLiteral("driver service is disabled");
            case kProblemFailedDriverEntry: return QStringLiteral("DriverEntry failed");
            case kProblemFailedPriorUnload: return QStringLiteral("a previous instance is still unloading");
            case kProblemFailedLoad: return QStringLiteral("the driver could not be loaded");
            case 43: return QStringLiteral("stopped by the driver");
            case 45: return QStringLiteral("phantom, not connected");
            case kProblemUnsignedDriver: return QStringLiteral("the driver signature was rejected");
            case kProblemNeedClassConfig: return QStringLiteral("the network class configuration never finishes");
            default: return QStringLiteral("see device manager code %1").arg(problem);
        }
    }

    QString HeadlineFor(const Verdict verdict, const QString &first, const QString &second) {
        switch (verdict) {
            case Verdict::Ok:
                return {};
            case Verdict::DeviceLeftover:
                // Wintun такое переживает сам: открывает оставшийся адаптер
                // заново, и обычно получается. Говорим о находке, но запуск не
                // отменяем — иначе отняли бы рабочий случай.
                return QObject::tr("The tunnel adapter is left over from an earlier session, and Windows reports problem code %1 on it. "
                                   "The tunnel will try to take it over.")
                    .arg(first);
            case Verdict::DeviceUnloading:
                return QObject::tr("Windows is still unloading the tunnel adapter of an earlier attempt (device problem code %1). "
                                   "Starting again only queues another device operation behind it, and those take minutes. Wait, or "
                                   "restart Windows.")
                    .arg(first);
            case Verdict::DeviceDisabled:
                return QObject::tr("The tunnel adapter is disabled in the Windows device manager, so the tunnel cannot use it. Enable it "
                                   "there, or restart Windows.");
            case Verdict::NeedsRestart:
                return QObject::tr("Windows put off installing the tunnel driver until the next restart, so the tunnel cannot start "
                                   "before Windows has been restarted.");
            case Verdict::ClassConfigBroken:
                return QObject::tr("Windows on this computer cannot finish setting up new network devices (device problem code 56), so "
                                   "the tunnel adapter cannot be created. It happens to every new network device, not only to this "
                                   "application, and reinstalling the application changes nothing. Repairing Windows over itself, with "
                                   "the files and applications kept, is what fixes it.");
            case Verdict::DriverMissing:
                return QObject::tr("The tunnel driver is registered at %1, and there is no file there. Windows words this as \"cannot "
                                   "find the file specified\". Antivirus software deleting the file is the usual reason.")
                    .arg(first);
            case Verdict::DriverDamaged:
                return QObject::tr("The tunnel driver file %1 did not pass the signature check (%2), so Windows will refuse to load it.")
                    .arg(first, second);
        }
        return {};
    }

    bool BlocksStart(const Verdict verdict) {
        switch (verdict) {
            case Verdict::DeviceUnloading:
            case Verdict::DeviceDisabled:
            case Verdict::NeedsRestart:
            case Verdict::ClassConfigBroken:
                return true;
            // Оставшийся адаптер Wintun забирает себе сам, а пропавший или
            // неподписанный файл драйвера он переустанавливает из своего
            // ресурса. Отменять запуск в этих случаях значило бы отнять
            // рабочий случай, поэтому только говорим о находке.
            case Verdict::Ok:
            case Verdict::DeviceLeftover:
            case Verdict::DriverMissing:
            case Verdict::DriverDamaged:
                return false;
        }
        return false;
    }
} // namespace Sys::WinTunDiag
