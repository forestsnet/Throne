# FSNT Client, прирост 1: каркас и тема — реализация

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Приложение открывается в новом окне FSNT Client — шапка и две панели, оформленные по теме, — и умеет переключиться в расширенный режим и обратно.

**Architecture:** Новое окно живёт в том же бинаре рядом с существующим `MainWindow`. Выбор окна происходит в единственной точке создания — `UI_InitMainWindow()`. Оформление берётся из существующих `ThemeTokens`, собственной палитры не заводим. Панели в этом приросте пустые: их наполняют приросты 2-4.

**Tech Stack:** C++17, Qt 6 Widgets, QSS, SQLite через SQLiteCpp, CMake + Ninja, Qt Test.

**Spec:** `docs/superpowers/specs/2026-09-04-fsnt-client-ui-design.md`

## Global Constraints

- Ветка: `sync-upstream-2026-09`. `origin/devtest` не трогать.
- Сборка: `export CMAKE_PREFIX_PATH="$(brew --prefix qt)"`, генератор Ninja, `srslist.h` в каталоге сборки, `ThroneCore` в бандле для запуска.
- Тесты — за флагом `THRONE_BUILD_TESTS` (по умолчанию OFF), как заведено для `ProviderPolicyTest`.
- **Простой режим — дефолт только для новых установок.** У существующих интерфейс не меняется после обновления.
- **Переключение режима — через перезапуск приложения**, не на лету: два окна подписались бы на одни сигналы ядра дважды.
- Ни один новый файл не должен вырасти в размер `mainwindow_setup.cpp`. Один файл — один виджет.
- Собственной палитры не заводим: цвета берутся из `themeManager()->tokens`.
- Сообщения коммитов на русском, без упоминаний Claude/Anthropic/AI и без трейлера `Co-Authored-By`.
- В этом приросте панели пустые. Список серверов, кнопка подключения и карточка подписки — следующие приросты; не забегать вперёд.

## File Structure

**Создаются:**

| Файл | Ответственность |
|---|---|
| `include/ui/fsnt/UiMode.hpp`, `src/ui/fsnt/UiMode.cpp` | Перечисление режимов и чистая функция выбора режима при старте |
| `include/ui/fsnt/FsntWindow.h`, `src/ui/fsnt/FsntWindow.cpp` | Окно: шапка, две панели, переход в расширенный режим |
| `include/ui/fsnt/FsntTheme.hpp`, `src/ui/fsnt/FsntTheme.cpp` | Сборка QSS из `ThemeTokens` |
| `tests/UiModeTest.cpp` | Тесты выбора режима |

**Изменяются:**

| Файл | Что |
|---|---|
| `include/database/SettingsRepo.h` | Поле `ui_mode` |
| `src/database/SettingsRepo.cpp` | Регистрация `ui_mode` |
| `src/ui/mainWindow/mainwindow_setup.cpp:74` | Развилка в `UI_InitMainWindow` |
| `CMakeLists.txt` | Новые исходники |
| `tests/CMakeLists.txt` | Тестовая цель |
| `res/translations/ru_RU.ts` | Строки нового окна |

---

### Task 1: Режим интерфейса и правило выбора

**Files:**
- Create: `include/ui/fsnt/UiMode.hpp`, `src/ui/fsnt/UiMode.cpp`, `tests/UiModeTest.cpp`
- Modify: `include/database/SettingsRepo.h`, `src/database/SettingsRepo.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: ничего.
- Produces: `enum class Fsnt::UiMode { Advanced = 0, Simple = 1 }`; `Fsnt::UiMode Fsnt::ResolveInitialUiMode(int storedValue, bool databaseHasContent)`; поле `SettingsRepo::ui_mode`.

- [ ] **Step 1: Написать заголовок**

Создать `include/ui/fsnt/UiMode.hpp`:

```cpp
#pragma once

namespace Fsnt {
    enum class UiMode {
        Advanced = 0,   // прежнее окно Throne
        Simple = 1,     // FSNT Client
    };

