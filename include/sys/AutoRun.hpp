#pragma once

#include <qglobal.h>

// Возвращает, встало ли оно на самом деле. Раньше ответа не было, и когда
// система отказывала, человек видел лишь снятую обратно галочку без объяснений.
bool AutoRun_SetEnabled(bool enable);

bool AutoRun_IsEnabled();

void AutoRun_FixTaskIfNeeded();

void AutoRun_MigrateIfNeeded();
