# Отчёт о синхронизации форка с upstream

**Дата:** 04.09.2026
**Ветка:** `sync-upstream-2026-09`
**База:** `upstream/dev` = `339340e4` (04.09.2026)
**Источник доработок:** `origin/devtest` = `7dbc2337` (тег `1.1.2`, 09.02.2026)
**Точка расхождения:** `0dcc0e11` (09.02.2026)

## Итог

| Показатель | Было в форке | Перенесено |
|---|---|---|
| Файлов изменено | 34 | 28 |
| Строк добавлено | +2 394 | +1 602 |
| Коммитов | 31 | 12 |

Около трети наших доработок переносить не пришлось: за 7 месяцев upstream самостоятельно решил те же задачи, в большинстве случаев лучше. Ветка `origin/devtest` не изменялась, тег `1.1.2` по-прежнему указывает на рабочий релиз.

## Перенесено

| Доработка | Куда легла |
|---|---|
| Обновлятор для macOS (330 строк) | `src/updater/`, подключён в `CMakeLists.txt` под `if (APPLE)` |
| Проверка обновлений по релизам форка | `mainwindow_system.cpp`: `CheckUpdate`, `GetUpdateDirectory`, `PrepareUpdateEnvironment`, `InstallUpdateAndRestart` |
| Сравнение версий по тегу релиза | `mainwindow_system.cpp`: `compareVersions`, `isNewerByTag` |
| Детект конфликтующих VPN/антивирусов | новый `src/ui/mainWindow/mainwindow_conflicts.cpp` (243 строки), вызов в `profile_start` |
| Сбор отладочной информации | новый `src/ui/mainWindow/mainwindow_debuginfo.cpp` (429 строк) |
| Сброс режима VPN и ожидание завершения процессов при выходе | `mainwindow_system.cpp`: `prepare_exit` |
| Остановка VPN-процесса при выключении режима | `mainwindow_system.cpp`: `set_spmode_vpn` |
| Упрочнённое завершение core | `src/sys/Process.cpp`: `Kill`, `Restart` |
| Кнопки верхней панели (Service Menu, Update, Speedtest, Ping) | `mainwindow.ui` + `mainwindow_setup.cpp` |
| Запасной размер окна и минимум 1000×600 | `mainwindow.ui` + `mainwindow_setup.cpp` |
| Брендирование форка | `software_name`, User-Agent `Throne/<версия> (FSNT Fork)` |
| 16 изменённых значений по умолчанию | `include/database/SettingsRepo.h` |
| Дефолтный маршрут становится текущим | `src/global/Configs.cpp` |
| Понятная ошибка при сбое инициализации БД | `src/main.cpp` |
| Автоподстановка DNS в TUN-режиме | `src/configs/generate.cpp` |
| Ссылки `throne://subscribe?url=` | `mainwindow_deeplink.cpp`: глагол `subscribe` |
| Кнопка «Сброс и перезапуск» при сбое TUN | `mainwindow_profile_lifecycle.cpp` |
| Копирование обновлятора в macOS-бандл | `script/deploy_macos.sh` |
| Три иконки панели | `res/icon/`, `res/Throne.qrc` |
| 15 строк русского перевода | `res/translations/ru_RU.ts` |

## Перенесено с изменениями

1. **Сбор отладочной информации.** Читал файл нашего самодельного логгера. Переведён на `Logging::LogDir()` и `Logging::PreviousSessionLogPath()` — собирает логи с ротацией и логи прошлой сессии. Шесть полей настроек переименованы или удалены upstream, сопоставлены: `core_port` → `core_box_api_port`, `domain_strategy` → `resolve_domain_strategy`, `outbound_domain_strategy` → `default_domain_strategy`, `remote_dns_strategy` и `direct_dns_strategy` → производные от `*_disable_ipv6`, `sniffing_mode` удалён.
2. **Загрузка обновления.** Наш `DownloadUpdate` дублировал старый `DownloadAsset` и содержал ошибку: `readAll()` и `error()` вызывались после `deleteLater()`. Вместо переноса дублёра в апстримный `DownloadAsset` добавлен необязательный параметр `destinationDir` — все шесть существующих вызовов работают без правок.
3. **Кнопка Ping.** Функция `urltest_current_group` у upstream удалена; кнопка переведена на `ui->actionUrl_Test_Group->trigger()` — тем же способом, каким работает кнопка Speedtest.
4. **Действие сбора отладки.** Вместо своего `menu_collect_debug_info` использовано объявленное upstream, но нереализованное `menu_profile_debug_info`; добавлено в Hidden menu с горячей клавишей `Ctrl+Shift+D`.
5. **`exit_reason`.** Магические числа заменены на появившееся у upstream перечисление: `3` → `ExitReason::RestartWithTun`, `1` → `ExitReason::RunUpdater`.
6. **Дедупликация подписок.** Логика «найти группу по домену и обновить её» вынесена из `GroupUpdater` в апстримный `handle_addsub` — работает теперь и для `throne://addsub`, и для `throne://subscribe`.