    // storedValue: значение settingsRepo->ui_mode; -1 означает "никогда не задавалось".
    // databaseHasContent: в базе уже есть группы или профили, то есть установка не новая.
    //
    // Новая установка получает простой режим. Существующая остаётся в расширенном:
    // интерфейс не должен смениться под ногами после обновления.
    UiMode ResolveInitialUiMode(int storedValue, bool databaseHasContent);
}
```

- [ ] **Step 2: Написать падающий тест**

Создать `tests/UiModeTest.cpp`:

```cpp
#include <QTest>

#include "include/ui/fsnt/UiMode.hpp"

using namespace Fsnt;

class UiModeTest : public QObject {
    Q_OBJECT
private slots:
    void freshInstallGetsSimple() {
        QCOMPARE(ResolveInitialUiMode(-1, false), UiMode::Simple);
    }

    void existingInstallStaysAdvanced() {
        QCOMPARE(ResolveInitialUiMode(-1, true), UiMode::Advanced);
    }

    void storedChoiceWins() {
        QCOMPARE(ResolveInitialUiMode(0, false), UiMode::Advanced);
        QCOMPARE(ResolveInitialUiMode(1, true), UiMode::Simple);
    }

    void garbageValueFallsBackToStoredRules() {
        // Значение вне диапазона трактуем как незаданное, а не как режим 0.
        QCOMPARE(ResolveInitialUiMode(42, false), UiMode::Simple);
        QCOMPARE(ResolveInitialUiMode(42, true), UiMode::Advanced);
        QCOMPARE(ResolveInitialUiMode(-7, false), UiMode::Simple);
    }
};

