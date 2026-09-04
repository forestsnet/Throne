# Политика провайдера: фундамент — реализация

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Разбирать HTTP-заголовки ответа подписки в объект политики, хранить его у группы и применять при активации профиля этой группы.

**Architecture:** Заголовки разбираются в `ProviderPolicy` и сохраняются JSON-ом в новой колонке `provider_policy_json` таблицы `groups`. В рантайме единая точка `ActiveProviderPolicy()` отдаёт политику группы запущенного профиля; на неё будут смотреть остальные подсистемы. Управляющие флаги трёхзначны: отсутствие заголовка — не «false», а «нет мнения».

**Tech Stack:** C++17, Qt 6.10, SQLite через SQLiteCpp, CMake + Ninja, Qt Test.

**Spec:** `docs/superpowers/specs/2026-09-04-provider-policy-design.md`

## Global Constraints

- Ветка: `sync-upstream-2026-09`. `origin/devtest` не трогать.
- Сборка: `export CMAKE_PREFIX_PATH="$(brew --prefix qt)"`, генератор Ninja, заголовок `srslist.h` должен лежать в каталоге сборки.
- Тест-сьюта в проекте нет. Этот план вводит первый — только для чистой логики разбора, за флагом `THRONE_BUILD_TESTS` (по умолчанию OFF), чтобы обычная сборка и CI не изменились.
- `ProviderPolicy.cpp` **не должен зависеть ни на что из проекта, кроме Qt** — иначе тестовая цель потянет половину приложения. Base64 декодировать через `QByteArray::fromBase64`, не через `DecodeB64IfValid` из `Utils.hpp`.
- Сообщения коммитов на русском, без упоминаний Claude/Anthropic/AI и без трейлера `Co-Authored-By`.
- После каждой задачи проект должен собираться. Сломанную сборку не коммитить.
- Отсутствие заголовка **никогда** не означает `false` для пользовательских настроек. Для ограничений интерфейса отсутствие означает «ограничение снято».

## File Structure

**Создаются:**
- `include/configs/sub/ProviderPolicy.hpp` — структура политики и три свободные функции: разбор из заголовков, сериализация, десериализация. Зависимости — только Qt.
- `src/configs/sub/ProviderPolicy.cpp` — их реализация.
- `tests/CMakeLists.txt` — тестовая цель за флагом.
- `tests/ProviderPolicyTest.cpp` — Qt Test на разбор и round-trip.

**Изменяются:**
- `include/database/entities/Group.h` — поле `provider_policy_json`.
- `src/database/GroupsRepo.cpp` — колонка в схеме, миграция, INSERT/SELECT, `groupFromJson`.
- `src/configs/sub/GroupUpdater.cpp` — захват заголовков и сохранение политики в группу.
- `include/ui/mainwindow.h` — метод `applyProviderPolicy` и флаг-страж.
- `src/ui/mainWindow/mainwindow_profile_lifecycle.cpp` — вызов применения в `profile_start`.
- `CMakeLists.txt` — новый исходник и подключение `tests/`.

---

### Task 1: Структура политики и разбор заголовков

**Files:**
- Create: `include/configs/sub/ProviderPolicy.hpp`
- Create: `src/configs/sub/ProviderPolicy.cpp`
- Create: `tests/ProviderPolicyTest.cpp`
- Create: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: ничего.
- Produces: `struct Subscription::ProviderPolicy`; `ProviderPolicy Subscription::ParseProviderPolicy(const QList<QPair<QByteArray, QByteArray>> &headers)`.

- [ ] **Step 1: Написать заголовок с объявлениями**

Создать `include/configs/sub/ProviderPolicy.hpp`:

```cpp
#pragma once

#include <optional>

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

namespace Subscription {
    // Настройки, присланные панелью в HTTP-заголовках ответа подписки.
    // Трёхзначность обязательна: отсутствие заголовка не равно false.
    struct ProviderPolicy {
        int schema = 1;

        // Информационные
        QString title;                    // profile-title
        QString announce;                 // announce
        QString supportUrl;               // support-url
        QString webPageUrl;               // profile-web-page-url
        QString providerId;               // providerid
        qint64  refillDate = 0;           // subscription-refill-date
        int     updateIntervalHours = 0;  // profile-update-interval, 0 = не прислан

        // Управляющие
        std::optional<bool> tunEnable;    // tun-enable
        std::optional<bool> alwaysHwid;   // subscription-always-hwid-enable
        std::optional<bool> autoUpdate;   // subscription-auto-update-enable
        std::optional<bool> dnsFromJson;  // dns-from-json-enable
        std::optional<bool> hideSettings; // hide-settings
        std::optional<bool> hideUrl;      // hide-url
        std::optional<bool> pin;          // subscription-pin
        std::optional<bool> collapse;     // subscriptions-collapse
        std::optional<bool> pingOnOpen;   // subscription-ping-onopen-enabled

        QString perAppProxyList;          // per-app-proxy-list, формат не разбираем
        QJsonObject unknown;              // нераспознанные заголовки, чтобы не терять

        bool isEmpty() const;
    };

    ProviderPolicy ParseProviderPolicy(const QList<QPair<QByteArray, QByteArray>> &headers);
    QString        SerializeProviderPolicy(const ProviderPolicy &policy);
    ProviderPolicy DeserializeProviderPolicy(const QString &json);
}
```

