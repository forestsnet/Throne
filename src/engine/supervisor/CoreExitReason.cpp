#include "include/engine/supervisor/CoreExitReason.h"

#include <QObject>

namespace Engine {
    bool CoreExitLooksLikePanic(const QString &line) {
        return line.startsWith(QLatin1String("Core panicked:")) || line.startsWith(QLatin1String("panic:")) ||
               line.startsWith(QLatin1String("fatal error:"));
    }

    QString CoreExitReasonText(const bool failedToStart, const QString &rawCause) {
        // The file is gone, or it is there and the system will not run it. Either
        // way the tunnel is not coming back on its own, and that is the part worth
        // saying: without it the window reports a dropped connection and leaves
        // the impression that waiting will fix it.
        if (failedToStart) return QObject::tr("the core could not be started");
        if (CoreExitLooksLikePanic(rawCause)) return QObject::tr("the core stopped with an error");
        // A core killed from outside, a clean exit, a provider that cut the
        // session - the supervisor cannot tell these apart, and guessing in the
        // window would be worse than the bare fact.
        return {};
    }
} // namespace Engine
