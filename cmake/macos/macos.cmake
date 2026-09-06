find_library(SECURITY_FRAMEWORK Security)
find_library(USER_NOTIFICATIONS_FRAMEWORK UserNotifications)
set(PLATFORM_SOURCES include/sys/macos/MacOS.h src/sys/macos/MacOS.cpp src/sys/macos/AutoRun.cpp src/sys/macos/UrlScheme.cpp
        include/sys/macos/MacNotify.hpp src/sys/macos/MacNotify.mm)
set(PLATFORM_LIBRARIES ${SECURITY_FRAMEWORK} ${USER_NOTIFICATIONS_FRAMEWORK})
# ARC только для файла с уведомлениями: в остальном проекте Objective-C нет,
# а тут иначе пришлось бы вручную считать ссылки на объекты уведомления.
set_source_files_properties(src/sys/macos/MacNotify.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
