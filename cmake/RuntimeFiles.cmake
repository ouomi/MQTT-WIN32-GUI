# The installed directory is the portable package. Tests are never installed.
install(TARGETS win32mqtt RUNTIME DESTINATION .)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION .)
install(FILES "${PROJECT_SOURCE_DIR}/resources/licenses/MQTT-C.txt" DESTINATION LICENSES)

if(NOT WIN32MQTT_ENABLE_TLS)
    return()
endif()

set(runtime_files "${PROJECT_SOURCE_DIR}/resources/ca-bundle.pem")
install(FILES ${runtime_files} DESTINATION .)
install(FILES "${PROJECT_SOURCE_DIR}/resources/licenses/OpenSSL.txt" DESTINATION LICENSES)

if(WIN32MQTT_BUNDLE_OPENSSL)
    # An explicit directory also supports OpenSSL installations outside vcpkg.
    set(WIN32MQTT_OPENSSL_RUNTIME_DIR "" CACHE PATH "Directory containing the matching OpenSSL runtime DLLs")
    if(WIN32MQTT_OPENSSL_RUNTIME_DIR)
        set(runtime_dir "${WIN32MQTT_OPENSSL_RUNTIME_DIR}")
    elseif(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(runtime_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    else()
        message(FATAL_ERROR "DLL bundling needs a vcpkg triplet or WIN32MQTT_OPENSSL_RUNTIME_DIR. For static or externally managed OpenSSL, set WIN32MQTT_BUNDLE_OPENSSL=OFF.")
    endif()

    # MinGW FindOpenSSL uses release import libraries. MSVC selects debug libraries
    # for Debug, so use the matching vcpkg runtime directory for that configuration.
    foreach(library IN ITEMS ssl crypto)
        file(GLOB dlls CONFIGURE_DEPENDS "${runtime_dir}/lib${library}-*.dll")
        list(LENGTH dlls count)
        if(NOT count EQUAL 1)
            message(FATAL_ERROR "Expected one lib${library}-*.dll in ${runtime_dir}; found ${count}.")
        endif()
        if(MSVC AND NOT WIN32MQTT_OPENSSL_RUNTIME_DIR)
            file(GLOB debug_dlls CONFIGURE_DEPENDS
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin/lib${library}-*.dll")
            list(LENGTH debug_dlls debug_count)
            if(NOT debug_count EQUAL 1)
                message(FATAL_ERROR "Expected one Debug OpenSSL ${library} DLL in the vcpkg triplet.")
            endif()
            set(dlls "$<IF:$<CONFIG:Debug>,${debug_dlls},${dlls}>")
        endif()
        list(APPEND runtime_files "${dlls}")
        install(FILES "${dlls}" DESTINATION .)
    endforeach()
endif()

# Run on each app build, including when only the CA bundle changed or a copied
# DLL was removed. TARGET_FILE_DIR also handles multi-config generators.
# CMP0112 (NEW with our CMake minimum) avoids a dependency cycle here.
add_custom_target(win32mqtt_runtime
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:win32mqtt>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${runtime_files} "$<TARGET_FILE_DIR:win32mqtt>"
    COMMENT "Copying MQTT runtime files"
    VERBATIM
)
add_dependencies(win32mqtt win32mqtt_runtime)
