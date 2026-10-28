#pragma once

#include "Const.hpp"
#include "Utils.hpp"
#include "include/database/DatabaseManager.h"

#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace API {
    struct PrivilegeInfo;
}

namespace Configs {
    void initDB(const std::string& dbPath);

    QString FindCoreRealPath();
#ifdef Q_OS_LINUX
    // Запущены ли мы из AppImage — в нём ядро лежит в образе только для чтения.
    bool RunningFromAppImage();
    // Кладёт ядро и его таблицы рядом с настройками, откуда их можно запускать
    // с правами. Без вызова этой функции TUN в AppImage не поднимется.
    void PrepareAppImageCore();
#endif

    bool IsAdmin(bool forceRenew=false);

    // The cached IsAdmin answer without an RPC or side effects; empty while unknown. Main thread.
    std::optional<bool> KnownAdmin();

    // Caches an answer IsAdmin would have fetched, such as the core's privilege probe. Main thread.
    void SetKnownAdmin(bool admin);

    // The core's last privilege report (the probe stores it next to SetKnownAdmin); empty while unknown. Main thread.
    std::optional<API::PrivilegeInfo> KnownCapabilities();

    void SetKnownCapabilities(const API::PrivilegeInfo &info);

    // Main thread, before refusing Tun on a cached tun_device == false (the device may appear while the core runs): asks the
    // connected core again within timeoutMs and updates both caches. No-op otherwise and on Windows.
    void RefreshStaleTunDevice(int timeoutMs);

    bool isSetuidSet(const std::string& path);

    // Set once at startup to the resolved data dir; before that GetBasePath() falls back to the old guess.
    void SetBasePath(const QString &path);

    // Каталог настроек — это подкаталог config рабочего, и живёт он отдельно от
    // GetBasePath(): туда Bootstrap переводит текущий каталог процесса, и туда
    // же ядро кладёт файлы, путь к которым определяется относительно него —
    // прежде всего cache.db. Пока не задан, совпадает с GetBasePath().
    // Этот запуск — возвращение после установки обновления. Один раз ведём
    // себя как «подключаться при запуске»: VPN выключил не человек, а
    // обновление, и оставлять его без защиты молча нельзя. Флаг в базе
    // снимается при первом обращении, а ответ держится до конца процесса —
    // спрашивают его в нескольких местах и в разное время.
    bool ResumingAfterUpdate();

    void SetConfigPath(const QString &path);
    QString GetConfigPath();

    QString GetBasePath();
} // namespace Configs

#define ROUTES_PREFIX_NAME QString("route_profiles")
#define ROUTES_PREFIX QString(ROUTES_PREFIX_NAME + "/")
