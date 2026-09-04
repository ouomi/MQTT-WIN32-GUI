cmake_minimum_required(VERSION 3.20)

project(win32mqtt VERSION 0.1.0 LANGUAGES C CXX RC)

include(CTest)

option(WIN32MQTT_ENABLE_TLS "Enable mqtts:// using OpenSSL" ON)
option(WIN32MQTT_OPENSSL_DYNAMIC "Bundle OpenSSL DLLs beside WIN32-MQTT.exe" ON)

if(NOT WIN32)
    message(FATAL_ERROR
        "WIN32-MQTT is a Win32 GUI application. On Linux, configure it with "
        "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw64.cmake.")
endif()

add_executable(win32mqtt WIN32
    resources/app.rc
    src/win32/main.cpp
    src/win32/main_window.cpp
    src/win32/app_settings.cpp
    src/win32/app_settings.h
    src/win32/control_helpers.cpp
    src/win32/localization.cpp
    src/win32/connection_panel.cpp
    src/win32/message_panel.cpp
    src/win32/publish_panel.cpp
    src/win32/subscription_panel.cpp
    src/win32/will_panel.cpp
    src/win32/will_panel.h
    src/win32/mqtt_window_bridge.cpp
    src/subscription_catalog.cpp
    src/subscription_catalog.hpp
    src/mqtt/mqtt_endpoint.cpp
    src/mqtt/mqtt_endpoint.hpp
    src/mqtt/mqtt_session.cpp
    src/mqtt/mqtt_c/mqtt.c
    src/mqtt/mqtt_c/mqtt_pal.c
)

set_target_properties(win32mqtt PROPERTIES OUTPUT_NAME "WIN32-MQTT")
target_compile_features(win32mqtt PRIVATE c_std_99 cxx_std_17)
target_include_directories(win32mqtt PRIVATE src/mqtt/mqtt_c)

if(BUILD_TESTING)
    add_executable(win32mqtt_core_tests
        tests/core_module_tests.cpp
        src/mqtt/mqtt_endpoint.cpp
        src/mqtt/mqtt_endpoint.hpp
        src/subscription_catalog.cpp
        src/subscription_catalog.hpp
    )
    target_compile_features(win32mqtt_core_tests PRIVATE cxx_std_17)
    target_include_directories(win32mqtt_core_tests PRIVATE src)
    if(MSVC)
        target_compile_options(win32mqtt_core_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(win32mqtt_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
        add_test(NAME win32mqtt_core_modules COMMAND win32mqtt_core_tests)
    endif()
endif()
target_compile_definitions(win32mqtt PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
if(MINGW)
    target_compile_definitions(win32mqtt PRIVATE WIN32MQTT_MINGW=1)
endif()

if(WIN32MQTT_ENABLE_TLS)
    find_package(OpenSSL REQUIRED)
    target_compile_definitions(win32mqtt PRIVATE MQTT_USE_BIO WIN32MQTT_ENABLE_TLS=1)
    target_link_libraries(win32mqtt PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    configure_file(resources/ca-bundle.pem ca-bundle.pem COPYONLY)
else()
    target_compile_definitions(win32mqtt PRIVATE WIN32MQTT_ENABLE_TLS=0)
endif()

target_link_libraries(win32mqtt PRIVATE
    comctl32
    ws2_32
    user32
    gdi32
    dwmapi
    uxtheme
)

install(TARGETS win32mqtt RUNTIME DESTINATION .)
install(FILES resources/licenses/MQTT-C.txt DESTINATION LICENSES)
if(WIN32MQTT_ENABLE_TLS)
    install(FILES resources/ca-bundle.pem DESTINATION .)
    install(FILES resources/licenses/OpenSSL.txt DESTINATION LICENSES)
    if(WIN32MQTT_OPENSSL_DYNAMIC)
        if(NOT DEFINED VCPKG_INSTALLED_DIR OR NOT DEFINED VCPKG_TARGET_TRIPLET)
            message(FATAL_ERROR "WIN32MQTT_OPENSSL_DYNAMIC requires a vcpkg triplet.")
        endif()
        set(WIN32MQTT_OPENSSL_RUNTIME_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        file(GLOB WIN32MQTT_OPENSSL_RUNTIME_DLLS CONFIGURE_DEPENDS
            "${WIN32MQTT_OPENSSL_RUNTIME_DIR}/libssl-*.dll"
            "${WIN32MQTT_OPENSSL_RUNTIME_DIR}/libcrypto-*.dll")
        list(LENGTH WIN32MQTT_OPENSSL_RUNTIME_DLLS WIN32MQTT_OPENSSL_RUNTIME_DLL_COUNT)
        if(NOT WIN32MQTT_OPENSSL_RUNTIME_DLL_COUNT EQUAL 2)
            message(FATAL_ERROR "Expected exactly two OpenSSL runtime DLLs in ${WIN32MQTT_OPENSSL_RUNTIME_DIR}.")
        endif()
        install(FILES ${WIN32MQTT_OPENSSL_RUNTIME_DLLS} DESTINATION .)
    endif()
endif()

if(MSVC)
    target_compile_options(win32mqtt PRIVATE /W4 /permissive-)
else()
    target_compile_options(win32mqtt PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(win32mqtt PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()
