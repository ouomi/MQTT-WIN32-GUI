cmake_minimum_required(VERSION 3.20)

project(win32mqtt VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

option(WIN32MQTT_BUILD_APP "Build the Windows GUI application" ${WIN32})
option(WIN32MQTT_ENABLE_TLS "Enable mqtts:// using OpenSSL" ON)
option(WIN32MQTT_BUNDLE_OPENSSL "Copy and install OpenSSL runtime DLLs" OFF)

if(WIN32MQTT_BUILD_APP AND NOT WIN32)
    message(FATAL_ERROR "The GUI requires Windows or a MinGW toolchain. Use the native-tests preset to test on Linux.")
endif()
if(NOT WIN32MQTT_BUILD_APP AND NOT BUILD_TESTING)
    message(FATAL_ERROR "Nothing to build: enable WIN32MQTT_BUILD_APP or BUILD_TESTING.")
endif()

# Find OpenSSL once. It is required by the TLS app and optional for native BIO tests.
if(WIN32MQTT_BUILD_APP AND WIN32MQTT_ENABLE_TLS)
    find_package(OpenSSL REQUIRED)
elseif(BUILD_TESTING AND (NOT WIN32 OR WIN32MQTT_ENABLE_TLS))
    find_package(OpenSSL QUIET)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(NOT WIN32MQTT_BUILD_APP)
    message(STATUS "Building tests only; the Windows GUI is disabled.")
    return()
endif()

enable_language(RC)

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

set_target_properties(win32mqtt PROPERTIES
    OUTPUT_NAME "WIN32-MQTT"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
target_compile_features(win32mqtt PRIVATE c_std_99 cxx_std_17)
target_include_directories(win32mqtt PRIVATE src/mqtt/mqtt_c)

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
    target_compile_definitions(win32mqtt PRIVATE MQTT_USE_BIO WIN32MQTT_ENABLE_TLS=1)
    target_link_libraries(win32mqtt PRIVATE OpenSSL::SSL OpenSSL::Crypto)
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

if(MSVC)
    target_compile_options(win32mqtt PRIVATE /W4 /permissive-)
else()
    target_compile_options(win32mqtt PRIVATE -Wall -Wextra -Wpedantic)
    if(MINGW)
        target_link_options(win32mqtt PRIVATE -municode -static -static-libgcc -static-libstdc++)
    endif()
endif()

include(cmake/RuntimeFiles.cmake)
