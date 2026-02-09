# Throne Updater - Standalone application for installing updates

cmake_minimum_required(VERSION 3.16)

project(ThroneUpdater VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Находим Qt
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

# Исходные файлы
set(SOURCES
    main.cpp
)

# Создаем исполняемый файл
if(WIN32)
    add_executable(updater WIN32 ${SOURCES})
elseif(APPLE)
    add_executable(updater MACOSX_BUNDLE ${SOURCES})
else()
    add_executable(updater ${SOURCES})
endif()

# Линкуем Qt
target_link_libraries(updater PRIVATE Qt${QT_VERSION_MAJOR}::Core)

# Устанавливаем свойства
set_target_properties(updater PROPERTIES
    OUTPUT_NAME "updater"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Копируем updater в папку сборки Throne
add_custom_command(TARGET updater POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:updater>
        ${CMAKE_BINARY_DIR}/$<TARGET_FILE_NAME:updater>
    COMMENT "Copying updater to build directory"
)

# Установка
install(TARGETS updater
    RUNTIME DESTINATION .
    BUNDLE DESTINATION .
)