QTEST_APPLESS_MAIN(UiModeTest)
#include "UiModeTest.moc"
```

Добавить цель в `tests/CMakeLists.txt`, следом за существующей:

```cmake
qt_add_executable(UiModeTest
        UiModeTest.cpp
        ../src/ui/fsnt/UiMode.cpp
)
target_include_directories(UiModeTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(UiModeTest PRIVATE Qt6::Test)

add_test(NAME UiModeTest COMMAND UiModeTest)
```

- [ ] **Step 3: Прогнать — должен не собраться**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake -B build-test -G Ninja -DTHRONE_BUILD_TESTS=ON 2>&1 | tail -5
```

Ожидаемо: ошибка вида `Cannot find source file: ../src/ui/fsnt/UiMode.cpp`. Это подтверждает, что тест проверяет ещё не написанный код.

- [ ] **Step 4: Реализовать**

Создать `src/ui/fsnt/UiMode.cpp`:

```cpp
#include "include/ui/fsnt/UiMode.hpp"

namespace Fsnt {
    UiMode ResolveInitialUiMode(int storedValue, bool databaseHasContent) {
        if (storedValue == static_cast<int>(UiMode::Advanced)) return UiMode::Advanced;
        if (storedValue == static_cast<int>(UiMode::Simple)) return UiMode::Simple;

        // Не задано или мусор: новая установка получает простой режим,
        // существующая остаётся в расширенном.
        return databaseHasContent ? UiMode::Advanced : UiMode::Simple;
    }
}
```

- [ ] **Step 5: Прогнать тест — должен пройти**

```bash
cmake -B build-test -G Ninja -DTHRONE_BUILD_TESTS=ON > /dev/null 2>&1
cmake --build build-test --target UiModeTest 2>&1 | tail -3
./build-test/tests/UiModeTest
```

Ожидаемо: `Totals: 6 passed, 0 failed, 0 skipped` — четыре метода плюс `initTestCase` и `cleanupTestCase`.

- [ ] **Step 6: Добавить настройку**

В `include/database/SettingsRepo.h`, рядом с `theme`:

```cpp
        int ui_mode = -1;   // -1 = не задан; см. Fsnt::ResolveInitialUiMode
```

В `src/database/SettingsRepo.cpp`, в карту целочисленных настроек рядом с `{"language", &language},`:

```cpp
            {"ui_mode",                &ui_mode},
```

Зарегистрировать исходник в корневом `CMakeLists.txt`, рядом с остальными файлами `src/ui/`:

```cmake
        src/ui/fsnt/UiMode.cpp
        include/ui/fsnt/UiMode.hpp
```

- [ ] **Step 7: Собрать основное приложение**

```bash
cmake --build build -j 2>&1 | tail -3
```

Ожидаемо: сборка проходит, ошибок нет.

- [ ] **Step 8: Коммит**

```bash
git add include/ui/fsnt/UiMode.hpp src/ui/fsnt/UiMode.cpp tests/ \
        include/database/SettingsRepo.h src/database/SettingsRepo.cpp CMakeLists.txt
git commit -m "Добавить режим интерфейса и правило его выбора при первом запуске"
```

---

### Task 2: Тема нового окна

**Files:**
- Create: `include/ui/fsnt/FsntTheme.hpp`, `src/ui/fsnt/FsntTheme.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ThemeTokens` и `themeManager()` из `include/ui/setting/ThemeManager.hpp`.
- Produces: `QString Fsnt::BuildStyleSheet(const ThemeTokens &tokens)`.

- [ ] **Step 1: Написать заголовок**

Создать `include/ui/fsnt/FsntTheme.hpp`:

```cpp
#pragma once

#include <QString>

struct ThemeTokens;

namespace Fsnt {
    // Собирает QSS для окна FSNT Client из палитры приложения.
    // Своей палитры не заводим: цвета обязаны совпадать с расширенным режимом.
    QString BuildStyleSheet(const ThemeTokens &tokens);

    // Радиусы и отступы в одном месте, чтобы карточки не разъезжались между виджетами.
    inline constexpr int kCardRadius = 10;
    inline constexpr int kRowRadius = 8;
    inline constexpr int kPanelPadding = 12;
}
```

- [ ] **Step 2: Реализовать**

Создать `src/ui/fsnt/FsntTheme.cpp`:

```cpp
#include "include/ui/fsnt/FsntTheme.hpp"

#include "include/ui/setting/ThemeManager.hpp"

namespace Fsnt {
    QString BuildStyleSheet(const ThemeTokens &t) {
        const auto c = [](const QColor &color) { return color.name(QColor::HexRgb); };

        return QString(R"(
            QWidget#fsntRoot { background: %1; color: %2; }

            QWidget#fsntHeader {
                background: %1;
                border-bottom: 1px solid %3;
            }
            QLabel#fsntTitle { color: %2; font-size: 13px; }
            QLabel#fsntLogo {
                background: %4;
                color: %5;
                border-radius: 6px;
                font-size: 11px;
                padding: 3px 5px;
            }

            QWidget#fsntServerPanel { border-right: 1px solid %3; }

            QToolButton#fsntIconButton {
                border: none;
                border-radius: %6px;
                padding: 4px;
            }
            QToolButton#fsntIconButton:hover { background: %7; }

            QLabel#fsntPlaceholder { color: %8; font-size: 13px; }
        )")
            .arg(c(t.surface),        // 1
                 c(t.onSurface),      // 2
                 c(t.borderSubtle),   // 3
                 c(t.accent),         // 4
                 c(t.onAccent),       // 5
                 QString::number(kRowRadius),  // 6
                 c(t.hoverFill),      // 7
                 c(t.muted));         // 8
    }
}
```

Зарегистрировать в корневом `CMakeLists.txt` рядом с `UiMode.cpp`:

```cmake
        src/ui/fsnt/FsntTheme.cpp
        include/ui/fsnt/FsntTheme.hpp
```

- [ ] **Step 3: Собрать**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
cmake --build build -j 2>&1 | tail -3
```

Ожидаемо: сборка проходит. Визуально проверять нечего — стиль применит Task 3.

- [ ] **Step 4: Коммит**

```bash
git add include/ui/fsnt/FsntTheme.hpp src/ui/fsnt/FsntTheme.cpp CMakeLists.txt
git commit -m "Добавить оформление окна FSNT Client поверх палитры приложения"
```

---

### Task 3: Каркас окна

**Files:**
- Create: `include/ui/fsnt/FsntWindow.h`, `src/ui/fsnt/FsntWindow.cpp`
- Modify: `CMakeLists.txt`, `res/translations/ru_RU.ts`

**Interfaces:**
- Consumes: `Fsnt::BuildStyleSheet` из Task 2, `themeManager()`.
- Produces: класс `FsntWindow` с публичным конструктором и защищёнными панелями `serverPanel()` и `sidePanel()` — их наполняют следующие приросты.

- [ ] **Step 1: Написать заголовок**

Создать `include/ui/fsnt/FsntWindow.h`:

```cpp
#pragma once

#include <QMainWindow>

class QVBoxLayout;

// Потребительское окно FSNT Client. В этом приросте панели пустые:
// список серверов, кнопку подключения и карточку подписки добавляют
// следующие приросты, каждый в своём файле.
class FsntWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit FsntWindow(QWidget *parent = nullptr);

protected:
    // Точки расширения для следующих приростов.
    QVBoxLayout *serverPanelLayout() const { return m_serverLayout; }
    QVBoxLayout *sidePanelLayout() const { return m_sideLayout; }

private:
    void buildHeader(QVBoxLayout *root);
    void buildPanels(QVBoxLayout *root);
    void applyTheme();
    void switchToAdvancedMode();

    QVBoxLayout *m_serverLayout = nullptr;
    QVBoxLayout *m_sideLayout = nullptr;
};
```

- [ ] **Step 2: Реализовать**

Создать `src/ui/fsnt/FsntWindow.cpp`:

```cpp
#include "include/ui/fsnt/FsntWindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "include/database/SettingsRepo.h"
#include "include/global/Configs.hpp"
#include "include/ui/fsnt/FsntTheme.hpp"
#include "include/ui/fsnt/UiMode.hpp"
#include "include/ui/setting/ThemeManager.hpp"

