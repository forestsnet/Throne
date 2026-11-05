#pragma once

#include <QString>

namespace Engine {
    // A panic or fatal line as the core's stderr starts it.
    bool CoreExitLooksLikePanic(const QString &line);

    // Why the core stopped, as one clause to follow "The connection dropped: ".
    // Empty when the raw cause names nothing a person can act on, and the window
    // then says only that the connection dropped.
    //
    // The raw cause itself is never shown: QProcess words it in English whatever
    // the interface language is, and the core's own output is English by nature.
    // The log keeps it either way, so nothing is lost by wording the category
    // here instead.
    QString CoreExitReasonText(bool failedToStart, const QString &rawCause);
} // namespace Engine
