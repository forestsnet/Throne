#pragma once

#include <QString>

// prompt — строка, которую macOS покажет в окне запроса пароля: своё название
// приложения она туда не подставит.
int Mac_Run_Command(QString command, const QString &prompt = {});