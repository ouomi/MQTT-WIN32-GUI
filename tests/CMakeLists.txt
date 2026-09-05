# One small helper keeps names, warnings and CTest registration consistent.
# Protocol targets intentionally compile mqtt.c separately: their PAL, lock and
# clock definitions differ. No test executable is linked into the application.
function(add_mqtt_test name label)
    set(target win32mqtt_${name}_tests)
    add_executable(${target} ${ARGN})
    target_compile_features(${target} PRIVATE c_std_99 cxx_std_17)
    target_include_directories(${target} PRIVATE . ../src ../src/mqtt/mqtt_c)
    if(MSVC)
        target_compile_definitions(${target} PRIVATE WIN32MQTT_TEST_MSVC=1)
        target_compile_options(${target} PRIVATE /W4 $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    # Cross-compilation alone does not mean the tests have run.
    if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES TIMEOUT 10 LABELS "${label}")
    endif()
endfunction()

set(protocol_source ../src/mqtt/mqtt_c/mqtt.c)
set(transport_source ../src/mqtt/mqtt_c/mqtt_pal.c)

add_mqtt_test(settings_document core settings_document_tests.cpp ../src/settings_document.cpp ../src/mqtt/mqtt_endpoint.cpp)

add_mqtt_test(core core core_module_tests.cpp ../src/mqtt/mqtt_endpoint.cpp ../src/subscription_catalog.cpp)

add_mqtt_test(publish protocol mqtt_publish_tests.c ${protocol_source})
add_mqtt_test(init protocol mqtt_init_tests.c ${protocol_source})
add_mqtt_test(protocol protocol mqtt_protocol_tests.c ${protocol_source})
add_mqtt_test(disconnect connection mqtt_disconnect_tests.cpp ${protocol_source})
add_mqtt_test(capacity core mqtt_capacity_tests.cpp ${protocol_source})
foreach(name IN ITEMS publish init protocol disconnect capacity)
    target_compile_definitions(win32mqtt_${name}_tests PRIVATE MQTT_CUSTOM_PAL_HEADER="mqtt_test_pal.h")
endforeach()
foreach(name IN ITEMS init protocol disconnect capacity)
    target_compile_definitions(win32mqtt_${name}_tests PRIVATE WIN32MQTT_TEST_TRACK_LOCKS=1)
endforeach()
target_compile_definitions(win32mqtt_protocol_tests PRIVATE WIN32MQTT_TEST_CLOCK=1)

add_mqtt_test(connect_attempt connection mqtt_connect_attempt_tests.cpp)
add_mqtt_test(dns connection mqtt_dns_tests.cpp)
target_compile_definitions(win32mqtt_dns_tests PRIVATE WIN32MQTT_DNS_TEST_API=1)
add_mqtt_test(display core mqtt_display_tests.cpp ../src/win32/mqtt_window_bridge.cpp)
find_package(Threads REQUIRED)
target_link_libraries(win32mqtt_dns_tests PRIVATE Threads::Threads)
target_link_libraries(win32mqtt_display_tests PRIVATE Threads::Threads)

add_mqtt_test(socket transport mqtt_transport_tests.c ${transport_source})
target_compile_definitions(win32mqtt_socket_tests PRIVATE MQTT_CUSTOM_PAL_HEADER="mqtt_transport_pal.h")
if(TARGET OpenSSL::SSL)
    add_mqtt_test(bio transport mqtt_transport_tests.c ${transport_source})
    target_compile_definitions(win32mqtt_bio_tests PRIVATE
        MQTT_CUSTOM_PAL_HEADER="mqtt_transport_pal.h" MQTT_USE_BIO=1)
    target_link_libraries(win32mqtt_bio_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
else()
    message(STATUS "BIO tests disabled: OpenSSL was not found or TLS is disabled for this Windows build.")
endif()

if(NOT WIN32)
    add_mqtt_test(session connection mqtt_session_tests.cpp ../src/mqtt/mqtt_session.cpp
        ../src/mqtt/mqtt_endpoint.cpp ${protocol_source})
    target_compile_definitions(win32mqtt_session_tests PRIVATE
        MQTT_CUSTOM_PAL_HEADER="mqtt_session_pal.h" WIN32MQTT_ENABLE_TLS=0)
    target_link_libraries(win32mqtt_session_tests PRIVATE Threads::Threads)
    set_tests_properties(win32mqtt_session_tests PROPERTIES TIMEOUT 30)
endif()

option(WIN32MQTT_BROKER_TESTS "Build opt-in real Windows/Broker integration runner" OFF)
if(WIN32 AND WIN32MQTT_BROKER_TESTS)
    add_executable(win32mqtt_live_tests mqtt_live_tests.cpp ../src/mqtt/mqtt_session.cpp
        ../src/mqtt/mqtt_endpoint.cpp ${protocol_source} ${transport_source})
    target_compile_features(win32mqtt_live_tests PRIVATE cxx_std_17 c_std_99)
    target_include_directories(win32mqtt_live_tests PRIVATE ../src ../src/mqtt/mqtt_c)
    target_compile_definitions(win32mqtt_live_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
        _WIN32_WINNT=0x0602 WIN32MQTT_ENABLE_TLS=$<BOOL:${WIN32MQTT_ENABLE_TLS}>)
    if(MINGW)
        target_compile_definitions(win32mqtt_live_tests PRIVATE WIN32MQTT_MINGW=1)
    endif()
    target_link_libraries(win32mqtt_live_tests PRIVATE ws2_32)
    if(WIN32MQTT_ENABLE_TLS)
        target_compile_definitions(win32mqtt_live_tests PRIVATE MQTT_USE_BIO=1)
        target_link_libraries(win32mqtt_live_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()
endif()

if(WIN32)
    add_mqtt_test(settings core mqtt_settings_tests.cpp ../src/win32/app_settings.cpp ../src/settings_document.cpp ../src/mqtt/mqtt_endpoint.cpp ../src/subscription_catalog.cpp)
    target_compile_definitions(win32mqtt_settings_tests PRIVATE UNICODE _UNICODE NOMINMAX)
endif()

if(TARGET OpenSSL::SSL)
    add_mqtt_test(tls transport mqtt_tls_tests.c ${protocol_source} ${transport_source})
    target_compile_definitions(win32mqtt_tls_tests PRIVATE MQTT_CUSTOM_PAL_HEADER="mqtt_test_pal.h"
        WIN32MQTT_TEST_TRACK_LOCKS=1 MQTT_USE_BIO=1)
    target_link_libraries(win32mqtt_tls_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
