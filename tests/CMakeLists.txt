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

foreach(test_target IN ITEMS win32mqtt_core_tests win32mqtt_publish_tests win32mqtt_init_tests)
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
