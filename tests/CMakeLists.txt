add_executable(win32mqtt_core_tests
    core_module_tests.cpp
    ../src/mqtt/mqtt_endpoint.cpp
    ../src/subscription_catalog.cpp
)
target_compile_features(win32mqtt_core_tests PRIVATE cxx_std_17)
target_include_directories(win32mqtt_core_tests PRIVATE ../src)

# Compile the actual in-tree protocol implementation with an isolated test PAL.
add_executable(win32mqtt_publish_tests
    mqtt_publish_tests.c
    ../src/mqtt/mqtt_c/mqtt.c
)
target_compile_features(win32mqtt_publish_tests PRIVATE c_std_99)
target_include_directories(win32mqtt_publish_tests PRIVATE . ../src/mqtt/mqtt_c)
target_compile_definitions(win32mqtt_publish_tests PRIVATE
    MQTT_CUSTOM_PAL_HEADER="mqtt_test_pal.h"
)
if(MSVC)
    target_compile_definitions(win32mqtt_publish_tests PRIVATE WIN32MQTT_TEST_MSVC=1)
endif()

add_executable(win32mqtt_init_tests
    mqtt_init_tests.c
    ../src/mqtt/mqtt_c/mqtt.c
)
target_compile_features(win32mqtt_init_tests PRIVATE c_std_99)
target_include_directories(win32mqtt_init_tests PRIVATE . ../src/mqtt/mqtt_c)
target_compile_definitions(win32mqtt_init_tests PRIVATE
    MQTT_CUSTOM_PAL_HEADER="mqtt_test_pal.h"
    WIN32MQTT_TEST_TRACK_LOCKS=1
)
if(MSVC)
    target_compile_definitions(win32mqtt_init_tests PRIVATE WIN32MQTT_TEST_MSVC=1)
endif()

add_executable(win32mqtt_backpressure_tests mqtt_backpressure_tests.c ../src/mqtt/mqtt_c/mqtt.c)
target_compile_definitions(win32mqtt_backpressure_tests PRIVATE
    MQTT_CUSTOM_PAL_HEADER="mqtt_test_pal.h"
    WIN32MQTT_TEST_TRACK_LOCKS=1 WIN32MQTT_TEST_CLOCK=1)

add_executable(win32mqtt_socket_tests mqtt_transport_tests.c ../src/mqtt/mqtt_c/mqtt_pal.c)
target_compile_definitions(win32mqtt_socket_tests PRIVATE MQTT_CUSTOM_PAL_HEADER="mqtt_transport_pal.h")

add_executable(win32mqtt_disconnect_tests mqtt_disconnect_tests.cpp ../src/mqtt/mqtt_c/mqtt.c)
target_compile_features(win32mqtt_disconnect_tests PRIVATE cxx_std_17)
target_include_directories(win32mqtt_disconnect_tests PRIVATE ../src)
target_compile_definitions(win32mqtt_disconnect_tests PRIVATE
    MQTT_CUSTOM_PAL_HEADER="mqtt_test_pal.h" WIN32MQTT_TEST_TRACK_LOCKS=1)

add_executable(win32mqtt_capacity_tests mqtt_capacity_tests.cpp ../src/mqtt/mqtt_c/mqtt.c)
target_compile_features(win32mqtt_capacity_tests PRIVATE cxx_std_17)
target_include_directories(win32mqtt_capacity_tests PRIVATE ../src)
target_compile_definitions(win32mqtt_capacity_tests PRIVATE
    MQTT_CUSTOM_PAL_HEADER="mqtt_test_pal.h" WIN32MQTT_TEST_TRACK_LOCKS=1)

add_executable(win32mqtt_connect_attempt_tests mqtt_connect_attempt_tests.cpp)
add_executable(win32mqtt_dns_tests mqtt_dns_tests.cpp)
target_compile_definitions(win32mqtt_dns_tests PRIVATE WIN32MQTT_DNS_TEST_API=1)
find_package(Threads REQUIRED)
target_link_libraries(win32mqtt_dns_tests PRIVATE Threads::Threads)
add_executable(win32mqtt_display_tests mqtt_display_tests.cpp ../src/win32/mqtt_window_bridge.cpp)
target_link_libraries(win32mqtt_display_tests PRIVATE Threads::Threads)
foreach(test_target IN ITEMS win32mqtt_connect_attempt_tests win32mqtt_dns_tests win32mqtt_display_tests)
    target_compile_features(${test_target} PRIVATE cxx_std_17)
    target_include_directories(${test_target} PRIVATE ../src)
endforeach()

set(transport_tests win32mqtt_backpressure_tests win32mqtt_socket_tests win32mqtt_disconnect_tests
    win32mqtt_connect_attempt_tests win32mqtt_dns_tests win32mqtt_capacity_tests win32mqtt_display_tests)
# Native protocol tests remain available without OpenSSL. TLS application builds
# also compile the BIO tests with the same OpenSSL selected for the application.
if(NOT WIN32 OR WIN32MQTT_ENABLE_TLS)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        add_executable(win32mqtt_bio_tests mqtt_transport_tests.c ../src/mqtt/mqtt_c/mqtt_pal.c)
        target_compile_definitions(win32mqtt_bio_tests PRIVATE
            MQTT_CUSTOM_PAL_HEADER="mqtt_transport_pal.h" MQTT_USE_BIO=1)
        target_link_libraries(win32mqtt_bio_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        list(APPEND transport_tests win32mqtt_bio_tests)
    else()
        message(STATUS "OpenSSL unavailable: BIO transport tests are skipped")
    endif()
endif()
foreach(test_target IN LISTS transport_tests)
    target_compile_features(${test_target} PRIVATE c_std_99)
    target_include_directories(${test_target} PRIVATE . ../src/mqtt/mqtt_c)
    if(MSVC)
        target_compile_definitions(${test_target} PRIVATE WIN32MQTT_TEST_MSVC=1)
    endif()
    if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
        add_test(NAME ${test_target} COMMAND ${test_target})
        set_tests_properties(${test_target} PROPERTIES TIMEOUT 10)
    endif()
endforeach()

foreach(test_target IN ITEMS win32mqtt_core_tests win32mqtt_publish_tests win32mqtt_init_tests ${transport_tests})
    if(MSVC)
        target_compile_options(${test_target} PRIVATE /W4)
    else()
        target_compile_options(${test_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
if(MSVC)
    target_compile_options(win32mqtt_core_tests PRIVATE /permissive-)
endif()

if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
    add_test(NAME win32mqtt_core_modules COMMAND win32mqtt_core_tests)
    add_test(NAME win32mqtt_publish_parser COMMAND win32mqtt_publish_tests)
    add_test(NAME win32mqtt_initialization COMMAND win32mqtt_init_tests)
endif()
