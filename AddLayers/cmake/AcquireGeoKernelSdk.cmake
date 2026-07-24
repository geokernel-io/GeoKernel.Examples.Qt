function(_geokernel_resolve_latest_release output_version output_json cache_root)
    set(_release_json "${cache_root}/latest-release.json")
    file(MAKE_DIRECTORY "${cache_root}")
    file(DOWNLOAD
        "https://api.github.com/repos/geokernel-io/GeoKernel/releases/latest"
        "${_release_json}"
        HTTPHEADER "Accept: application/vnd.github+json"
        HTTPHEADER "User-Agent: GeoKernel-AddLayers-CMake"
        TLS_VERIFY ON
        STATUS _release_status)
    list(GET _release_status 0 _release_code)
    if(NOT _release_code EQUAL 0)
        message(FATAL_ERROR "Latest GeoKernel release lookup failed: ${_release_status}")
    endif()

    file(READ "${_release_json}" _release_data)
    string(JSON _release_tag ERROR_VARIABLE _json_error GET "${_release_data}" tag_name)
    if(_json_error OR NOT _release_tag MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)$")
        message(FATAL_ERROR "GitHub latest release response has no valid vMAJOR.MINOR.PATCH tag.")
    endif()
    set(${output_version} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    set(${output_json} "${_release_data}" PARENT_SCOPE)
endfunction()

function(_geokernel_require_release_asset release_json asset_name)
    string(JSON _asset_count ERROR_VARIABLE _asset_error LENGTH "${release_json}" assets)
    if(_asset_error)
        message(FATAL_ERROR "GitHub latest release response has no asset list.")
    endif()

    set(_found FALSE)
    if(_asset_count GREATER 0)
        math(EXPR _asset_last "${_asset_count} - 1")
        foreach(_asset_index RANGE 0 ${_asset_last})
            string(JSON _candidate GET "${release_json}" assets ${_asset_index} name)
            if(_candidate STREQUAL "${asset_name}")
                set(_found TRUE)
                break()
            endif()
        endforeach()
    endif()
    if(NOT _found)
        message(FATAL_ERROR
            "Latest GeoKernel release does not contain the required asset: ${asset_name}")
    endif()
endfunction()

function(geokernel_acquire_sdk output_variable)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Automatic GeoKernel SDK acquisition supports x64 builds only.")
    endif()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _system_processor)
    if(NOT _system_processor MATCHES "^(amd64|x86_64|x64)$")
        message(FATAL_ERROR
            "Automatic GeoKernel SDK acquisition supports x64 only; detected ${CMAKE_SYSTEM_PROCESSOR}.")
    endif()

    set(_cache_root "${CMAKE_CURRENT_SOURCE_DIR}/.geokernel-sdk")
    if(GEOKERNEL_SDK_VERSION STREQUAL "latest" OR GEOKERNEL_SDK_VERSION STREQUAL "")
        _geokernel_resolve_latest_release(_sdk_version _latest_release_json "${_cache_root}")
    elseif(GEOKERNEL_SDK_VERSION MATCHES "^v?([0-9]+\\.[0-9]+\\.[0-9]+)$")
        set(_sdk_version "${CMAKE_MATCH_1}")
    else()
        message(FATAL_ERROR "GEOKERNEL_SDK_VERSION must be 'latest' or MAJOR.MINOR.PATCH.")
    endif()

    if(WIN32)
        set(_platform "windows-x64-msvc")
        set(_archive_name "GeoKernel-${_sdk_version}-SDK-windows-x64-msvc.zip")
        set(_sdk_root "${_cache_root}/${_sdk_version}/${_platform}")
        set(_config_path "${_sdk_root}/lib/cmake/GeoKernel/GeoKernelConfig.cmake")
    elseif(UNIX AND NOT APPLE)
        set(_platform "linux-x64")
        set(_archive_name "GeoKernel-${_sdk_version}-linux-x64.tar.gz")
        set(_extract_root "${_cache_root}/${_sdk_version}/${_platform}")
        set(_sdk_root "${_extract_root}/GeoKernel-${_sdk_version}-linux-x64")
        set(_config_path "${_sdk_root}/GeoKernelConfig.cmake")
    else()
        message(FATAL_ERROR "Automatic GeoKernel SDK acquisition supports Windows x64 and Linux x64 only.")
    endif()

    if(DEFINED _latest_release_json)
        _geokernel_require_release_asset("${_latest_release_json}" "${_archive_name}")
        _geokernel_require_release_asset("${_latest_release_json}" "github-release-assets.sha256")
    endif()

    set(_release_base "https://github.com/geokernel-io/GeoKernel/releases/download/v${_sdk_version}")
    set(_checksum_name "github-release-assets.sha256")
    set(_download_root "${_cache_root}/downloads/${_sdk_version}")
    set(_archive_path "${_download_root}/${_archive_name}")
    set(_archive_part_path "${_archive_path}.part")
    set(_checksum_path "${_download_root}/${_checksum_name}")

    if(EXISTS "${_config_path}")
        set(${output_variable} "${_sdk_root}" PARENT_SCOPE)
        set(GEOKERNEL_RESOLVED_SDK_VERSION "${_sdk_version}" PARENT_SCOPE)
        message(STATUS "Using cached GeoKernel SDK ${_sdk_version}: ${_sdk_root}")
        return()
    endif()

    file(MAKE_DIRECTORY "${_download_root}")
    file(DOWNLOAD
        "${_release_base}/${_checksum_name}"
        "${_checksum_path}"
        TLS_VERIFY ON
        STATUS _checksum_status)
    list(GET _checksum_status 0 _checksum_code)
    if(NOT _checksum_code EQUAL 0)
        message(FATAL_ERROR "GeoKernel SDK checksum download failed: ${_checksum_status}")
    endif()

    file(STRINGS "${_checksum_path}" _checksum_lines)
    set(_expected_hash "")
    foreach(_line IN LISTS _checksum_lines)
        if(_line MATCHES "^([0-9a-fA-F]+) +\\*?(.+)$" AND
           CMAKE_MATCH_2 STREQUAL "${_archive_name}")
            string(LENGTH "${CMAKE_MATCH_1}" _hash_length)
            if(_hash_length EQUAL 64)
                string(TOLOWER "${CMAKE_MATCH_1}" _expected_hash)
            endif()
        endif()
    endforeach()
    if(NOT _expected_hash)
        message(FATAL_ERROR "The public release checksum file does not list ${_archive_name}.")
    endif()

    file(REMOVE "${_archive_part_path}")
    file(DOWNLOAD
        "${_release_base}/${_archive_name}"
        "${_archive_part_path}"
        EXPECTED_HASH "SHA256=${_expected_hash}"
        TLS_VERIFY ON
        TIMEOUT 900
        INACTIVITY_TIMEOUT 60
        SHOW_PROGRESS
        STATUS _archive_status)
    list(GET _archive_status 0 _archive_code)
    if(NOT _archive_code EQUAL 0)
        message(FATAL_ERROR "GeoKernel SDK download failed: ${_archive_status}")
    endif()
    file(REMOVE "${_archive_path}")
    file(RENAME "${_archive_part_path}" "${_archive_path}")

    if(WIN32)
        file(REMOVE_RECURSE "${_sdk_root}")
        file(MAKE_DIRECTORY "${_sdk_root}")
        file(ARCHIVE_EXTRACT INPUT "${_archive_path}" DESTINATION "${_sdk_root}")
    else()
        file(REMOVE_RECURSE "${_extract_root}")
        file(MAKE_DIRECTORY "${_extract_root}")
        file(ARCHIVE_EXTRACT INPUT "${_archive_path}" DESTINATION "${_extract_root}")
    endif()
    if(NOT EXISTS "${_config_path}")
        message(FATAL_ERROR "Downloaded ${_platform} SDK does not contain GeoKernelConfig.cmake.")
    endif()

    set(${output_variable} "${_sdk_root}" PARENT_SCOPE)
    set(GEOKERNEL_RESOLVED_SDK_VERSION "${_sdk_version}" PARENT_SCOPE)
    message(STATUS "Downloaded and verified GeoKernel SDK ${_sdk_version} for ${_platform}.")
endfunction()
