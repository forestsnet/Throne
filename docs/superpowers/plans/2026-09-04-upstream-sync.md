# Синхронизация форка forestsnet/Throne с upstream throneproj/Throne

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Перенести все доработки форка (31 коммит, 34 файла) поверх актуального `upstream/dev`, получив собираемую ветку с чистой историей, где каждая фича — отдельный коммит.

**Architecture:** Не мерджим upstream в форк, а переносим форк на upstream. Новая ветка создаётся от `upstream/dev`; наши доработки накладываются группами по одной фиче на коммит. Ключевая сложность — upstream разбил монолитный `src/ui/mainwindow.cpp` (2738 строк) на 13 файлов в `src/ui/mainWindow/`, поэтому наш патч на +1072 строки переносится туда пофункционально вручную.

**Tech Stack:** C++17, Qt 6, CMake, sing-box (Go), макOS/Windows/Linux.

**Spec:** этот документ самодостаточен; исходные данные анализа — в разделе «Контекст» ниже.

---

## Контекст (зафиксированные факты)

| Параметр | Значение |
|---|---|
| Точка расхождения (merge-base) | `0dcc0e1177ceb0271951fe739c094c7c8547bec2` (09.02.2026) |
| Наш HEAD (`origin/devtest`, = тег `1.1.2`) | `7dbc2337441efec88bb43c6fbf1d5708661ce117` |
| Upstream HEAD (`upstream/dev`) | `339340e4357f2e842ec3be7f362607b6e29c3dbc` (04.09.2026) |
| Upstream ушёл вперёд | 547 коммитов, 699 файлов, +83 960 / −22 654 |
| Наших изменений | 31 коммит, 34 файла, +2 394 / −211 |
| Пробный merge | 18 конфликтных файлов, 34 хунка, 2 × modify/delete |

Целевая ветка форка — `devtest`: тег последнего релиза `1.1.2` указывает ровно на её HEAD, предыдущие релизы (1.1.1, 1.1.0, 1.0.32, 1.0.30) — тоже с неё. Ветку `production` в этом плане **не трогаем**: она разошлась с релизной линией (64 коммита вне неё) и требует отдельного разбора.

### Вердикт по фичам, которые могли дублироваться с upstream

Проверено пофайлово, вывод по каждой:

1. **Логирование — НАШЕ ВЫБРОСИТЬ.** Upstream добавил полноценный `src/global/Logger.cpp` (namespace `Logging`: ротация 4 МБ × 3 файла, crash-логи, ring-буфер, уровни). Наш самодельный `messageHandler` + `QFile* logFile` в `src/main.cpp` полностью им перекрывается и будет конфликтовать за Qt message handler. Публичный API upstream: `Logging::InstallQtMessageHandler()`, `Init(baseDir)`, `Shutdown()`, `SetLevel(Level)`, `Write(...)`, `WriteUserLog(...)`, `RecentLines(int)`, `FlushForCrash()`, `PreviousSessionLogPath()`, `LogDir()`, `CrashDir()`.
2. **Сбор debug-info — НАШЕ ОСТАВИТЬ.** Upstream объявил в `include/ui/mainwindow.ui:861` действие `menu_profile_debug_info`, но **слота к нему нет**: поиск по всему `upstream/dev` даёт единственное вхождение — саму строку в `.ui`. Реализации не существует. Наш сборщик переносим и вешаем на это готовое действие.
3. **Speedtest / Ping — НАШЕ ОСТАВИТЬ.** Наши `toolButton_speedtest` и `toolButton_ping` — не своя реализация, а кнопки-ярлыки на тулбаре: первая дёргает существующий `ui->actionSpeedtest_Group->trigger()`, вторая вызывает `urltest_current_group(...)`. Upstream-овский `TestConfig::SpeedTestMode` (FULL/DL/UL/SIMPLEDL/COUNTRY) — про тестирование профилей, это другая функция. Конфликта нет.

### Важно про тесты

В проекте **нет собственного тест-сьюта** (тесты есть только у вендоренного `3rdparty/simple-protobuf`). Поэтому критерий приёмки каждой задачи — **успешная конфигурация и сборка CMake**, плюс точечная ручная проверка. Ни в одном шаге ниже не выдумывать несуществующие тестовые команды.

## Уточнения, установленные при исполнении (04.09.2026)

Проверено на реальном дереве `upstream/dev` — эти пункты **отменяют** соответствующие предположения в задачах ниже.

