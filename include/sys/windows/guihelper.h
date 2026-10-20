#pragma once

class QWidget;

void Windows_QWidget_SetForegroundWindow(QWidget* w);

bool Windows_IsInAdmin();

// Красит системную рамку окна под тему приложения. Заголовок рисует сама
// Windows, о нашей тёмной теме она не знает, и светлая полоса над тёмным окном
// выглядит чужеродно.
void Windows_SetDarkTitleBar(QWidget *w, bool dark);