- [ ] **Step 2: Написать падающий тест на разбор**

Создать `tests/ProviderPolicyTest.cpp`:

```cpp
#include <QTest>

#include "include/configs/sub/ProviderPolicy.hpp"

using namespace Subscription;

namespace {
    QList<QPair<QByteArray, QByteArray>> H(std::initializer_list<QPair<QByteArray, QByteArray>> items) {
        return QList<QPair<QByteArray, QByteArray>>(items);
    }
}

class ProviderPolicyTest : public QObject {
    Q_OBJECT
private slots:
    void emptyHeadersGiveEmptyPolicy() {
        const auto p = ParseProviderPolicy({});
        QVERIFY(p.isEmpty());
        QVERIFY(!p.tunEnable.has_value());
    }

    void absentFlagIsNotFalse() {
        const auto p = ParseProviderPolicy(H({{"support-url", "https://example.invalid"}}));
        QVERIFY(!p.tunEnable.has_value());
        QCOMPARE(p.supportUrl, QStringLiteral("https://example.invalid"));
    }

    void booleansParsed() {
        const auto p = ParseProviderPolicy(H({
            {"tun-enable", "true"},
            {"hide-settings", "false"},
            {"subscription-pin", "1"},
            {"no-limit-enabled", "0"},
        }));
        QCOMPARE(p.tunEnable.value(), true);
        QCOMPARE(p.hideSettings.value(), false);
        QCOMPARE(p.pin.value(), true);
    }

    void headerNamesAreCaseInsensitive() {
        const auto p = ParseProviderPolicy(H({{"TUN-Enable", "true"}}));
        QCOMPARE(p.tunEnable.value(), true);
    }

    void base64ValuesDecoded() {
        // "VmxleFZQTiB8IERFVg==" -> "VlexVPN | DEV"
        const auto p = ParseProviderPolicy(H({{"profile-title", "base64:VmxleFZQTiB8IERFVg=="}}));
        QCOMPARE(p.title, QStringLiteral("VlexVPN | DEV"));
    }

    void brokenBase64FallsBackToRaw() {
        const auto p = ParseProviderPolicy(H({{"profile-title", "base64:!!!not-base64!!!"}}));
        QCOMPARE(p.title, QStringLiteral("!!!not-base64!!!"));
    }

    void numericHeadersParsed() {
        const auto p = ParseProviderPolicy(H({
            {"profile-update-interval", "1"},
            {"subscription-refill-date", "1788566400"},
        }));
        QCOMPARE(p.updateIntervalHours, 1);
        QCOMPARE(p.refillDate, static_cast<qint64>(1788566400));
    }

    void nonNumericIntervalIgnored() {
        const auto p = ParseProviderPolicy(H({{"profile-update-interval", "soon"}}));
        QCOMPARE(p.updateIntervalHours, 0);
    }

    void unknownHeadersPreserved() {
        const auto p = ParseProviderPolicy(H({{"x-brand-new-flag", "yes"}}));
        QCOMPARE(p.unknown.value("x-brand-new-flag").toString(), QStringLiteral("yes"));
    }

    void unrelatedHttpHeadersIgnored() {
        const auto p = ParseProviderPolicy(H({
            {"content-type", "application/json"},
            {"etag", "W/\"abc\""},
            {"date", "Fri, 04 Sep 2026 12:20:53 GMT"},
        }));
        QVERIFY(p.isEmpty());
        QVERIFY(p.unknown.isEmpty());
    }
};

QTEST_APPLESS_MAIN(ProviderPolicyTest)
#include "ProviderPolicyTest.moc"
```

Создать `tests/CMakeLists.txt`:

```cmake
find_package(Qt6 REQUIRED COMPONENTS Test)

# Класс теста объявлен прямо в .cpp и включает свой .moc — нужен AUTOMOC.
# В корневом CMakeLists он выставлен точечно на цели Throne, на подкаталог не распространяется.
set(CMAKE_AUTOMOC ON)

qt_add_executable(ProviderPolicyTest
        ProviderPolicyTest.cpp
        ../src/configs/sub/ProviderPolicy.cpp
)
target_include_directories(ProviderPolicyTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(ProviderPolicyTest PRIVATE Qt6::Test)

add_test(NAME ProviderPolicyTest COMMAND ProviderPolicyTest)
```

В конец корневого `CMakeLists.txt`, перед блоком обновлятора:

```cmake
option(THRONE_BUILD_TESTS "Build unit tests" OFF)
if (THRONE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 3: Убедиться, что тест не собирается — реализации ещё нет**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake -B build-test -G Ninja -DTHRONE_BUILD_TESTS=ON 2>&1 | tail -5
cmake --build build-test --target ProviderPolicyTest 2>&1 | tail -20
```

