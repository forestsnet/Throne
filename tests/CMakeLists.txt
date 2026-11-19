find_package(Qt6 REQUIRED COMPONENTS Test Widgets)

# Класс теста объявлен прямо в .cpp и включает свой .moc — нужен AUTOMOC.
set(CMAKE_AUTOMOC ON)

# Compile the real request helper and dialog against isolated application
# services. Networking and widget event delivery use Qt itself.
qt_add_executable(HTTPRequestHelperTest
        HTTPRequestHelperTest.cpp
        ../include/global/HTTPRequestHelper.hpp
        ../src/global/HTTPRequestHelper.cpp
        ../src/global/NetworkFailureText.cpp
)
target_include_directories(HTTPRequestHelperTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/http_import_stubs ${CMAKE_SOURCE_DIR})
target_link_libraries(HTTPRequestHelperTest PRIVATE Qt6::Test Qt6::Network)
add_test(NAME HTTPRequestHelperTest COMMAND HTTPRequestHelperTest)

qt_add_executable(AddSubscriptionDialogTest
        AddSubscriptionDialogTest.cpp
        ../include/ui/fsnt/AddSubscriptionDialog.h
        ../src/ui/fsnt/AddSubscriptionDialog.cpp
)
target_include_directories(AddSubscriptionDialogTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/http_import_stubs ${CMAKE_SOURCE_DIR})
target_link_libraries(AddSubscriptionDialogTest PRIVATE Qt6::Test Qt6::Widgets)
add_test(NAME AddSubscriptionDialogTest COMMAND AddSubscriptionDialogTest)
if (WIN32)
    set_tests_properties(AddSubscriptionDialogTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
else ()
    set_tests_properties(AddSubscriptionDialogTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif ()

qt_add_executable(DirectDnsSelectionTest
        DirectDnsSelectionTest.cpp
        ../src/global/DirectDnsSelection.cpp
        ../src/configs/DnsAddress.cpp
)
target_include_directories(DirectDnsSelectionTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(DirectDnsSelectionTest PRIVATE Qt6::Test)
add_test(NAME DirectDnsSelectionTest COMMAND DirectDnsSelectionTest)

qt_add_executable(TunIPv6PolicyTest
        TunIPv6PolicyTest.cpp
        ../src/configs/TunIPv6Policy.cpp
)
target_include_directories(TunIPv6PolicyTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(TunIPv6PolicyTest PRIVATE Qt6::Test)
add_test(NAME TunIPv6PolicyTest COMMAND TunIPv6PolicyTest)

qt_add_executable(ProviderPolicyTest
        ProviderPolicyTest.cpp
        ../src/configs/sub/ProviderPolicy.cpp
)
target_include_directories(ProviderPolicyTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(ProviderPolicyTest PRIVATE Qt6::Test)

add_test(NAME ProviderPolicyTest COMMAND ProviderPolicyTest)

qt_add_executable(TunDiagnosticsTextTest
        TunDiagnosticsTextTest.cpp
        ../src/sys/TunDiagnosticsText.cpp
)
target_include_directories(TunDiagnosticsTextTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(TunDiagnosticsTextTest PRIVATE Qt6::Test)
add_test(NAME TunDiagnosticsTextTest COMMAND TunDiagnosticsTextTest)

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

qt_add_executable(CustomOutboundsTest
        CustomOutboundsTest.cpp
        ../src/configs/CustomOutbounds.cpp
)
target_include_directories(CustomOutboundsTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(CustomOutboundsTest PRIVATE Qt6::Test)
add_test(NAME CustomOutboundsTest COMMAND CustomOutboundsTest)

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

qt_add_executable(SubscriptionHostTest
        SubscriptionHostTest.cpp
        ../src/configs/sub/SubscriptionHost.cpp
)
target_include_directories(SubscriptionHostTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(SubscriptionHostTest PRIVATE Qt6::Test)

add_test(NAME SubscriptionHostTest COMMAND SubscriptionHostTest)

qt_add_executable(ExitLookupPlanTest
        ExitLookupPlanTest.cpp
        ../src/global/ExitLookupPlan.cpp
)
target_include_directories(ExitLookupPlanTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(ExitLookupPlanTest PRIVATE Qt6::Test Qt6::Network)

add_test(NAME ExitLookupPlanTest COMMAND ExitLookupPlanTest)
