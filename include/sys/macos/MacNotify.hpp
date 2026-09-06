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
    // onActivated зовём, когда по уведомлению щёлкнули; onRefused — только
    // когда показать нечем технически: нет фреймворка, бандл без подписи,
    // система не приняла запрос. Явный запрет в настройках сюда не попадает —
    // это ответ человека, и обходить его своей карточкой нельзя.
    void Post(const QString &title, const QString &body,
              const std::function<void()> &onActivated,
              const std::function<void()> &onRefused);
} // namespace MacNotify