Ожидаемо: ошибка вида `ProviderPolicy.cpp: No such file or directory` либо неразрешённые символы `ParseProviderPolicy`. Это подтверждает, что тест действительно проверяет ещё не написанный код.

- [ ] **Step 4: Реализовать разбор**

Создать `src/configs/sub/ProviderPolicy.cpp`:

```cpp
#include "include/configs/sub/ProviderPolicy.hpp"

#include <QJsonDocument>

namespace Subscription {
    namespace {
        // Значение может прийти как "base64:<...>"; при неудаче декодирования
        // возвращаем исходную строку — подписку из-за этого не отвергаем.
        QString decodeValue(const QByteArray &raw) {
            const QString text = QString::fromUtf8(raw).trimmed();
            if (!text.startsWith("base64:", Qt::CaseInsensitive)) return text;

            const QByteArray payload = text.mid(7).toUtf8();
            // Строгий режим обязателен: по умолчанию декодер молча пропускает
            // недопустимые символы и возвращает мусор со статусом Ok.
            const auto decoded = QByteArray::fromBase64Encoding(
                payload, QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
            if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok) {
                return text.mid(7);
            }
            return QString::fromUtf8(decoded.decoded);
        }

        std::optional<bool> toBool(const QString &value) {
            const QString v = value.trimmed().toLower();
            if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
            if (v == "false" || v == "0" || v == "no" || v == "off") return false;
            return std::nullopt;
        }
    }

    bool ProviderPolicy::isEmpty() const {
        return title.isEmpty() && announce.isEmpty() && supportUrl.isEmpty()
            && webPageUrl.isEmpty() && providerId.isEmpty()
            && refillDate == 0 && updateIntervalHours == 0
            && !tunEnable && !alwaysHwid && !autoUpdate && !dnsFromJson
            && !hideSettings && !hideUrl && !pin && !collapse && !pingOnOpen
            && perAppProxyList.isEmpty() && unknown.isEmpty();
    }

    ProviderPolicy ParseProviderPolicy(const QList<QPair<QByteArray, QByteArray>> &headers) {
        // Заголовки, которые шлёт любой HTTP-сервер: в unknown им не место.
        static const QSet<QString> ignored = {
            "server", "date", "content-type", "content-length", "content-encoding",
            "set-cookie", "expires", "cache-control", "pragma", "etag", "vary",
            "connection", "transfer-encoding", "accept-ranges", "age", "location",
            "cross-origin-opener-policy", "cross-origin-resource-policy",
            "referrer-policy", "access-control-allow-origin", "content-disposition",
            "strict-transport-security", "x-content-type-options", "x-frame-options",
            "subscription-userinfo", // читается отдельно, в group->info
        };

        ProviderPolicy p;
        for (const auto &[nameRaw, valueRaw] : headers) {
            const QString name = QString::fromUtf8(nameRaw).trimmed().toLower();
            const QString value = decodeValue(valueRaw);

            if (name == "profile-title")                        { p.title = value; continue; }
            if (name == "announce")                             { p.announce = value; continue; }
            if (name == "support-url")                          { p.supportUrl = value; continue; }
            if (name == "profile-web-page-url")                 { p.webPageUrl = value; continue; }
            if (name == "providerid")                           { p.providerId = value; continue; }
            if (name == "per-app-proxy-list")                   { p.perAppProxyList = value; continue; }

            if (name == "profile-update-interval") {
                bool ok = false;
                const int hours = value.toInt(&ok);
                if (ok && hours > 0) p.updateIntervalHours = hours;
                continue;
            }
            if (name == "subscription-refill-date") {
                bool ok = false;
                const qint64 ts = value.toLongLong(&ok);
                if (ok && ts > 0) p.refillDate = ts;
                continue;
            }

            if (name == "tun-enable")                          { p.tunEnable = toBool(value); continue; }
            if (name == "subscription-always-hwid-enable")     { p.alwaysHwid = toBool(value); continue; }
            if (name == "subscription-auto-update-enable")     { p.autoUpdate = toBool(value); continue; }
            if (name == "dns-from-json-enable")                { p.dnsFromJson = toBool(value); continue; }
            if (name == "hide-settings")                       { p.hideSettings = toBool(value); continue; }
            if (name == "hide-url")                            { p.hideUrl = toBool(value); continue; }
            if (name == "subscription-pin")                    { p.pin = toBool(value); continue; }
            if (name == "subscriptions-collapse")              { p.collapse = toBool(value); continue; }
            if (name == "subscription-ping-onopen-enabled")    { p.pingOnOpen = toBool(value); continue; }

            if (ignored.contains(name)) continue;
            p.unknown[name] = value;
        }
        return p;
    }
}
```

Добавить в корневой `CMakeLists.txt`, рядом с остальными исходниками `src/configs/sub/`:

```cmake
        src/configs/sub/ProviderPolicy.cpp
```

Также добавить `#include <QSet>` в `ProviderPolicy.cpp`.

