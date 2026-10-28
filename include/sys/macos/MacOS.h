#pragma once

#include <QString>

// prompt — строка, которую macOS покажет в окне запроса пароля: своё название
// приложения она туда не подставит.
// osascript отдаёт этот код, когда человек закрыл окно пароля. Это не сбой, и
// жаловаться в журнал на него не нужно.
constexpr int kMacAuthCancelled = 1;

int Mac_Run_Command(QString command, const QString &prompt = {});