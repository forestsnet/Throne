find_package(Qt6 REQUIRED COMPONENTS Test Widgets)

# Класс теста объявлен прямо в .cpp и включает свой .moc — нужен AUTOMOC.
set(CMAKE_AUTOMOC ON)

qt_add_executable(ProviderPolicyTest
        ProviderPolicyTest.cpp
        ../src/configs/sub/ProviderPolicy.cpp
)
target_include_directories(ProviderPolicyTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(ProviderPolicyTest PRIVATE Qt6::Test)

add_test(NAME ProviderPolicyTest COMMAND ProviderPolicyTest)

qt_add_executable(ProfileSuccessorTest
        ProfileSuccessorTest.cpp
        ../src/configs/sub/ProfileSuccessor.cpp
)
target_include_directories(ProfileSuccessorTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(ProfileSuccessorTest PRIVATE Qt6::Test)

add_test(NAME ProfileSuccessorTest COMMAND ProfileSuccessorTest)

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

qt_add_executable(GeoAssetMirrorsTest
        GeoAssetMirrorsTest.cpp
        ../src/engine/services/GeoAssetMirrors.cpp
)
target_include_directories(GeoAssetMirrorsTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(GeoAssetMirrorsTest PRIVATE Qt6::Test)

add_test(NAME GeoAssetMirrorsTest COMMAND GeoAssetMirrorsTest)

qt_add_executable(DnsAddressTest
        DnsAddressTest.cpp
        ../src/configs/DnsAddress.cpp
)
target_include_directories(DnsAddressTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(DnsAddressTest PRIVATE Qt6::Test)

add_test(NAME DnsAddressTest COMMAND DnsAddressTest)

qt_add_executable(DnsProbeTest
        DnsProbeTest.cpp
        ../include/global/DnsProbe.hpp
        ../src/global/DnsProbe.cpp
        ../src/configs/DnsAddress.cpp
)
target_include_directories(DnsProbeTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(DnsProbeTest PRIVATE Qt6::Test Qt6::Network)

add_test(NAME DnsProbeTest COMMAND DnsProbeTest)

qt_add_executable(SystemStateJournalTest
        SystemStateJournalTest.cpp
        ../src/global/SystemStateJournal.cpp
)
target_include_directories(SystemStateJournalTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(SystemStateJournalTest PRIVATE Qt6::Test)

add_test(NAME SystemStateJournalTest COMMAND SystemStateJournalTest)

qt_add_executable(CoreExitReasonTest
        CoreExitReasonTest.cpp
        ../src/engine/supervisor/CoreExitReason.cpp
)
target_include_directories(CoreExitReasonTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(CoreExitReasonTest PRIVATE Qt6::Test)

add_test(NAME CoreExitReasonTest COMMAND CoreExitReasonTest)

qt_add_executable(NetworkFailureTextTest
        NetworkFailureTextTest.cpp
        ../src/global/NetworkFailureText.cpp
)
target_include_directories(NetworkFailureTextTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(NetworkFailureTextTest PRIVATE Qt6::Test Qt6::Network)

add_test(NAME NetworkFailureTextTest COMMAND NetworkFailureTextTest)

# Виджетный тест: своё окно, колесо приходит через оконную систему, поэтому
# нужен offscreen — на машине сборки дисплея может не быть.
qt_add_executable(WheelGuardTest
        WheelGuardTest.cpp
        ../src/ui/app/WheelGuard.cpp
)
target_include_directories(WheelGuardTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(WheelGuardTest PRIVATE Qt6::Test Qt6::Widgets)

add_test(NAME WheelGuardTest COMMAND WheelGuardTest)
set_tests_properties(WheelGuardTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