FsntWindow::FsntWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("FSNT Client");
    resize(880, 560);
    setMinimumSize(760, 500);

    auto *central = new QWidget(this);
    central->setObjectName("fsntRoot");
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildHeader(root);
    buildPanels(root);

    applyTheme();
    connect(themeManager(), &ThemeManager::themeChanged, this, [this] { applyTheme(); });
}

void FsntWindow::buildHeader(QVBoxLayout *root) {
    auto *header = new QWidget(this);
    header->setObjectName("fsntHeader");
    header->setFixedHeight(44);

    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(Fsnt::kPanelPadding, 0, Fsnt::kPanelPadding, 0);
    row->setSpacing(8);

    auto *logo = new QLabel("FN", header);
    logo->setObjectName("fsntLogo");
    row->addWidget(logo);

    auto *title = new QLabel("FSNT Client", header);
    title->setObjectName("fsntTitle");
    row->addWidget(title);

    row->addStretch();

    auto *advanced = new QToolButton(header);
    advanced->setObjectName("fsntIconButton");
    advanced->setText(tr("Advanced mode"));
    advanced->setToolTip(tr("Switch to the advanced interface"));
    connect(advanced, &QToolButton::clicked, this, &FsntWindow::switchToAdvancedMode);
    row->addWidget(advanced);

    root->addWidget(header);
}

