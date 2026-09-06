#pragma once

#include <QString>

#include <functional>

// Настоящее уведомление macOS.
//
// QSystemTrayIcon::showMessage тут бесполезен: Qt отправляет его через
// NSUserNotificationCenter, который Apple выбросила, и баннер не появляется
// никогда. Работающий путь один — UserNotifications.framework, и ходить в него
// приходится из Objective-C.
namespace MacNotify {
    // Спросить разрешение заранее, если его ещё не спрашивали. Иначе первое
    // же событие уходит в никуда: пока человек читает системный запрос, показывать
    // уже нечего.
    void Prime();

    // Показать уведомление. Ответ асинхронный: система может спросить
    // разрешение у человека, а он — не ответить сразу.
    //
    // onActivated зовём, когда по уведомлению щёлкнули; onRefused — когда
    // система отказалась его показывать (запрет в настройках, нет фреймворка,
    // бандл без подписи). На отказ у окна есть своя карточка.
    void Post(const QString &title, const QString &body,
              const std::function<void()> &onActivated,
              const std::function<void()> &onRefused);
} // namespace MacNotify
