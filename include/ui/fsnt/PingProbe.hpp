#pragma once

#include <QString>

#include <functional>

// Проверка задержки одного сервера. Виды отвечают на разные вопросы, и путать
// их нельзя: первые три меряют дорогу до самого сервера, минуя туннель, а
// запрос идёт через туннель и меряет весь путь целиком — именно его человек и
// чувствует, открывая сайт.
namespace Fsnt {
    enum class PingKind {
        Icmp = 0,
        Tcp = 1,
        Handshake = 2,
        RequestGet = 3,
        RequestHead = 4,
    };

    PingKind PingKindFromSetting(int value);
    int PingKindToSetting(PingKind kind);

    // Название для меню и настроек.
    QString PingKindTitle(PingKind kind);
    // Одна строка о том, что именно меряется.
    QString PingKindHint(PingKind kind);

    // Проба одного профиля. Колбэк приходит в UI-поток; ms < 0 означает неудачу,
    // и тогда в error лежит короткая причина. Результат уже записан в профиль.
    void ProbeProfile(int profileId, PingKind kind,
                      const std::function<void(int ms, const QString &error)> &done = {});
} // namespace Fsnt
