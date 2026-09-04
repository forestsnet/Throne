find_package(Qt6 REQUIRED COMPONENTS Test)

# Класс теста объявлен прямо в .cpp и включает свой .moc — нужен AUTOMOC.
set(CMAKE_AUTOMOC ON)

qt_add_executable(ProviderPolicyTest
        ProviderPolicyTest.cpp
        ../src/configs/sub/ProviderPolicy.cpp
)
target_include_directories(ProviderPolicyTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(ProviderPolicyTest PRIVATE Qt6::Test)

add_test(NAME ProviderPolicyTest COMMAND ProviderPolicyTest)

qt_add_executable(UiModeTest
        UiModeTest.cpp
        ../src/ui/fsnt/UiMode.cpp
)
target_include_directories(UiModeTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(UiModeTest PRIVATE Qt6::Test)

add_test(NAME UiModeTest COMMAND UiModeTest)

qt_add_executable(SubscriptionUsageTest
        SubscriptionUsageTest.cpp
        ../src/configs/sub/SubscriptionUsage.cpp
)
target_include_directories(SubscriptionUsageTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(SubscriptionUsageTest PRIVATE Qt6::Test)

add_test(NAME SubscriptionUsageTest COMMAND SubscriptionUsageTest)

qt_add_executable(XrayDnsStripTest
        XrayDnsStripTest.cpp
        ../src/configs/XrayDnsStrip.cpp
)
target_include_directories(XrayDnsStripTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(XrayDnsStripTest PRIVATE Qt6::Test)

add_test(NAME XrayDnsStripTest COMMAND XrayDnsStripTest)