1. **`RemoveProfileBatch` — наше удаление НЕ переносим.** Upstream активно использует метод: вызов в `src/database/ProfilesRepo.cpp:406`, определение `src/database/entities/Group.cpp:150`, объявление `include/database/entities/Group.h:70`. Наш коммит `8be3889b` его удалял — перенос сломал бы апстримный код. Task 5 Step 3: правку `Group.cpp` пропустить.
2. **`script/env_deploy.sh` — удалён upstream.** Наш патч к нему неприменим. Task 6 Step 3: пропустить.
3. **`resizeEvent` — уже объявлен upstream** в `include/ui/mainwindow.h:385`. Task 7 Step 3: своё объявление не добавлять, только сверить реализацию.
4. **Патч `GroupUpdater.cpp` — это НЕ пакетные операции и НЕ HWID.** Его реальное содержимое (+140 строк): разбор диплинка `throne://subscribe?url=…` и умное управление группами подписок — поиск существующей группы по домену, обновление её URL, создание новой с именем по домену, удаление пустой группы `Default`. Пакетность у upstream своя и шире нашей (`BatchDeleteProfiles`, `AddProfileBatch`, `ImportBatch`, `GetProfileBatch`, `BATCH_LIMIT_WRITE=1500`, `BATCH_LIMIT_READ=4096`, плюс фикс #1753). HWID у upstream тоже полный и шире нашего (`sub_send_hwid` + `sub_custom_hwid_params`, тултип с показом HWID, передача в `HttpGet`). Task 10 переписан: переносим только логику диплинка и групп.
5. **Требования локальной сборки.** Нужен заголовок `srslist.h` (254 КБ, 2196 rule-set'ов) в каталоге сборки — он в `.gitignore` и скачивается так же, как в CI:
   ```bash
   curl -fLso <build-dir>/srslist.h "https://raw.githubusercontent.com/throneproj/routeprofiles/rule-set/srslist.h"
   ```
   Сборка запускается с `export CMAKE_PREFIX_PATH="$(brew --prefix qt)"` и генератором Ninja. Проверено: чистый `upstream/dev` собирается без ошибок, бинарь 15.5 МБ.

## Global Constraints

- Ветка-приёмник: `sync-upstream-2026-09`, создаётся от `339340e4` (`upstream/dev`).
- `origin/devtest` **не трогать ни при каких условиях** — это релизная ветка и наш единственный резерв.
- Один коммит = одна фича. Сообщения коммитов на русском, в стиле существующей истории форка («Добавить…», «Исправить…», «Улучшить…»).
- В коммиты, их тела, названия и описания PR **не добавлять** упоминания Claude / Anthropic / AI и трейлер `Co-Authored-By`.
- Эталон наших изменений всегда берётся как `git diff 0dcc0e11 7dbc2337 -- <файл>`. Ниже эти хеши обозначены `$BASE` и `$OURS`.
- После каждой задачи, затрагивающей C++, проект должен собираться. Сломанную сборку не коммитить.
- Наши правки `include/database/SettingsRepo.h` — **только смена значений по умолчанию**, новых полей мы не заводили. Не превращать их в новые поля.

## File Structure

**Переносится без конфликтов (6 файлов, upstream их не касался):**
- `src/updater/main.cpp` (281 стр.), `src/updater/CMakeLists.txt` (49 стр.) — отдельный бинарь-апдейтер для macOS
- `res/icon/hidden-menu.png`, `res/icon/network-ping.png`, `res/icon/speedtest.png`
- `include/ui/setting/dialog_hotkey.h`

**Правится поверх upstream (мелкие конфликты, 15 файлов):**
- `include/database/SettingsRepo.h` (7 хунков) — дефолты
- `src/main.cpp` (5) — URL-схема, минус наше логирование
- `src/configs/sub/GroupUpdater.cpp` (4) — пакетные операции
- `include/ui/mainwindow.ui` (4), `include/ui/mainwindow.h` (3) — UI
- `script/deploy_macos.sh` (2), `script/env_deploy.sh`, `.gitignore`, `cmake/macos/macos.cmake`, `CMakeLists.txt`, `res/Throne.qrc`, `res/MacOSXBundleInfo.plist`, `res/translations/ru_RU.ts`, `src/database/SettingsRepo.cpp`, `src/configs/generate.cpp`, `include/global/HTTPRequestHelper.hpp`, `src/global/HTTPRequestHelper.cpp`, `src/global/Configs.cpp`, `src/sys/Process.cpp`, `src/configs/common/utils.cpp`, `include/configs/outbounds/wireguard.h`, `src/database/entities/Group.cpp`, `cmake/linux/linux.cmake`, `script/build_go.sh`, `script/deploy_linux64.sh`, `include/ui/setting/dialog_basic_settings.ui`

**Переносится вручную по функциям (главная работа):**

`src/ui/mainwindow.cpp` (+1072 наших строки) и `src/ui/mainwindow_rpc.cpp` (+54) удалены upstream. Карта, куда переехала каждая функция, которую мы правили:

| Наша функция | Новый файл upstream |
|---|---|
| `MainWindow::MainWindow` (конструктор) | `src/ui/mainWindow/mainwindow_setup.cpp` |
| `StopVPNProcess`, `prepare_exit`, `set_spmode_vpn`, `CheckUpdate`, `get_elevated_permissions`, `on_menu_exit_triggered` | `src/ui/mainWindow/mainwindow_system.cpp` |
| `closeEvent`, `resizeEvent` | `src/ui/mainWindow/mainwindow_events.cpp` |
| `refresh_status` | `src/ui/mainWindow/mainwindow_view.cpp` |
| `show_group` | `src/ui/mainWindow/mainwindow_groups.cpp` |
| `on_menu_update_subscription_triggered` | `src/ui/mainWindow/mainwindow_profiles.cpp` |
| `dialog_message_impl` | `src/ui/mainWindow/mainwindow_deeplink.cpp` |

Все 13 функций у upstream существуют — перенос механический, переписывания логики не требуется.

---

### Task 1: Подготовка ветки и рабочих артефактов

**Files:**
- Create: `/tmp/throne-sync/` (вне репозитория — эталонные патчи)

**Interfaces:**
- Produces: ветка `sync-upstream-2026-09` от `upstream/dev`; каталог с эталонными диффами всех 34 файлов.

- [ ] **Step 1: Убедиться, что рабочее дерево чистое и ветки на месте**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
git status --short
git rev-parse origin/devtest upstream/dev
```

Ожидаемо: пустой вывод `status`, хеши `7dbc2337…` и `339340e4…`.

- [ ] **Step 2: Выгрузить эталонные патчи всех наших изменений**

```bash
mkdir -p /tmp/throne-sync/patches
cd /Users/admin/work_vpn/throne_dev/Throne
BASE=0dcc0e1177ceb0271951fe739c094c7c8547bec2
OURS=7dbc2337441efec88bb43c6fbf1d5708661ce117
for f in $(git diff --name-only $BASE $OURS); do
  out="/tmp/throne-sync/patches/$(echo "$f" | tr '/' '_').patch"
  git diff $BASE $OURS -- "$f" > "$out"
done
ls -la /tmp/throne-sync/patches | head -40
```

Ожидаемо: 34 файла патчей.

- [ ] **Step 3: Создать ветку синхронизации от upstream**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
git checkout -b sync-upstream-2026-09 339340e4357f2e842ec3be7f362607b6e29c3dbc
git log --oneline -1
```

Ожидаемо: `339340e4 improve db error handling`.

- [ ] **Step 4: Зафиксировать базовую собираемость ДО наших правок**

Это контрольная точка: если сборка упадёт позже, будет ясно, наша ли это вина.

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
cmake -B build-baseline -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -20
```

Ожидаемо: конфигурация проходит. Если падает из-за отсутствующих зависимостей (Qt, Go, sing-box) — сначала поднять окружение по `.github/workflows/build.yml`, и только потом продолжать. **Не начинать перенос на несобираемой базе.**

- [ ] **Step 5: Коммит не нужен, зафиксировать состояние**

```bash
git status --short
```

Ожидаемо: чисто (каталог `build-baseline/` должен попадать под `.gitignore`; если нет — не добавлять его в индекс).

---

### Task 2: Файлы без конфликтов — апдейтер, иконки, хоткеи

**Files:**
- Create: `src/updater/main.cpp`, `src/updater/CMakeLists.txt`
- Create: `res/icon/hidden-menu.png`, `res/icon/network-ping.png`, `res/icon/speedtest.png`
- Modify: `include/ui/setting/dialog_hotkey.h`
- Modify: `CMakeLists.txt` (подключение подкаталога)

**Interfaces:**
- Produces: собираемая цель `updater` на macOS; иконки, доступные для `res/Throne.qrc` в Task 6.

- [ ] **Step 1: Перенести файлы, которых upstream не касался, ровно как есть**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
OURS=7dbc2337441efec88bb43c6fbf1d5708661ce117
git checkout $OURS -- src/updater/main.cpp src/updater/CMakeLists.txt \
  res/icon/hidden-menu.png res/icon/network-ping.png res/icon/speedtest.png \
  include/ui/setting/dialog_hotkey.h
git status --short
```

Ожидаемо: 6 файлов в индексе (`A` для новых, `M` для `dialog_hotkey.h`).

- [ ] **Step 2: Проверить, что правка dialog_hotkey.h не конфликтует с upstream**

upstream этот файл не менял, но проверить обязательно — за 7 месяцев мог измениться его контекст.

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
git diff --cached include/ui/setting/dialog_hotkey.h
git show upstream/dev:include/ui/setting/dialog_hotkey.h | diff - <(git show 0dcc0e11:include/ui/setting/dialog_hotkey.h) && echo "IDENTICAL — безопасно"
```

Ожидаемо: `IDENTICAL — безопасно`. Если файлы разошлись — не перезаписывать целиком, а наложить только наш хунк вручную.

- [ ] **Step 3: Подключить апдейтер в корневой CMakeLists.txt**

Наш патч добавляет в самый конец файла (после `qt_finalize_executable(Throne)`):

```cmake

#### Throne Updater (macOS only - Windows/Linux use official Odin updater) ####
if (APPLE)
    add_subdirectory(src/updater)
endif()
```

Найти в upstream-версии `CMakeLists.txt` строку `qt_finalize_executable(Throne)` и дописать блок сразу после неё. upstream переписал 335 строк этого файла, поэтому **не применять патч автоматически** — вставить вручную в конец.

- [ ] **Step 4: Проверить сборку**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
cmake -B build -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -20
```

Ожидаемо: конфигурация проходит, цель `updater` появляется (на macOS).

- [ ] **Step 5: Коммит**

```bash
git add src/updater res/icon/hidden-menu.png res/icon/network-ping.png \
  res/icon/speedtest.png include/ui/setting/dialog_hotkey.h CMakeLists.txt
git commit -m "Добавить отдельный обновлятор для macOS, иконки тулбара и правку хоткеев"
```

---

### Task 3: Значения по умолчанию

**Files:**
- Modify: `include/database/SettingsRepo.h`
- Modify: `src/database/SettingsRepo.cpp`
- Modify: `src/global/Configs.cpp`

**Interfaces:**
- Consumes: ничего.
- Produces: изменённые дефолты, на которые опирается поведение UI в Task 8.

- [ ] **Step 1: Применить наши дефолты в SettingsRepo.h**

Это **правка значений существующих полей**, а не добавление новых. Для каждого поля ниже найти его объявление в upstream-версии файла и заменить только значение по умолчанию:

```cpp
QString theme = "qdarkstyle";
int language = 4;                  // 0-system, 1-en, 2-zh-CN, 3-zh-TW, 4-auto
int sub_auto_update = 120;         // минуты; было 30
bool sub_clear = true;
bool sub_send_hwid = true;
QString utlsFingerprint = "chrome";
bool remember_enable = true;
bool windows_set_admin = true;
QString remote_dns_strategy = "prefer_ipv4";
QString direct_dns = "tls://77.88.8.8";
QString direct_dns_strategy = "prefer_ipv4";
QString domain_strategy = "prefer_ipv4";
QString outbound_domain_strategy = "prefer_ipv4";
int ruleset_mirror = Mirrors::GITHUB;
bool random_inbound_port = true;
bool enable_tun_routing = true;
int vpn_mtu = 1420;
QString xray_log_level = "info";
Xray::XrayVlessPreference xray_vless_preference = Xray::XhttpAndReality;
```

Сверяться с эталоном: `/tmp/throne-sync/patches/include_database_SettingsRepo.h.patch`.

**Если поля в upstream больше нет** — не создавать его заново, а зафиксировать в списке отброшенного (Task 11) и идти дальше.

- [ ] **Step 2: Проверить каждое поле на существование в upstream**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
for id in theme language sub_auto_update sub_clear sub_send_hwid utlsFingerprint \
          remember_enable windows_set_admin remote_dns_strategy direct_dns \
          direct_dns_strategy domain_strategy outbound_domain_strategy \
          ruleset_mirror random_inbound_port enable_tun_routing vpn_mtu \
          xray_log_level xray_vless_preference; do
  printf "%-30s " "$id"
  grep -q "\b$id\b" include/database/SettingsRepo.h && echo "OK" || echo "!!! ОТСУТСТВУЕТ"
done
```

Ожидаемо: все `OK`. Каждое `!!! ОТСУТСТВУЕТ` — записать в список отброшенного.

- [ ] **Step 3: Применить правку Configs.cpp — дефолтный маршрут делаем текущим**

В `src/global/Configs.cpp`, в блоке создания дефолтного маршрута, после `AddRouteProfile(defaultRoute);` добавить:

```cpp
            // Устанавливаем созданный дефолтный маршрут как текущий
            dataManager->settingsRepo->current_route_id = defaultRoute->id;
            dataManager->settingsRepo->Save();
```

- [ ] **Step 4: Применить правку src/database/SettingsRepo.cpp (1 хунк)**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
cat /tmp/throne-sync/patches/src_database_SettingsRepo.cpp.patch
```

Наложить единственный хунк вручную, сверяясь с текущим содержимым upstream-версии.

- [ ] **Step 5: Собрать**

```bash
cmake --build build --target Throne -j 2>&1 | tail -30
```

Ожидаемо: сборка проходит.

- [ ] **Step 6: Коммит**

```bash
git add include/database/SettingsRepo.h src/database/SettingsRepo.cpp src/global/Configs.cpp
git commit -m "Обновить настройки по умолчанию: тема, язык, DNS, интервал подписок, MTU"
```

---

### Task 4: main.cpp — URL-схема throne:// и отказ от своего логирования

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `Logging::` API из `include/global/Logger.hpp` (upstream).
- Produces: функция `registerUrlScheme()`, доступная в `main()`.

- [ ] **Step 1: Изучить эталон и текущее состояние upstream**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
cat /tmp/throne-sync/patches/src_main.cpp.patch
echo "=========== UPSTREAM ==========="
sed -n '1,80p' src/main.cpp
```

- [ ] **Step 2: Перенести registerUrlScheme() — это наше, аналога у upstream нет**

Взять тело функции из эталона:

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
git show 7dbc2337441efec88bb43c6fbf1d5708661ce117:src/main.cpp | \
  sed -n '/^void registerUrlScheme()/,/^}/p'
```

Вставить в `src/main.cpp` и добавить её вызов в `main()` там же, где он стоял у нас (сверить по эталонному патчу).

- [ ] **Step 3: НЕ переносить наше логирование**

Из нашего патча **сознательно отбрасываются**:
- `static QFile* logFile = nullptr;`
- `void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)`
- соответствующий вызов `qInstallMessageHandler(messageHandler)`

Причина: upstream добавил `src/global/Logger.cpp` с ротацией, crash-логами и ring-буфером. Два обработчика Qt-сообщений одновременно жить не могут — второй затрёт первый. Убедиться, что upstream уже ставит свой:

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
grep -n "Logging::InstallQtMessageHandler\|Logging::Init" src/main.cpp
```

Ожидаемо: обе строки присутствуют. Если нет — остановиться и разобраться, где upstream инициализирует логгер, прежде чем идти дальше.

- [ ] **Step 4: Перенести остальные хунки main.cpp**

Оставшиеся правки касаются обработки `throne://` и очистки аргументов командной строки. Накладывать вручную по эталонному патчу, пропуская всё, что относится к `logFile` / `messageHandler`.

- [ ] **Step 5: Собрать**

```bash
cmake --build build --target Throne -j 2>&1 | tail -30
```

Ожидаемо: сборка проходит, предупреждений о неиспользуемом `logFile` нет.

- [ ] **Step 6: Коммит**

```bash
git add src/main.cpp
git commit -m "Добавить регистрацию URL-схемы throne:// и обработку аргументов запуска"
```

---

### Task 5: Системные правки — процессы, HTTP, генерация конфигов

**Files:**
- Modify: `src/sys/Process.cpp`
- Modify: `src/global/HTTPRequestHelper.cpp`, `include/global/HTTPRequestHelper.hpp`
- Modify: `src/configs/generate.cpp`, `src/configs/common/utils.cpp`
- Modify: `include/configs/outbounds/wireguard.h`, `src/database/entities/Group.cpp`

**Interfaces:**
- Produces: helper-функции загрузки файлов для апдейтера (используются в Task 8, `CheckUpdate`).

- [ ] **Step 1: Наложить правки Process.cpp**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
cat /tmp/throne-sync/patches/src_sys_Process.cpp.patch
```

Наш патч (+35 строк) добавляет более жёсткое завершение процессов. upstream изменил этот файл на 42 строки — накладывать вручную, сверяя контекст.

- [ ] **Step 2: Наложить правки HTTPRequestHelper (нужны апдейтеру)**

```bash
cat /tmp/throne-sync/patches/src_global_HTTPRequestHelper.cpp.patch
cat /tmp/throne-sync/patches/include_global_HTTPRequestHelper.hpp.patch
```

Не забыть `#include <QDir>` в `.cpp` — у нас он добавлялся отдельным коммитом (`75e3a97e`), без него сборка падает.

- [ ] **Step 3: Наложить оставшиеся мелкие правки**

```bash
for p in src_configs_generate.cpp src_configs_common_utils.cpp \
         include_configs_outbounds_wireguard.h src_database_entities_Group.cpp; do
  echo "=== $p ==="; cat "/tmp/throne-sync/patches/$p.patch"
done
```

Каждый — 1–2 хунка. Обратить внимание: `src/database/entities/Group.cpp` у нас **удаляет** метод `RemoveProfileBatch` (коммит `8be3889b`). Проверить, не использует ли upstream этот метод:

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
grep -rn "RemoveProfileBatch" src include | head
```

Если upstream его использует — **наше удаление не переносить**, записать в список отброшенного.

- [ ] **Step 4: Собрать**

```bash
cmake --build build --target Throne -j 2>&1 | tail -30
```

- [ ] **Step 5: Коммит**

```bash
git add src/sys/Process.cpp src/global/HTTPRequestHelper.cpp \
  include/global/HTTPRequestHelper.hpp src/configs/generate.cpp \
  src/configs/common/utils.cpp include/configs/outbounds/wireguard.h \
  src/database/entities/Group.cpp
git commit -m "Улучшить завершение процессов и добавить загрузку файлов для обновлятора"
```

---

### Task 6: Сборочная обвязка и ресурсы

**Files:**
- Modify: `.gitignore`, `cmake/linux/linux.cmake`, `cmake/macos/macos.cmake`
- Modify: `script/build_go.sh`, `script/deploy_linux64.sh`, `script/deploy_macos.sh`, `script/env_deploy.sh`
- Modify: `res/Throne.qrc`, `res/MacOSXBundleInfo.plist`

**Interfaces:**
- Consumes: иконки из Task 2.
- Produces: `res/Throne.qrc` с тремя нашими иконками; `Info.plist` с зарегистрированной URL-схемой `throne://`.

- [ ] **Step 1: Добавить иконки в ресурсы**

В `res/Throne.qrc` добавить три строки в тот же блок, где перечислены остальные иконки:

```xml
<file>icon/hidden-menu.png</file>
<file>icon/network-ping.png</file>
<file>icon/speedtest.png</file>
```

- [ ] **Step 2: Наложить правки Info.plist — регистрация URL-схемы**

```bash
cat /tmp/throne-sync/patches/res_MacOSXBundleInfo.plist.patch
```

upstream добавил в этот файл 29 строк. Наш патч (+77) регистрирует `CFBundleURLTypes` для `throne://`. Наложить вручную, не затирая апстримные ключи.

- [ ] **Step 3: Наложить правки скриптов и cmake**

```bash
for p in .gitignore cmake_linux_linux.cmake cmake_macos_macos.cmake \
         script_build_go.sh script_deploy_linux64.sh script_deploy_macos.sh \
         script_env_deploy.sh; do
  echo "=== $p ==="; cat "/tmp/throne-sync/patches/$p.patch" 2>/dev/null
done
```

Особое внимание `script/env_deploy.sh`: в пробном мердже это был конфликт `UD` — **upstream удалил у себя строки, которые мы правили**. Проверить, существует ли файл сейчас:

```bash
ls -la script/env_deploy.sh 2>/dev/null || echo "УДАЛЁН UPSTREAM — наш патч не переносим"
```

- [ ] **Step 4: Проверить, что сборка и деплой-скрипты не сломаны**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
bash -n script/deploy_macos.sh && bash -n script/build_go.sh && \
  bash -n script/deploy_linux64.sh && echo "Синтаксис скриптов OK"
cmake -B build -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -15
```

- [ ] **Step 5: Коммит**

```bash
git add .gitignore cmake script res/Throne.qrc res/MacOSXBundleInfo.plist
git commit -m "Обновить сборочные скрипты, ресурсы и регистрацию схемы throne:// в бандле"
```

---

### Task 7: Разметка интерфейса

**Files:**
- Modify: `include/ui/mainwindow.ui`
- Modify: `include/ui/setting/dialog_basic_settings.ui`
- Modify: `include/ui/mainwindow.h`

**Interfaces:**
- Produces: виджеты `toolButton_hidden`, `toolButton_speedtest`, `toolButton_ping`; объявления слотов и `resizeEvent`, которые реализуются в Task 8.

- [ ] **Step 1: Наложить правки mainwindow.ui (4 конфликтных хунка)**

```bash
cat /tmp/throne-sync/patches/include_ui_mainwindow.ui.patch
```

Наши добавления — три кнопки тулбара:

```xml
<widget class="QToolButton" name="toolButton_hidden">
  <property name="icon"><iconset theme="hidden-menu"/></property>
</widget>
<widget class="QToolButton" name="toolButton_speedtest">
  <property name="toolTip"><string>Speedtest</string></property>
</widget>
<widget class="QToolButton" name="toolButton_ping">
  <property name="toolTip"><string>Ping</string></property>
  <property name="icon"><iconset theme="network-ping"/></property>
</widget>
```

upstream переписал этот файл на 464 строки — вставлять в актуальную структуру layout'а, а не по номерам строк из патча.

- [ ] **Step 2: Наложить правку dialog_basic_settings.ui (1 хунк)**

Наш патч меняет всего 4 строки (размеры диалога), тогда как upstream переписал 1549. Проверить, актуальна ли ещё правка:

```bash
cat /tmp/throne-sync/patches/include_ui_setting_dialog_basic_settings.ui.patch
```

Если upstream уже задал разумные размеры — **правку не переносить**, записать в список отброшенного.

- [ ] **Step 3: Наложить правки mainwindow.h (3 хунка)**

Нужны объявления: `void resizeEvent(QResizeEvent *event) override;`, слот сбора debug-info и вспомогательные методы. Сверяться с эталоном:

```bash
cat /tmp/throne-sync/patches/include_ui_mainwindow.h.patch
```

Убедиться, что `resizeEvent` ещё не объявлен upstream:

```bash
grep -n "resizeEvent" include/ui/mainwindow.h
```

- [ ] **Step 4: Проверить, что uic обрабатывает формы**

```bash
cmake --build build --target Throne -j 2>&1 | tail -30
```

На этом шаге сборка **может упасть на неразрешённых символах** объявленных, но ещё не реализованных слотов — это ожидаемо, они появятся в Task 8. Главное, что `uic`/`moc` отрабатывают без ошибок разбора форм.

- [ ] **Step 5: Коммит**

```bash
git add include/ui/mainwindow.ui include/ui/setting/dialog_basic_settings.ui include/ui/mainwindow.h
git commit -m "Добавить кнопки тулбара и объявления обработчиков главного окна"
```

---

### Task 8: Перенос логики главного окна в новую структуру upstream

Это ядро работы: +1072 строки из удалённого `src/ui/mainwindow.cpp` и +54 из `src/ui/mainwindow_rpc.cpp` раскладываются по файлам `src/ui/mainWindow/`.

**Files:**
- Modify: `src/ui/mainWindow/mainwindow_setup.cpp`, `mainwindow_system.cpp`, `mainwindow_events.cpp`, `mainwindow_view.cpp`, `mainwindow_groups.cpp`, `mainwindow_profiles.cpp`, `mainwindow_deeplink.cpp`

**Interfaces:**
- Consumes: объявления из Task 7 (`include/ui/mainwindow.h`), виджеты из `mainwindow.ui`, `Logging::` API.
- Produces: полностью перенесённое поведение форка в главном окне.

- [ ] **Step 1: Выгрузить наши версии функций как справочный материал**

```bash
mkdir -p /tmp/throne-sync/funcs
cd /Users/admin/work_vpn/throne_dev/Throne
OURS=7dbc2337441efec88bb43c6fbf1d5708661ce117
git show $OURS:src/ui/mainwindow.cpp > /tmp/throne-sync/funcs/mainwindow.cpp.ours
git show $OURS:src/ui/mainwindow_rpc.cpp > /tmp/throne-sync/funcs/mainwindow_rpc.cpp.ours
git diff 0dcc0e11 $OURS -- src/ui/mainwindow.cpp > /tmp/throne-sync/funcs/mainwindow.diff
wc -l /tmp/throne-sync/funcs/*
```

- [ ] **Step 2: Перенести правки конструктора → mainwindow_setup.cpp**

Наши добавления в `MainWindow::MainWindow`:
- минимальный размер окна и восстановление геометрии (применять сохранённый размер, но не меньше минимального);
- подключение кнопок тулбара:

```cpp
    ui->horizontalLayout_2->addWidget(ui->toolButton_speedtest);
    ui->horizontalLayout_2->addWidget(ui->toolButton_ping);
    ui->horizontalLayout_2->addSpacing(50);

    connect(ui->toolButton_speedtest, &QToolButton::clicked, this, [=,this]() {
        ui->actionSpeedtest_Group->trigger();
    });

    connect(ui->toolButton_ping, &QToolButton::clicked, this, [=,this]() {
        auto currentGroup = Configs::dataManager->groupsRepo->CurrentGroup();
        if (!currentGroup || currentGroup->Profiles().isEmpty()) {
            MessageBoxWarning(tr("Ping Test"), tr("No profiles in current group."));
            return;
        }
        urltest_current_group(Configs::dataManager->groupsRepo->CurrentGroup()->Profiles());
    });
```

- меню на `toolButton_update` (проверить обновления / GitHub releases / страница проекта);
- режимы колонок таблицы: `Name` — `Stretch`, остальные — `ResizeToContents`, плюс минимальная ширина секций;
- шорткат `Ctrl+Shift+D` на сбор debug-info.

Точные тела брать из `/tmp/throne-sync/funcs/mainwindow.cpp.ours`.

- [ ] **Step 3: Проверить сборку после setup**

```bash
cmake --build build --target Throne -j 2>&1 | tail -30
```

- [ ] **Step 4: Перенести системные функции → mainwindow_system.cpp**

Переносятся правки в `StopVPNProcess`, `prepare_exit`, `set_spmode_vpn`, `CheckUpdate`, `get_elevated_permissions`, `on_menu_exit_triggered`. Смысл наших доработок:
- принудительное завершение core с многократной проверкой и обработкой событий Qt между попытками;
- гарантированная остановка VPN-процесса при выключении режима и при выходе;
- закрытие `QLocalServer` **перед** запуском апдейтера (иначе апдейтер считает приложение работающим);
- увеличенная задержка перед `quit()`;
- `exit_reason = 3` для автоматического включения VPN после перезапуска;
- на Windows — автоматический перезапуск с правами администратора, на остальных ОС — диалог;
- перезапуск активного профиля при **включении** VPN (при выключении — только остановка).

- [ ] **Step 5: Проверить сборку после system**

```bash
cmake --build build --target Throne -j 2>&1 | tail -30
```

- [ ] **Step 6: Перенести обработчики событий → mainwindow_events.cpp**

`closeEvent` — наши правки завершения; `resizeEvent` — новый метод целиком:

```cpp
void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    // ResizeToContents колонки обновляются автоматически
    // Stretch колонка (Name) адаптируется автоматически
    // Дополнительных действий не требуется
}
```

Тело сверить с `/tmp/throne-sync/funcs/mainwindow.cpp.ours` — там оно может быть полнее.

- [ ] **Step 7: Перенести оставшиеся четыре функции**

- `refresh_status` → `mainwindow_view.cpp`: наша правка закомментировала вывод версии NKR при отсутствии значка в трее (строка `// if (!isTray) tt << QString(NKR_VERSION);`).
- `show_group` → `mainwindow_groups.cpp`.
- `on_menu_update_subscription_triggered` → `mainwindow_profiles.cpp`: обработка команд после обновления подписки.
- `dialog_message_impl` → `mainwindow_deeplink.cpp`: обработка `throne://`.

- [ ] **Step 8: Перенести правки mainwindow_rpc.cpp (+54 строки)**

Файл удалён upstream. Определить, куда переехало его содержимое, и перенести туда:

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
cat /tmp/throne-sync/funcs/mainwindow_rpc.cpp.ours | head -40
grep -rln "RPC\|rpc" src/ui/mainWindow/ | head
```

- [ ] **Step 9: Полная сборка**

```bash
cmake --build build --target Throne -j 2>&1 | tail -40
```

Ожидаемо: сборка проходит без ошибок и без неразрешённых символов.

- [ ] **Step 10: Коммит**

```bash
git add src/ui/mainWindow/
git commit -m "Перенести доработки главного окна: геометрия, кнопки тулбара, завершение процессов и режим VPN"
```

---

### Task 9: Сбор отладочной информации

**Files:**
- Modify: `src/ui/mainWindow/mainwindow_system.cpp` (или отдельный новый файл, см. шаг 1)
- Modify: `include/ui/mainwindow.h`

**Interfaces:**
- Consumes: `Logging::RecentLines()`, `Logging::LogDir()`, `Logging::PreviousSessionLogPath()` — вместо нашего выброшенного логгера.
- Produces: рабочее действие `menu_profile_debug_info`, объявленное upstream в `.ui`, но не реализованное.

- [ ] **Step 1: Решить, куда положить реализацию**

Наш сборщик — крупная самостоятельная функция. Логичнее вынести её в отдельный файл `src/ui/mainWindow/mainwindow_debuginfo.cpp` и добавить его в список исходников `CMakeLists.txt`, следуя структуре upstream (файлы `mainWindow/*` перечислены там явно):

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
grep -n "mainWindow/" CMakeLists.txt | head -20
```

- [ ] **Step 2: Перенести тело сборщика**

```bash
sed -n '/^void MainWindow::on_menu_collect_debug_info_triggered/,/^}/p' \
  /tmp/throne-sync/funcs/mainwindow.cpp.ours > /tmp/throne-sync/funcs/debuginfo.cpp
wc -l /tmp/throne-sync/funcs/debuginfo.cpp
```

Функция собирает: системную информацию, сетевую информацию, таблицу маршрутизации, версии приложения и ядер, информацию о пользователе, список запущенных процессов, последние 1500 строк логов ядра, логи приложения, настройки без паролей, сведения о группах и профилях со статистикой.

- [ ] **Step 3: Переписать работу с логами на API upstream**

Наш код читал логи из файла, который писал наш `messageHandler`. Его больше нет. Заменить чтение лог-файла приложения на:

```cpp
    // Логи текущей сессии UI
    const QStringList recent = Logging::RecentLines(1500);
    // Каталог логов и лог прошлой сессии
    const QString logDir = Logging::LogDir();
    const QString prevLog = Logging::PreviousSessionLogPath();
```

Не забыть `#include "include/global/Logger.hpp"`.

- [ ] **Step 4: Повесить на действие upstream**

upstream уже объявил действие в `include/ui/mainwindow.ui:861`. Использовать его вместо заведения своего: слот назвать `on_menu_profile_debug_info_triggered()` (Qt свяжет автоматически по имени) либо подключить явным `connect` в `mainwindow_setup.cpp`. Там же оставить наш шорткат `Ctrl+Shift+D`.

- [ ] **Step 5: Собрать и проверить вручную**

```bash
cmake --build build --target Throne -j 2>&1 | tail -30
```

Затем запустить приложение, нажать `Ctrl+Shift+D` и убедиться, что архив с отладочной информацией создаётся, а внутри есть непустой файл с логами.

- [ ] **Step 6: Коммит**

```bash
git add src/ui/mainWindow/mainwindow_debuginfo.cpp include/ui/mainwindow.h CMakeLists.txt
git commit -m "Добавить сбор отладочной информации по Ctrl+Shift+D на базе нового логгера"
```

---

### Task 10: Пакетные операции с профилями и переводы

**Files:**
- Modify: `src/configs/sub/GroupUpdater.cpp`
- Modify: `res/translations/ru_RU.ts`

**Interfaces:**
- Consumes: настройку `sub_send_hwid` из Task 3.

- [ ] **Step 1: Наложить правки GroupUpdater.cpp (4 хунка)**

```bash
cat /tmp/throne-sync/patches/src_configs_sub_GroupUpdater.cpp.patch
```

Наш патч (+164) добавляет увеличенные лимиты пакетной обработки, пакетное добавление профилей с идентификаторами и передачу HWID. upstream переписал файл на 864 строки — накладывать строго вручную, сверяя, не появилось ли у upstream своей пакетной обработки:

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
grep -n "Batch\|batch" src/configs/sub/GroupUpdater.cpp | head -20
```

Если upstream уже реализовал пакетность — взять его вариант, наш отбросить и записать в список отброшенного.

- [ ] **Step 2: Перенести переводы**

Наш патч добавляет всего 4 строки, upstream переписал 4922. **Не накладывать патч.** Вместо этого добавить недостающие строки заново через Qt-инструменты:

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
cat /tmp/throne-sync/patches/res_translations_ru_RU.ts.patch
bash script/translate.sh 2>&1 | tail -20
```

Затем перевести появившиеся строки наших новых элементов (`Speedtest`, `Ping`, `Ping Test`, `No profiles in current group.`, пункт меню сбора отладочной информации, кнопка «Сброс и перезапуск»).

- [ ] **Step 3: Собрать**

```bash
cmake --build build --target Throne -j 2>&1 | tail -30
```

- [ ] **Step 4: Коммит**

```bash
git add src/configs/sub/GroupUpdater.cpp res/translations/ru_RU.ts
git commit -m "Добавить пакетную обработку профилей и обновить русский перевод"
```

---

### Task 11: Приёмка — сборка, ручная проверка, отчёт о расхождениях

**Files:**
- Create: `docs/superpowers/plans/2026-09-04-upstream-sync-report.md`

**Interfaces:**
- Consumes: результаты всех предыдущих задач.

- [ ] **Step 1: Чистая сборка с нуля**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -20
cmake --build build -j 2>&1 | tail -40
```

Ожидаемо: сборка проходит целиком, включая цель `updater` на macOS.

- [ ] **Step 2: Проверить, что ни один наш файл не потерян**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
BASE=0dcc0e1177ceb0271951fe739c094c7c8547bec2
OURS=7dbc2337441efec88bb43c6fbf1d5708661ce117
echo "=== Файлы, которые мы меняли, и их судьба ==="
for f in $(git diff --name-only $BASE $OURS); do
  if [ -f "$f" ]; then echo "OK       $f"; else echo "ОТСУТСТВУЕТ  $f"; fi
done
```

Ожидаемо `ОТСУТСТВУЕТ` только для `src/ui/mainwindow.cpp` и `src/ui/mainwindow_rpc.cpp` (удалены upstream осознанно, содержимое перенесено в Task 8). Любой другой отсутствующий файл — ошибка, вернуться и разобраться.

- [ ] **Step 3: Ручная проверка ключевых сценариев**

Тест-сьюта в проекте нет, поэтому проверяется руками. Запустить приложение и пройти:

1. Окно открывается, размер и позиция восстанавливаются; колонка `Name` тянется, остальные — по содержимому.
2. Кнопки `Speedtest` и `Ping` на тулбаре работают.
3. `Ctrl+Shift+D` собирает архив с отладочной информацией, логи внутри непустые.
4. Включение и выключение режима VPN; при выключении процесс VPN действительно завершается (проверить `ps`).
5. Выход из приложения не оставляет висящих процессов core/VPN.
6. Ссылка `throne://…` открывается приложением.
7. Дефолты новой конфигурации: тема, язык, `direct_dns = tls://77.88.8.8`, интервал обновления подписки 120 минут.
8. На macOS — запуск апдейтера из меню кнопки обновления.

- [ ] **Step 4: Записать отчёт о том, что отброшено и почему**

Создать `docs/superpowers/plans/2026-09-04-upstream-sync-report.md` со списком:
- отброшено сознательно (наше логирование в `main.cpp` — заменено на `Logging::` upstream);
- отброшено, потому что upstream сделал сам (заполнить по факту находок в Task 3, 5, 7, 10);
- перенесено с изменениями (сборщик debug-info — переведён на новый логгер);
- не переносилось, потому что файл удалён upstream (`script/env_deploy.sh`, если подтвердится).

- [ ] **Step 5: Финальный коммит и публикация ветки**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
git add docs/superpowers/plans/2026-09-04-upstream-sync-report.md
git commit -m "Добавить отчёт о синхронизации с upstream"
git log --oneline 339340e4..HEAD
```

Ожидаемо: ~9 наших коммитов поверх апстримного `339340e4`.

**Ветку не пушить и не мерджить в `devtest` без отдельного согласования с владельцем репозитория.**

---

## Порядок отката

Если что-то пойдёт не так, `origin/devtest` не затронут ни на одном шаге. Достаточно:

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
git checkout devtest
git branch -D sync-upstream-2026-09
```

Тег `1.1.2` продолжает указывать на `7dbc2337` — рабочий релиз всегда доступен.