- [ ] **Step 5: Прогнать тест — должен пройти**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake -B build-test -G Ninja -DTHRONE_BUILD_TESTS=ON > /dev/null 2>&1
cmake --build build-test --target ProviderPolicyTest 2>&1 | tail -5
./build-test/tests/ProviderPolicyTest
```

Ожидаемо: `Totals: 12 passed, 0 failed, 0 skipped` — Qt Test считает ещё `initTestCase` и `cleanupTestCase`.

- [ ] **Step 6: Убедиться, что обычная сборка не изменилась**

```bash
cmake --build build -j 2>&1 | tail -3
```

Ожидаемо: сборка проходит, тестовая цель в неё не входит (флаг `THRONE_BUILD_TESTS` по умолчанию OFF).

- [ ] **Step 7: Коммит**

```bash
git add include/configs/sub/ProviderPolicy.hpp src/configs/sub/ProviderPolicy.cpp \
        tests/ CMakeLists.txt
git commit -m "Добавить разбор заголовков подписки в объект политики провайдера"
```

---

### Task 2: Сериализация политики

**Files:**
- Modify: `src/configs/sub/ProviderPolicy.cpp`
- Modify: `tests/ProviderPolicyTest.cpp`

**Interfaces:**
- Consumes: `ProviderPolicy` из Task 1.
- Produces: `QString Subscription::SerializeProviderPolicy(const ProviderPolicy &)`, `ProviderPolicy Subscription::DeserializeProviderPolicy(const QString &)`.

- [ ] **Step 1: Написать падающий тест на round-trip**

Добавить в `tests/ProviderPolicyTest.cpp` в секцию `private slots`:

```cpp
    void roundTripPreservesEverything() {
        const auto original = ParseProviderPolicy(H({
            {"profile-title", "base64:VmxleFZQTiB8IERFVg=="},
            {"tun-enable", "true"},
            {"hide-settings", "false"},
            {"profile-update-interval", "3"},
            {"subscription-refill-date", "1788566400"},
            {"providerid", "g7PI7IhM"},
            {"x-brand-new-flag", "yes"},
        }));

        const auto restored = DeserializeProviderPolicy(SerializeProviderPolicy(original));

        QCOMPARE(restored.title, original.title);
        QCOMPARE(restored.tunEnable.value(), true);
        QCOMPARE(restored.hideSettings.value(), false);
        QCOMPARE(restored.updateIntervalHours, 3);
        QCOMPARE(restored.refillDate, static_cast<qint64>(1788566400));
        QCOMPARE(restored.providerId, QStringLiteral("g7PI7IhM"));
        QCOMPARE(restored.unknown.value("x-brand-new-flag").toString(), QStringLiteral("yes"));
    }

    void roundTripKeepsAbsentDistinctFromFalse() {
        ProviderPolicy p;
        p.hideSettings = false;   // прислано и false
        // p.tunEnable не задано вовсе
        const auto restored = DeserializeProviderPolicy(SerializeProviderPolicy(p));
        QVERIFY(restored.hideSettings.has_value());
        QCOMPARE(restored.hideSettings.value(), false);
        QVERIFY(!restored.tunEnable.has_value());
    }

    void brokenJsonGivesEmptyPolicy() {
        const auto p = DeserializeProviderPolicy(QStringLiteral("{ not json"));
        QVERIFY(p.isEmpty());
    }
```

- [ ] **Step 2: Прогнать — должен упасть**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build-test --target ProviderPolicyTest 2>&1 | tail -10
```

Ожидаемо: ошибка компоновки — `SerializeProviderPolicy` и `DeserializeProviderPolicy` объявлены, но не определены.

- [ ] **Step 3: Реализовать сериализацию**

Добавить в `src/configs/sub/ProviderPolicy.cpp`, внутрь `namespace Subscription`:

