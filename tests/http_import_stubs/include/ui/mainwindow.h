#pragma once
#include <QStringList>
class MainWindow {
public:
    QStringList CheckConflictingProcesses() { return {QStringLiteral("Unrelated VPN")}; }
};
inline MainWindow *GetMainWindow() { static MainWindow window; return &window; }