## Отброшено: upstream реализовал сам, и лучше

| Наша доработка | Чем перекрыта |
|---|---|
| Логирование в `main.cpp` (`messageHandler`, `logFile`) | `src/global/Logger.cpp`: ротация 4 МБ × 3, crash-логи, ring-буфер, уровни. Два обработчика Qt-сообщений несовместимы |
| `registerUrlScheme()` (~90 строк) | `include/sys/UrlScheme.hpp`: `UrlScheme_RegisterIfNeeded/Install/Uninstall`, переключатель и кнопки в настройках, перерегистрация только при изменении |
| Класс `ThroneApplication` с обработкой `QFileOpenEvent` | `src/main.cpp:43-55` — та же логика, включая замечание про доставку `throne://` на macOS только через событие |
| Правки `get_elevated_permissions` | Целиком в upstream: `StopVPNProcess()` после chmod на Linux и при setuid на macOS, перезапуск Windows через `exit_reason` |
| Автоперезапуск с правами админа на первом запуске | `src/main.cpp:326`. Апстримная логика безопаснее: сбрасывает флаг до запроса, поэтому отказ не зацикливает перезапуск |
| Перезапуск профиля после обновления подписки (`restart-/start-/stop`) | Обходной путь для бага #1753. Upstream починил правильно: сопоставление по ключу идентичности, `BatchDeleteProfiles` защищает работающий профиль — он переживает обновление, сохраняя id. Перенос рвал бы соединение при каждом обновлении |
| Режимы колонок таблицы профилей | `refresh_proxy_list_column_size()`: именованные константы, защита от дребезга и повторного входа, обработка смены набора колонок. Колонка Name уже `Stretch` |
| `resizeEvent` | Наш — пустышка. Апстримный вызывает `scheduleProxyListRefresh()`; перенос сломал бы обновление списка |
| Пакетные операции и HWID в `GroupUpdater` | `BatchDeleteProfiles`, `AddProfileBatch`, `ImportBatch`, `GetProfileBatch`, `BATCH_LIMIT_WRITE=1500`. HWID шире нашего: `sub_send_hwid` + `sub_custom_hwid_params` + показ HWID в подсказке |
| `CFBundleURLTypes` в `MacOSXBundleInfo.plist` | Проверено по ключам: апстримный plist — строгое надмножество нашего, плюс `CFBundleDocumentTypes` |
| Сигнал `aboutToStartUpdater` | Не нужен: наш обновлятор ждёт завершения через `pgrep -x`, а сервер upstream закрывает в `aboutToQuit` |

## Отброшено по другим причинам

| Правка | Причина |
|---|---|
| `src/database/entities/Group.cpp` — удаление `RemoveProfileBatch` | upstream метод использует: `ProfilesRepo.cpp:406`. Перенос сломал бы сборку |
| `src/configs/common/utils.cpp` | Патч удалял перевод строки в конце файла — регресс |
| `include/configs/outbounds/wireguard.h` | upstream уже добавил `override` (строка 77) |
| `script/env_deploy.sh` | Файл удалён upstream |
| `APP_PROTOCOL` в `cmake/linux/`, `cmake/macos/` | Переменная нигде не читается: существовала только ради подстановки в наш plist |
| Размер `dialog_basic_settings.ui` | upstream уже 925×634 против наших 950×650 |
| Проверка обновлятора в `script/deploy_linux64.sh` | Дублирует `chmod +x` из `build_go.sh:14` |
| Формулировка лога в `GroupUpdater` | Наша (`"invalid outbound, skipping"`) теряет тип outbound'а — менее информативна |
| `xray_vless_preference = XhttpAndReality` | У upstream уже это значение |
| `remote_dns_strategy` / `direct_dns_strategy` = `prefer_ipv4` | В новой модели `*_disable_ipv6` мигрирует только `ipv4_only`; наше значение равно апстримному умолчанию |
| Регенерация `.ts` через `update_translations` | Давала 4 822 строки шума (перенумерация ссылок). 15 наших строк добавлены вручную — 60 строк вместо 4 822 |