```cpp
    namespace {
        void putOpt(QJsonObject &o, const char *key, const std::optional<bool> &v) {
            if (v.has_value()) o[key] = v.value();   // отсутствие = ключа нет
        }

        std::optional<bool> getOpt(const QJsonObject &o, const char *key) {
            if (!o.contains(key)) return std::nullopt;
            return o.value(key).toBool();
        }
    }

    QString SerializeProviderPolicy(const ProviderPolicy &policy) {
        QJsonObject o;
        o["schema"] = policy.schema;
        if (!policy.title.isEmpty())       o["title"] = policy.title;
        if (!policy.announce.isEmpty())    o["announce"] = policy.announce;
        if (!policy.supportUrl.isEmpty())  o["supportUrl"] = policy.supportUrl;
        if (!policy.webPageUrl.isEmpty())  o["webPageUrl"] = policy.webPageUrl;
        if (!policy.providerId.isEmpty())  o["providerId"] = policy.providerId;
        if (policy.refillDate != 0)        o["refillDate"] = policy.refillDate;
        if (policy.updateIntervalHours != 0) o["updateIntervalHours"] = policy.updateIntervalHours;
        if (!policy.perAppProxyList.isEmpty()) o["perAppProxyList"] = policy.perAppProxyList;

        putOpt(o, "tunEnable", policy.tunEnable);
        putOpt(o, "alwaysHwid", policy.alwaysHwid);
        putOpt(o, "autoUpdate", policy.autoUpdate);
        putOpt(o, "dnsFromJson", policy.dnsFromJson);
        putOpt(o, "hideSettings", policy.hideSettings);
        putOpt(o, "hideUrl", policy.hideUrl);
        putOpt(o, "pin", policy.pin);
        putOpt(o, "collapse", policy.collapse);
        putOpt(o, "pingOnOpen", policy.pingOnOpen);

        if (!policy.unknown.isEmpty()) o["unknown"] = policy.unknown;

        return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }

    ProviderPolicy DeserializeProviderPolicy(const QString &json) {
        ProviderPolicy p;
        if (json.trimmed().isEmpty()) return p;

        const auto doc = QJsonDocument::fromJson(json.toUtf8());
        if (!doc.isObject()) return p;   // битая строка -> пустая политика, ограничений нет
        const auto o = doc.object();

        p.schema              = o.value("schema").toInt(1);
        p.title               = o.value("title").toString();
        p.announce            = o.value("announce").toString();
        p.supportUrl          = o.value("supportUrl").toString();
        p.webPageUrl          = o.value("webPageUrl").toString();
        p.providerId          = o.value("providerId").toString();
        p.refillDate          = o.value("refillDate").toVariant().toLongLong();
        p.updateIntervalHours = o.value("updateIntervalHours").toInt(0);
        p.perAppProxyList     = o.value("perAppProxyList").toString();

        p.tunEnable    = getOpt(o, "tunEnable");
        p.alwaysHwid   = getOpt(o, "alwaysHwid");
        p.autoUpdate   = getOpt(o, "autoUpdate");
        p.dnsFromJson  = getOpt(o, "dnsFromJson");
        p.hideSettings = getOpt(o, "hideSettings");
        p.hideUrl      = getOpt(o, "hideUrl");
        p.pin          = getOpt(o, "pin");
        p.collapse     = getOpt(o, "collapse");
        p.pingOnOpen   = getOpt(o, "pingOnOpen");

        p.unknown = o.value("unknown").toObject();
        return p;
    }
```

- [ ] **Step 4: Прогнать тест — должен пройти**

```bash
cmake --build build-test --target ProviderPolicyTest 2>&1 | tail -3
./build-test/tests/ProviderPolicyTest
```

Ожидаемо: `Totals: 15 passed, 0 failed, 0 skipped`.

- [ ] **Step 5: Коммит**

```bash
git add src/configs/sub/ProviderPolicy.cpp tests/ProviderPolicyTest.cpp
git commit -m "Добавить сериализацию политики провайдера с сохранением трёхзначности флагов"
```

---

### Task 3: Хранение политики у группы

**Files:**
- Modify: `include/database/entities/Group.h`
- Modify: `src/database/GroupsRepo.cpp:21-45` (схема и миграция), `:88-110` (`groupFromJson`), `:112-156` (`saveToDatabase`), `:158-206` (`loadFromDatabase`)

**Interfaces:**
- Consumes: `SerializeProviderPolicy` / `DeserializeProviderPolicy` из Task 2.
- Produces: поле `Group::provider_policy_json`, переживающее перезапуск.

- [ ] **Step 1: Добавить поле в сущность**

В `include/database/entities/Group.h`, сразу после `QString info = "";`:

```cpp
        QString provider_policy_json = "";   // ProviderPolicy, сериализованная; пусто = политики нет
```

- [ ] **Step 2: Добавить колонку в схему и миграцию**

В `src/database/GroupsRepo.cpp`, в `CREATE TABLE IF NOT EXISTS groups`, после строки `type_sort_by INTEGER NOT NULL DEFAULT 0,`:

```sql
                provider_policy_json TEXT NOT NULL DEFAULT '',
```

Рядом с существующей миграцией `type_sort_by` (строки 43-45) добавить, повторив её приём проверки:

```cpp
        // Migrate existing databases created before provider_policy_json was added.
        if (!groupsColumnExists("provider_policy_json"))
            db.exec("ALTER TABLE groups ADD COLUMN provider_policy_json TEXT NOT NULL DEFAULT ''");
```

Помощник `groupsColumnExists` уже существует в этом файле и используется миграцией `type_sort_by` — заводить свой не нужно.

- [ ] **Step 3: Провести колонку через сохранение**

В `saveToDatabase` добавить `provider_policy_json` в список колонок INSERT, восемнадцатый `?` в VALUES, строку в `ON CONFLICT DO UPDATE SET`:

```sql
                provider_policy_json = excluded.provider_policy_json,
```

и соответствующий аргумент в конец списка привязок, после `group->type_sort_by`:

```cpp
            group->provider_policy_json.toStdString(),
```

Порядок аргументов обязан совпадать с порядком колонок — иначе данные перепутаются молча.

