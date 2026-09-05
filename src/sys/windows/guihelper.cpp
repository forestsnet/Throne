
#include "include/sys/windows/guihelper.h"

#include <QWidget>

#include <windows.h>
#include <shlobj.h>
#include <dwmapi.h>

// Константа появилась только в SDK 10.0.22000, а собираемся мы и более старым.
// Значение стабильно; в сборках Windows 10 до 20H1 тот же смысл имел атрибут 19.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

void Windows_QWidget_SetForegroundWindow(QWidget *w) {
    HWND hForgroundWnd = GetForegroundWindow();
    DWORD dwForeID = ::GetWindowThreadProcessId(hForgroundWnd, NULL);
    DWORD dwCurID = ::GetCurrentThreadId();
    const bool attach = dwForeID != 0 && dwForeID != dwCurID &&
        AttachThreadInput(dwCurID, dwForeID, TRUE);
    SetForegroundWindow((HWND) w->winId());
    if (attach) AttachThreadInput(dwCurID, dwForeID, FALSE);
}

void Windows_SetDarkTitleBar(QWidget *w, const bool dark) {
    if (w == nullptr) return;
    const auto handle = reinterpret_cast<HWND>(w->winId());
    const BOOL value = dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(handle, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value)))) {
        constexpr DWORD kDarkModeBefore20H1 = 19;
        DwmSetWindowAttribute(handle, kDarkModeBefore20H1, &value, sizeof(value));
    }
}

int isThisAdmin = -1;

bool Windows_IsInAdmin() {
    if (isThisAdmin >= 0) return isThisAdmin;
    isThisAdmin = IsUserAnAdmin();
    return isThisAdmin;
}