void FsntWindow::buildPanels(QVBoxLayout *root) {
    auto *body = new QWidget(this);
    auto *columns = new QHBoxLayout(body);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(0);

    auto *serverPanel = new QWidget(body);
    serverPanel->setObjectName("fsntServerPanel");
    m_serverLayout = new QVBoxLayout(serverPanel);
    m_serverLayout->setContentsMargins(Fsnt::kPanelPadding, Fsnt::kPanelPadding,
                                       Fsnt::kPanelPadding, Fsnt::kPanelPadding);

    auto *serverStub = new QLabel(tr("Server list appears here"), serverPanel);
    serverStub->setObjectName("fsntPlaceholder");
    serverStub->setAlignment(Qt::AlignCenter);
    m_serverLayout->addWidget(serverStub);

    auto *sidePanel = new QWidget(body);
    m_sideLayout = new QVBoxLayout(sidePanel);
    m_sideLayout->setContentsMargins(Fsnt::kPanelPadding, Fsnt::kPanelPadding,
                                     Fsnt::kPanelPadding, Fsnt::kPanelPadding);

    auto *sideStub = new QLabel(tr("Connection and subscription appear here"), sidePanel);
    sideStub->setObjectName("fsntPlaceholder");
    sideStub->setAlignment(Qt::AlignCenter);
    m_sideLayout->addWidget(sideStub);

    columns->addWidget(serverPanel, 105);
    columns->addWidget(sidePanel, 100);

    root->addWidget(body, 1);
}

void FsntWindow::applyTheme() {
    setStyleSheet(Fsnt::BuildStyleSheet(themeManager()->tokens));
}

void FsntWindow::switchToAdvancedMode() {
    const auto answer = QMessageBox::question(
        this, tr("Advanced mode"),
        tr("The application will restart in the advanced interface. Continue?"));
    if (answer != QMessageBox::StandardButton::Yes) return;

    Configs::dataManager->settingsRepo->ui_mode = static_cast<int>(Fsnt::UiMode::Advanced);
    Configs::dataManager->settingsRepo->Save();

    // Перезапуск, а не подмена окна: два окна подписались бы на одни сигналы ядра дважды.
    QApplication::quit();
}
```

Добавить `#include <QApplication>` к списку включений.

Зарегистрировать в корневом `CMakeLists.txt`:

```cmake
        src/ui/fsnt/FsntWindow.cpp
        include/ui/fsnt/FsntWindow.h
```

- [ ] **Step 3: Собрать**

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
cmake --build build -j 2>&1 | tail -3
```

Ожидаемо: сборка проходит. Окно ещё не открывается — развилку ставит Task 4.

- [ ] **Step 4: Добавить переводы**

В `res/translations/ru_RU.ts` завести контекст `FsntWindow` перед закрывающим `</TS>`:

```xml
<context>
    <name>FsntWindow</name>
    <message>
        <source>Advanced mode</source>
        <translation>Расширенный режим</translation>
    </message>
    <message>
        <source>Switch to the advanced interface</source>
        <translation>Переключиться на расширенный интерфейс</translation>
    </message>
    <message>
        <source>The application will restart in the advanced interface. Continue?</source>
        <translation>Приложение перезапустится в расширенном интерфейсе. Продолжить?</translation>
    </message>
    <message>
        <source>Server list appears here</source>
        <translation>Здесь появится список серверов</translation>
    </message>
    <message>
        <source>Connection and subscription appear here</source>
        <translation>Здесь появятся подключение и подписка</translation>
    </message>