- [ ] **Step 4: Провести колонку через чтение**

В `loadFromDatabase` добавить `provider_policy_json` в SELECT после `type_sort_by`, затем:

```cpp
        json["provider_policy_json"] = QString::fromStdString(query->getColumn(17).getText());
```

В `groupFromJson`, после строки с `test_items_to_show`:

```cpp
        group->provider_policy_json = json["provider_policy_json"].toString();
```

- [ ] **Step 5: Собрать и проверить миграцию на существующей базе**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j 2>&1 | tail -3
```

Затем проверить миграцию на базе, созданной прошлой версией:

```bash
cd build && ./Throne.app/Contents/MacOS/Throne &
sleep 8; kill %1
sqlite3 ./Throne.app/Contents/MacOS/config/throne.db "PRAGMA table_info(groups);" | grep provider_policy
```

Ожидаемо: строка с `provider_policy_json|TEXT`. Если база была создана до правки — колонка должна появиться через `ALTER TABLE`, а не потерять данные.

- [ ] **Step 6: Коммит**

```bash
git add include/database/entities/Group.h src/database/GroupsRepo.cpp
git commit -m "Хранить политику провайдера в группе подписки"
```

---

### Task 4: Захват политики при обновлении подписки

**Files:**
- Modify: `src/configs/sub/GroupUpdater.cpp:249-260` (`fetch`), `:286-291` (вызов)

**Interfaces:**
- Consumes: `ParseProviderPolicy`, `SerializeProviderPolicy`, `Group::provider_policy_json`.
- Produces: заполненная политика в группе после каждого обновления подписки.

- [ ] **Step 1: Расширить fetch, чтобы он отдавал политику**

В `src/configs/sub/GroupUpdater.cpp` добавить включение:

```cpp
#include "include/configs/sub/ProviderPolicy.hpp"
```

Изменить сигнатуру `fetch` — добавить пятый параметр:

```cpp
    bool GroupUpdater::fetch(const QString &url, const QString &name, QByteArray &body,
                             QString &userInfo, QString &policyJson) {
```

Соответственно поправить объявление в `include/configs/sub/GroupUpdater.hpp`.

Сразу после строки, читающей `Subscription-UserInfo` (строка 257):

```cpp
        const auto policy = Subscription::ParseProviderPolicy(resp.header);
        policyJson = policy.isEmpty() ? QString() : Subscription::SerializeProviderPolicy(policy);
```

- [ ] **Step 2: Сохранить политику в группу**

В вызывающем коде около строки 286 заменить:

```cpp
        QString userInfo;
        if (!fetch(group->url.trimmed(), group->name, body, userInfo)) return;
```

на:

```cpp
        QString userInfo;
        QString policyJson;
        if (!fetch(group->url.trimmed(), group->name, body, userInfo, policyJson)) return;
```

и после `group->info = userInfo;`:

```cpp
        // Пустая политика не затирает сохранённую: панель могла временно не прислать заголовки.
        if (!policyJson.isEmpty()) group->provider_policy_json = policyJson;
```

Во втором месте вызова `fetch` (строка 187, импорт по ссылке без группы) передать пустую переменную-заглушку: политику там сохранять некуда, группы ещё нет.

- [ ] **Step 3: Собрать**

```bash
cmake --build build -j 2>&1 | tail -3
```

Ожидаемо: сборка проходит.

- [ ] **Step 4: Проверить на настоящей подписке**

Поднять локальную заглушку, отдающую нужные заголовки:

```bash
mkdir -p /tmp/policy-stub && cd /tmp/policy-stub
cat > stub.py <<'EOF'
from http.server import BaseHTTPRequestHandler, HTTPServer
BODY = b"vless://00000000-0000-0000-0000-000000000000@127.0.0.1:443?security=tls#stub"
import base64
class H(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("content-type", "text/plain; charset=utf-8")
        self.send_header("profile-title", "base64:" + base64.b64encode("Заглушка".encode()).decode())
        self.send_header("tun-enable", "true")
        self.send_header("hide-settings", "false")
        self.send_header("profile-update-interval", "3")
        self.send_header("x-brand-new-flag", "yes")
        self.end_headers()
        self.wfile.write(base64.b64encode(BODY))
    def log_message(self, *a): pass
HTTPServer(("127.0.0.1", 8899), H).serve_forever()
EOF
python3 stub.py &
```

Запустить Throne, добавить группу с URL `http://127.0.0.1:8899/sub`, обновить её, затем:

```bash
sqlite3 <путь>/throne.db "SELECT name, provider_policy_json FROM groups;"
```

Ожидаемо: в `provider_policy_json` виден JSON с `"tunEnable":true`, `"hideSettings":false`, `"updateIntervalHours":3`, `"title":"Заглушка"` и `"unknown":{"x-brand-new-flag":"yes"}`.

**Подводный камень стенда.** Планировщик автообновления подписок глобальный: он смотрит на `settings.sub_auto_update_last`, а не только на `groups.sub_last_update`. Чтобы заставить обновление повториться, сбрасывать надо оба:

```bash
sqlite3 <путь>/throne.db "UPDATE settings SET value='0' WHERE key='sub_auto_update_last';"
sqlite3 <путь>/throne.db "UPDATE groups SET sub_last_update=0 WHERE id=<gid>;"
```

Без сброса глобального счётчика приложение молча не пойдёт за подпиской, и проверка покажет старую политику — это выглядит как ошибка в коде, но ею не является.

Отдельно проверить, что отсутствие заголовка не превращается в `false`: перезапустить заглушку с `SEND_TUN=0`, сбросить оба счётчика, обновить. В `provider_policy_json` ключа `tunEnable` быть не должно вовсе, а `hideSettings` должен остаться `false`.

Остановить заглушку: `pkill -9 -f stub.py`.

- [ ] **Step 5: Коммит**

```bash
git add src/configs/sub/GroupUpdater.cpp include/configs/sub/GroupUpdater.hpp
git commit -m "Сохранять политику провайдера при обновлении подписки"
```

---

### Task 5: Активная политика и её применение

**Files:**
- Modify: `include/configs/sub/ProviderPolicy.hpp`, `src/configs/sub/ProviderPolicy.cpp`
- Modify: `include/ui/mainwindow.h`
- Modify: `src/ui/mainWindow/mainwindow_profile_lifecycle.cpp:174`

**Interfaces:**
- Consumes: всё из Task 1-4.
- Produces: `const ProviderPolicy &Subscription::ActiveProviderPolicy()` — точка, на которую будут смотреть подсистемы 2-4; `void MainWindow::applyProviderPolicy(int gid)`.

- [ ] **Step 1: Объявить и реализовать хранилище активной политики**

В `include/configs/sub/ProviderPolicy.hpp`, в конец namespace:

```cpp
    // Политика группы, которой принадлежит запущенный профиль.
    // Пустая, если профиль не запущен: остановка профиля снимает все ограничения.
    const ProviderPolicy &ActiveProviderPolicy();
    void SetActiveProviderPolicy(const ProviderPolicy &policy);
    void ClearActiveProviderPolicy();
```

В `src/configs/sub/ProviderPolicy.cpp`:

```cpp
    namespace {
        ProviderPolicy g_active;
    }

    const ProviderPolicy &ActiveProviderPolicy() { return g_active; }
    void SetActiveProviderPolicy(const ProviderPolicy &policy) { g_active = policy; }
    void ClearActiveProviderPolicy() { g_active = ProviderPolicy{}; }
```

- [ ] **Step 2: Объявить применение и флаг-страж**

В `include/ui/mainwindow.h`, рядом с `bool StopVPNProcess();`:

```cpp
    void applyProviderPolicy(int gid);
```

В приватную секцию, рядом с другими членами:

```cpp
    bool m_applyingProviderPolicy = false;
```

- [ ] **Step 3: Реализовать применение с защитой от рекурсии**

В `src/ui/mainWindow/mainwindow_profile_lifecycle.cpp` добавить включение:

```cpp
#include "include/configs/sub/ProviderPolicy.hpp"
```

и реализацию:

```cpp
void MainWindow::applyProviderPolicy(int gid) {
    // set_spmode_vpn заканчивается вызовом profile_start, поэтому без стража
    // применение политики зациклится: политика включает TUN, TUN перезапускает профиль.
    if (m_applyingProviderPolicy) return;

    const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
    if (!group) {
        Subscription::ClearActiveProviderPolicy();
        return;
    }

    const auto policy = Subscription::DeserializeProviderPolicy(group->provider_policy_json);
    Subscription::SetActiveProviderPolicy(policy);
    if (policy.isEmpty()) return;

    m_applyingProviderPolicy = true;

    if (policy.alwaysHwid.has_value()) {
        Configs::dataManager->settingsRepo->sub_send_hwid = policy.alwaysHwid.value();
    }
    if (policy.autoUpdate.has_value()) {
        group->skip_auto_update = !policy.autoUpdate.value();
        Configs::dataManager->groupsRepo->Save(group);
    }
    if (policy.updateIntervalHours > 0) {
        Configs::dataManager->settingsRepo->sub_auto_update = policy.updateIntervalHours * 60;
    }
    Configs::dataManager->settingsRepo->Save();

    // Последним: перезапускает профиль, поэтому все прочие настройки уже должны быть записаны.
    if (policy.tunEnable.has_value()
        && policy.tunEnable.value() != Configs::dataManager->settingsRepo->spmode_vpn) {
        set_spmode_vpn(policy.tunEnable.value(), true);
    }

    m_applyingProviderPolicy = false;
}
```

- [ ] **Step 4: Вызвать при активации профиля**

В `profile_start`, сразу после блока проверки конфликтующих процессов и до `#ifdef Q_OS_LINUX`:

```cpp
    if (const auto policyEnt = Configs::dataManager->profilesRepo->GetProfile(_id)) {
        applyProviderPolicy(policyEnt->gid);
    }
```

В `profile_stop` (тот же файл, `src/ui/mainWindow/mainwindow_profile_lifecycle.cpp:452`) — в самое начало тела:

```cpp
    Subscription::ClearActiveProviderPolicy();
```

Включение `ProviderPolicy.hpp` уже добавлено выше в этом же файле — второй раз не нужно.

- [ ] **Step 5: Собрать**

```bash
cmake --build build -j 2>&1 | tail -3
```

Ожидаемо: сборка проходит без ошибок.

- [ ] **Step 6: Проверить применение вручную**

Поднять заглушку из Task 4 Step 4 с `tun-enable: true`. Затем:

1. Убедиться, что TUN выключен.
2. Обновить подписку заглушки.
3. Запустить профиль из этой группы.
4. Убедиться, что TUN включился, а профиль перезапустился ровно один раз — в логе `config/logs/throne.log` не должно быть повторяющихся циклов запуска.
5. Остановить профиль и убедиться, что дальнейших изменений настроек нет.

Затем перезапустить заглушку **без** заголовка `tun-enable` и снова обновить подписку: TUN должен остаться в текущем состоянии, а не выключиться. Это проверка того, что отсутствие заголовка не равно `false`.

- [ ] **Step 7: Коммит**

```bash
git add include/configs/sub/ProviderPolicy.hpp src/configs/sub/ProviderPolicy.cpp \
        include/ui/mainwindow.h src/ui/mainWindow/mainwindow_profile_lifecycle.cpp
git commit -m "Применять политику провайдера при активации профиля группы"
```

---

### Task 6: Показ информационных полей и приёмка

**Files:**
- Modify: `src/ui/group/dialog_edit_group.cpp`
- Modify: `src/ui/mainWindow/mainwindow_debuginfo.cpp`

**Interfaces:**
- Consumes: `Group::provider_policy_json`, `DeserializeProviderPolicy`.

- [ ] **Step 1: Показать сведения провайдера в свойствах группы**

В `src/ui/group/dialog_edit_group.cpp`, после `ui->url->setText(ent->url);`:

```cpp
    const auto policy = Subscription::DeserializeProviderPolicy(ent->provider_policy_json);
    if (!policy.isEmpty()) {
        QStringList lines;
        if (!policy.announce.isEmpty())   lines << policy.announce;
        if (!policy.supportUrl.isEmpty()) lines << tr("Support: %1").arg(policy.supportUrl);
        if (policy.refillDate > 0) {
            lines << tr("Refill date: %1")
                        .arg(QDateTime::fromSecsSinceEpoch(policy.refillDate).toString(Qt::ISODate));
        }
        if (!lines.isEmpty()) ui->url->setToolTip(lines.join("\n"));
    }
```

Добавить включения `ProviderPolicy.hpp` и `<QDateTime>`.

- [ ] **Step 2: Добавить политику в отладочный архив**

В `src/ui/mainWindow/mainwindow_debuginfo.cpp`, в секцию 9 (сведения о группах), внутрь цикла по группам:

```cpp
                if (!group->provider_policy_json.isEmpty()) {
                    stream << "  Provider policy: " << group->provider_policy_json << "\n";
                }
```

Политика не содержит учётных данных — только настройки, поэтому в архив попадает целиком.

- [ ] **Step 3: Собрать**

```bash
cmake --build build -j 2>&1 | tail -3
```

- [ ] **Step 4: Полная приёмка**

Прогнать все проверки разом:

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
rm -rf build-accept && cmake -B build-accept -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
cp build-baseline/srslist.h build-accept/srslist.h
cmake --build build-accept -j 2>&1 | tail -3
cmake --build build-test --target ProviderPolicyTest 2>&1 | tail -2 && ./build-test/tests/ProviderPolicyTest
```

Ожидаемо: чистая сборка без ошибок, 15 тестов пройдено.

Затем вручную, с заглушкой из Task 4:

1. `profile-title` переименовывает группу при обновлении.
2. `tun-enable: true` включает TUN при запуске профиля этой группы.
3. Отсутствие `tun-enable` ничего не меняет.
4. `announce` и `support-url` видны в подсказке к полю URL в свойствах группы.
5. `Ctrl+Shift+D` собирает архив, в нём видна строка `Provider policy`.
6. Неизвестный заголовок `x-brand-new-flag` сохранён в `unknown` и виден в архиве.

- [ ] **Step 5: Коммит**

```bash
git add src/ui/group/dialog_edit_group.cpp src/ui/mainWindow/mainwindow_debuginfo.cpp
git commit -m "Показывать сведения провайдера в свойствах группы и в отладочном архиве"
```

---

## Что этот план сознательно не делает

Подсистемы 2-4 из спеки (ограничения интерфейса, DNS из JSON, per-app прокси) сюда не входят. Все три подключаются к `ActiveProviderPolicy()`, которого до этого плана не существует, поэтому их планы пишутся после того, как этот интерфейс станет реальным кодом, а не предположением.

Порядок дальнейших заходов свободный — между собой подсистемы 2-4 не зависят.
