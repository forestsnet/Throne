#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Опрос сетевых устройств и драйвера Wintun средствами самой Windows.
//
// Написано после разбора 2026-11-16. Туннель у клиента не поднимался, потому
// что машина не умела довести до конца настройку сетевого класса: любое новое
// сетевое устройство оставалось с кодом 56 в диспетчере устройств. Клиент же
// сообщал об этом так: пятнадцать секунд ожидания, потом «cannot find the file
// specified» и предложение сбросить ядро, которое тут ничем не поможет. Ночь
// ушла на то, что Windows знает с первой секунды и готова ответить одним
// вызовом.
//
// Здесь только API настройки устройств и диспетчера служб, без дочерних
// процессов и без PowerShell, поэтому опрос стоит миллисекунды и его можно
// делать перед каждым запуском.
namespace Sys::WinTunDiag {
    // Коды проблем устройства из cfgmgr32.h. Числами, а не макросами: имена в
    // разных SDK появлялись в разное время, а числа не менялись никогда. Здесь,
    // а не в опросе: таблицу названий читает переносимый файл, а решение по коду
    // принимает опрос, и расходиться им нельзя.
    inline constexpr quint32 kProblemNeedRestart = 14;
    inline constexpr quint32 kProblemDisabled = 22;
    inline constexpr quint32 kProblemFailedInstall = 28;
    inline constexpr quint32 kProblemFailedAdd = 31;
    inline constexpr quint32 kProblemFailedDriverEntry = 37;
    inline constexpr quint32 kProblemFailedPriorUnload = 38;
    inline constexpr quint32 kProblemFailedLoad = 39;
    inline constexpr quint32 kProblemUnsignedDriver = 52;
    inline constexpr quint32 kProblemNeedClassConfig = 56;

    enum class Verdict {
        // Ничего подозрительного.
        Ok,
        // Адаптер прошлой сессии на месте, и Wintun попробует забрать его себе.
        DeviceLeftover,
        // Прошлый адаптер ещё выгружается, и новый встанет в очередь за ним.
        DeviceUnloading,
        // Адаптер отключён в диспетчере устройств.
        DeviceDisabled,
        // Установка драйвера отложена до перезагрузки.
        NeedsRestart,
        // Windows не доводит настройку сетевого класса до конца.
        ClassConfigBroken,
        // Служба драйвера есть, файла по её пути нет.
        DriverMissing,
        // Файл есть, но подпись проверку не прошла.
        DriverDamaged,
    };

    struct NetDevice {
        QString instanceId;
        QString description;
        QString service;
        quint32 status = 0;
        quint32 problem = 0;
    };

    struct DriverFile {
        bool serviceInstalled = false;
        // SERVICE_STATUS::dwCurrentState, 0 — выяснить не удалось.
        quint32 serviceState = 0;
        QString binaryPath;
        bool fileExists = false;
        qint64 size = -1;
        QString version;
        // 1 — подпись на месте, -1 — проверка не прошла, 0 — выяснить не удалось.
        int signature = 0;
        QString signatureNote;
    };

    struct Diagnosis {
        Verdict verdict = Verdict::Ok;
        // Одна строка для журнала и заголовка окна. Пустая, когда всё в порядке.
        QString headline;
        // Разбор целиком: устройства с проблемами и состояние драйвера.
        QString details;
        // Запуск заведомо не состоится, и держать человека в ожидании незачем.
        bool blocksStart = false;
    };

    // verifySignature гоняет WinVerifyTrust и стоит десятки миллисекунд, поэтому
    // перед запуском профиля его не просим — только при разборе отказа.
    Diagnosis Diagnose(bool verifySignature);

    // Все сетевые устройства с их состоянием, как их видит диспетчер устройств.
    QList<NetDevice> NetDevices();

    // Драйвер Wintun: где лежит, тот ли это файл, цела ли подпись.
    DriverFile WintunDriver(bool verifySignature);

    // Раздел отчёта поддержки: устройства, драйвер, вклинившиеся драйверы.
    QString DevicesReport();

    // Запущенные драйверы и службы, которые вклиниваются в сетевой стек.
    // Имя процесса их не выдаёт: winws.exe переименовывается в один щелчок, а
    // WinDivert остаётся в списке служб под своим именем.
    QStringList RunningNetFilters();

    // Текст кода проблемы устройства, как его называет диспетчер устройств.
    QString ProblemText(quint32 problem);

    // Что сказать человеку про такой приговор. first и second подставляются в
    // текст: код проблемы, путь к драйверу, итог проверки подписи.
    //
    // Живёт отдельно от самого опроса, и не ради красоты: опрос компилируется
    // только под Windows, а lupdate на macOS видит лишь то, что попало в сборку.
    // Строки из файла, которого в ней нет, в перевод не приходят, а вписанные
    // руками пропадают при следующем обновлении из-за -no-obsolete.
    QString HeadlineFor(Verdict verdict, const QString &first = {}, const QString &second = {});

    // Запуск заведомо не состоится: ждать ответа Wintun пятнадцать секунд и
    // ставить в очередь ещё одну операцию с устройством незачем.
    bool BlocksStart(Verdict verdict);
} // namespace Sys::WinTunDiag
