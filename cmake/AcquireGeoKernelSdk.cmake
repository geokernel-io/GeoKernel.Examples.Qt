function(geokernel_acquire_sdk output_variable)
    if(NOT WIN32)
        message(FATAL_ERROR
            "Automatic GeoKernel SDK acquisition currently supports Windows x64 only. "
            "Set GEOKERNEL_ROOT to a local SDK for this platform.")
    endif()

    set(_package_name "geokernel-cpp-sdk")
    set(_archive_name "GeoKernel-${GEOKERNEL_SDK_VERSION}-SDK-windows-x64-msvc.zip")
    set(_registry_base
        "https://gitlab.com/api/v4/projects/84169108/packages/generic/${_package_name}/${GEOKERNEL_SDK_VERSION}")
    set(_cache_root "${CMAKE_CURRENT_SOURCE_DIR}/.geokernel-sdk")
    set(_download_root "${_cache_root}/downloads")
    set(_sdk_root "${_cache_root}/${GEOKERNEL_SDK_VERSION}/windows-x64-msvc")
    set(_archive_path "${_download_root}/${_archive_name}")
    set(_checksum_path "${_download_root}/${_archive_name}.sha256")
    set(_config_path "${_sdk_root}/lib/cmake/GeoKernel/GeoKernelConfig.cmake")

    if(EXISTS "${_config_path}")
        set(${output_variable} "${_sdk_root}" PARENT_SCOPE)
        message(STATUS "Using cached GeoKernel SDK: ${_sdk_root}")
        return()
    endif()

    set(_token "$ENV{GEOKERNEL_GITLAB_TOKEN}")
    if(NOT _token)
        message(FATAL_ERROR
            "GeoKernel SDK ${GEOKERNEL_SDK_VERSION} is not cached. Set the "
            "GEOKERNEL_GITLAB_TOKEN environment variable to a token with read_package_registry access, "
            "or configure with -DGEOKERNEL_ROOT=<extracted-sdk>.")
    endif()
    set(_token_type "$ENV{GEOKERNEL_GITLAB_TOKEN_TYPE}")
    if(NOT _token_type)
        set(_token_type "PRIVATE-TOKEN")
    endif()
    if(NOT _token_type MATCHES "^(PRIVATE-TOKEN|DEPLOY-TOKEN|JOB-TOKEN)$")
        message(FATAL_ERROR "Unsupported GEOKERNEL_GITLAB_TOKEN_TYPE: ${_token_type}")
    endif()

    file(MAKE_DIRECTORY "${_download_root}")
    file(DOWNLOAD
        "${_registry_base}/${_archive_name}.sha256"
        "${_checksum_path}"
        HTTPHEADER "${_token_type}: ${_token}"
        TLS_VERIFY ON
        STATUS _checksum_status
        LOG _checksum_log)
    list(GET _checksum_status 0 _checksum_code)
    if(NOT _checksum_code EQUAL 0)
        message(FATAL_ERROR "GeoKernel SDK checksum download failed: ${_checksum_status}\n${_checksum_log}")
    endif()
    file(READ "${_checksum_path}" _registry_checksum)
    string(REGEX MATCH "^[0-9a-fA-F]{64}" _registry_hash "${_registry_checksum}")
    string(TOLOWER "${_registry_hash}" _registry_hash)
    string(TOLOWER "${GEOKERNEL_SDK_SHA256}" _expected_hash)
    if(NOT _registry_hash STREQUAL _expected_hash)
        message(FATAL_ERROR
            "Pinned GeoKernel SDK SHA256 does not match the registry checksum. "
            "Expected ${_expected_hash}, registry ${_registry_hash}.")
    endif()

    file(DOWNLOAD
        "${_registry_base}/${_archive_name}"
        "${_archive_path}"
        HTTPHEADER "${_token_type}: ${_token}"
        EXPECTED_HASH "SHA256=${GEOKERNEL_SDK_SHA256}"
        TLS_VERIFY ON
        SHOW_PROGRESS
        STATUS _archive_status
        LOG _archive_log)
    list(GET _archive_status 0 _archive_code)
    if(NOT _archive_code EQUAL 0)
        message(FATAL_ERROR "GeoKernel SDK download failed: ${_archive_status}\n${_archive_log}")
    endif()

    file(REMOVE_RECURSE "${_sdk_root}")
    file(MAKE_DIRECTORY "${_sdk_root}")
    file(ARCHIVE_EXTRACT INPUT "${_archive_path}" DESTINATION "${_sdk_root}")
    if(NOT EXISTS "${_config_path}")
        message(FATAL_ERROR "Downloaded GeoKernel SDK is missing GeoKernelConfig.cmake.")
    endif()

    set(${output_variable} "${_sdk_root}" PARENT_SCOPE)
    message(STATUS "Downloaded and verified GeoKernel SDK ${GEOKERNEL_SDK_VERSION}.")
endfunction()