</context>
```

Проверить, что файл остался валидным XML:

```bash
python3 -c "import xml.dom.minidom; xml.dom.minidom.parse('res/translations/ru_RU.ts'); print('XML валиден')"
```

- [ ] **Step 5: Коммит**

```bash
git add include/ui/fsnt/FsntWindow.h src/ui/fsnt/FsntWindow.cpp CMakeLists.txt res/translations/ru_RU.ts
git commit -m "Добавить каркас окна FSNT Client с шапкой и двумя панелями"
```

---

### Task 4: Развилка запуска и возврат из расширенного режима

**Files:**
- Modify: `src/ui/mainWindow/mainwindow_setup.cpp:74`, `include/ui/mainwindow.h`

**Interfaces:**
- Consumes: `Fsnt::ResolveInitialUiMode`, `FsntWindow`.
- Produces: приложение стартует в выбранном режиме; из расширенного режима есть возврат в простой.

- [ ] **Step 1: Поставить развилку**

В `src/ui/mainWindow/mainwindow_setup.cpp` добавить включения:

```cpp
#include "include/ui/fsnt/FsntWindow.h"
#include "include/ui/fsnt/UiMode.hpp"
```

Заменить тело `UI_InitMainWindow` (строки 74-76):

```cpp
void UI_InitMainWindow() {
    auto &settings = Configs::dataManager->settingsRepo;

    // Установка считается существующей, если в базе уже есть хотя бы одна группа
    // с профилями: свежая база содержит только пустую группу по умолчанию.
    bool hasContent = false;
    for (int gid : Configs::dataManager->groupsRepo->GetAllGroupIds()) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (group && !group->Profiles().isEmpty()) { hasContent = true; break; }
    }

    const auto mode = Fsnt::ResolveInitialUiMode(settings->ui_mode, hasContent);
    if (settings->ui_mode != static_cast<int>(mode)) {
        settings->ui_mode = static_cast<int>(mode);
        settings->Save();
    }

    if (mode == Fsnt::UiMode::Simple) {
        new FsntWindow;
    } else {
        mainwindow = new MainWindow;
    }
}
```

`MainWindow` показывает себя сам в конце конструктора (`mainwindow_setup.cpp:1204`: `if (!Configs::dataManager->settingsRepo->flag_tray) show();`). `FsntWindow` обязан вести себя так же, иначе при запуске окна не будет видно. Добавить в конец его конструктора:

```cpp
    // Как и MainWindow: при запуске в трей окно не показываем.
    if (!Configs::dataManager->settingsRepo->flag_tray) show();
```

- [ ] **Step 2: Собрать и проверить оба режима**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j 2>&1 | tail -3
```

Проверка на чистой базе (простой режим):

```bash
cd build && rm -rf Throne.app/Contents/MacOS/config
./Throne.app/Contents/MacOS/Throne &
sleep 10 && kill %1
sqlite3 Throne.app/Contents/MacOS/config/throne.db "SELECT value FROM settings WHERE key='ui_mode';"
```

Ожидаемо: `1` — новая установка получила простой режим.

Проверка на «существующей» базе (расширенный режим):

```bash
rm -rf Throne.app/Contents/MacOS/config
./Throne.app/Contents/MacOS/Throne & sleep 8; kill %1
sqlite3 Throne.app/Contents/MacOS/config/throne.db \
  "DELETE FROM settings WHERE key='ui_mode';
   INSERT INTO groups (id,name,url,profiles_json) VALUES (77,'Old','',  '[1]');"
./Throne.app/Contents/MacOS/Throne & sleep 10; kill %1
sqlite3 Throne.app/Contents/MacOS/config/throne.db "SELECT value FROM settings WHERE key='ui_mode';"
```

Ожидаемо: `0` — существующая установка осталась в расширенном режиме.

- [ ] **Step 3: Добавить возврат в простой режим**

В расширенном режиме нужен обратный переход, иначе пользователь, переключившийся один раз, застрянет. Пункт добавляется в служебное меню, рядом с уже перенесённым сбором отладочной информации. В `include/ui/mainwindow.ui`, в `menuHidden_menu`, сразу после `menu_profile_debug_info`:

```xml
    <addaction name="menu_simple_mode"/>
```

и само действие рядом с `menu_profile_debug_info`:

```xml
  <action name="menu_simple_mode">
   <property name="text">
    <string>Simple mode</string>
   </property>
  </action>
```

Слот в `include/ui/mainwindow.h`, рядом с `on_menu_profile_debug_info_triggered`:

```cpp
    void on_menu_simple_mode_triggered();
```

Реализация в `src/ui/mainWindow/mainwindow_system.cpp`:

