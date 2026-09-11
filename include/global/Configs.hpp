#pragma once

#include "Const.hpp"
#include "Utils.hpp"
#include "include/database/DatabaseManager.h"

#include <array>
#include <string_view>
#include <utility>

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

    bool isSetuidSet(const std::string& path);

    QString GetBasePath();
} // namespace Configs

#define ROUTES_PREFIX_NAME QString("route_profiles")
#define ROUTES_PREFIX QString(ROUTES_PREFIX_NAME + "/")