## Требует решения владельца

1. **Автоподстановка DNS `1.1.1.1` вне macOS.** В `generate.cpp` форк заменил блокирующую ошибку «Local DNS и TUN несовместимы» на автоподстановку `1.1.1.1` и снял ограничение `getOS() == Darwin`. Перенесено как было. Но на Windows и Linux у upstream работает запасной вариант `local` (системный резолвер), и наша правка молча перезапишет его на Cloudflare в настройках пользователя. Возврат проверки `Darwin` — одна строка.
2. **Подтверждение при добавлении подписки по ссылке.** Форк добавлял подписку из `throne://subscribe` молча, upstream спрашивает подтверждение. Оставлено апстримное поведение: переход по ссылке из сети не должен без спроса менять состояние приложения. Пользователи форка получат один дополнительный клик.
3. **Подтверждение при перезапуске с правами администратора (Windows).** Форк перезапускал приложение молча, upstream спрашивает. Оставлено апстримное — оно же безопаснее при отказе в правах.
4. **Русские строки в `tr()`.** В диалоге о конфликтующих процессах исходные строки написаны по-русски (`tr("Продолжить запуск?")`), тогда как во всём остальном коде исходный язык — английский. Для русской локали работает, для остальных покажет русский текст. Перенесено как было.
5. **Ветка `production`.** Не трогалась. Она разошлась с релизной линией: 64 коммита вне неё, последняя активность 30.10.2025. Требует отдельного разбора.

## Требования локальной сборки

```bash
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
curl -fLso build/srslist.h "https://raw.githubusercontent.com/throneproj/routeprofiles/rule-set/srslist.h"
cmake --build build -j
```

Заголовок `srslist.h` (254 КБ, 2196 rule-set'ов) в репозитории не хранится — он в `.gitignore` и скачивается так же, как в CI (`.github/workflows/build.yml`).

## Проверено фактически

Чистая сборка с нуля: **224/224 цели, ноль ошибок**. Собраны `Throne` (15.9 МБ) и `updater` (75 КБ).

Приложение запущено, проработало 12 секунд без падения. Из созданной им базы подтверждены все 15 изменённых значений по умолчанию:

| Ключ | Значение |
|---|---|
| `theme` | `qdarkstyle` |
| `language` | `4` (ru_RU) |
| `sub_auto_update` | `120` |
| `sub_clear` | `true` |
| `sub_send_hwid` | `true` |
| `direct_dns` | `tls://77.88.8.8` |
| `vpn_mtu` | `1420` |
| `ruleset_mirror` | `0` (GITHUB) |
| `xray_log_level` | `info` |
| `random_inbound_port` | `true` |
| `enable_tun_routing` | `true` |
| `remember_enable` | `true` |
| `utlsFingerprint` | `chrome` |
| `domain_strategy` | `prefer_ipv4` |
| `outbound_domain_strategy` | `prefer_ipv4` |

Две последние стратегии записались под унаследованными ключами хранения — значит настройки существующих пользователей мигрируют на переименованные поля корректно.

Подсистема логирования upstream работает: создан `config/logs/throne.log` с заголовком сессии. Старый `~/Library/Application Support/Throne/throne_debug.log` от февраля не обновлялся — наш логгер убран чисто.

В логе две ошибки `execve: No such file or directory` — приложение не нашло ядро `ThroneCore`. Это ожидаемо: собиралась только C++-часть, Go-ядро строится отдельным шагом `script/build_go.sh` (в CI это отдельная стадия). К переносу отношения не имеет, чистый upstream вёл бы себя так же.

## Что осталось проверить руками

Без собранного ядра сетевые сценарии проверить нельзя, поэтому следующее требует полной сборки с `script/build_go.sh`:

1. Кнопки Speedtest и Ping на верхней панели.
2. `Ctrl+Shift+D` и пункт Debug Info в Hidden menu — создание архива, непустые логи внутри.
3. Включение и выключение TUN; после выключения VPN-процесса быть не должно.
4. Выход из приложения не оставляет процессов core/VPN.
5. Ссылка `throne://subscribe?url=...` открывается и добавляет подписку; повторный переход по ссылке того же домена обновляет группу, а не создаёт вторую.
6. Проверка обновлений находит релизы `forestsnet/Throne` и корректно сравнивает версии по тегу.
7. Дефолты при первом запуске: тема, русский язык, `direct_dns = tls://77.88.8.8`, подписки раз в 120 минут, MTU 1420.
8. Предупреждение о конфликтующих процессах при старте профиля в TUN-режиме.