```cpp
void MainWindow::on_menu_simple_mode_triggered() {
    const auto answer = QMessageBox::question(
        this, tr("Simple mode"),
        tr("The application will restart in the simple interface. Continue?"));
    if (answer != QMessageBox::StandardButton::Yes) return;

    Configs::dataManager->settingsRepo->ui_mode = static_cast<int>(Fsnt::UiMode::Simple);
    Configs::dataManager->settingsRepo->Save();
    this->exit_reason = ExitReason::Restart;
    on_menu_exit_triggered();
}
```

Добавить `#include "include/ui/fsnt/UiMode.hpp"` в этот файл.

Добавить переводы в контекст `MainWindow` файла `res/translations/ru_RU.ts`:

```xml
    <message>
        <source>Simple mode</source>
        <translation>Простой режим</translation>
    </message>
    <message>
        <source>The application will restart in the simple interface. Continue?</source>
        <translation>Приложение перезапустится в простом интерфейсе. Продолжить?</translation>
    </message>
```

- [ ] **Step 4: Собрать и проверить переход туда-обратно**

```bash
cmake --build build -j 2>&1 | tail -3
```

Затем вручную: запустить в простом режиме, нажать «Расширенный режим», подтвердить, запустить снова — должно открыться старое окно. В нём открыть «Скрытое меню» → «Простой режим», подтвердить — приложение перезапустится в новом окне.

- [ ] **Step 5: Коммит**

```bash
git add src/ui/mainWindow/mainwindow_setup.cpp src/ui/mainWindow/mainwindow_system.cpp \
        include/ui/mainwindow.h include/ui/mainwindow.ui res/translations/ru_RU.ts
git commit -m "Выбирать окно при старте и дать переход между простым и расширенным режимами"
```

---

### Task 5: Приёмка

**Files:**
- Modify: `docs/superpowers/specs/2026-09-04-fsnt-client-ui-design.md`

- [ ] **Step 1: Чистая сборка и тесты**

```bash
cd /Users/admin/work_vpn/throne_dev/Throne
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
rm -rf build-accept && cmake -B build-accept -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
cp build-baseline/srslist.h build-accept/srslist.h
cmake --build build-accept -j 2>&1 | tail -3
cmake --build build-test --target UiModeTest > /dev/null 2>&1 && ./build-test/tests/UiModeTest | tail -2
cmake --build build-test --target ProviderPolicyTest > /dev/null 2>&1 && ./build-test/tests/ProviderPolicyTest | tail -2
```

Ожидаемо: сборка без ошибок, `UiModeTest` 6 пройдено, `ProviderPolicyTest` 19 пройдено.

- [ ] **Step 2: Ручная проверка**

1. Чистая установка открывается в новом окне; в базе `ui_mode = 1`.
2. База с профилями открывается в старом окне; `ui_mode = 0`.
3. Кнопка «Расширенный режим» спрашивает подтверждение и после перезапуска открывает старое окно.
4. «Скрытое меню» → «Простой режим» возвращает в новое окно.
5. Смена темы в расширенном режиме меняет и оформление нового окна: переключить тему, вернуться в простой режим, убедиться, что цвета совпадают с расширенным.
6. Окно не меньше 760×500, панели делят ширину примерно поровну.

- [ ] **Step 3: Отметить прирост в спеке**

В разделе «Разбиение на приросты» пометить первый пункт как выполненный, указав дату и то, что панели пока пустые.

- [ ] **Step 4: Коммит**

```bash
git add docs/superpowers/specs/
git commit -m "Отметить готовность каркаса FSNT Client"
```

---

## Что этот план сознательно не делает

Панели остаются пустыми. Кнопка подключения, список серверов, карточка подписки и настройки простого режима — приросты 2-5 из спеки. Их планы пишутся после того, как каркас станет реальным кодом: точки расширения `serverPanelLayout()` и `sidePanelLayout()` до этого момента остаются предположением.

Иконка FN в шапке — текстовая заглушка. Замена на настоящий значок произойдёт, когда владелец его предоставит; место подготовлено объектом `fsntLogo`.
