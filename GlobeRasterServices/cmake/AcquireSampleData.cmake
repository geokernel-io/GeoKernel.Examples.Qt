include_guard(GLOBAL)
# Download to a temporary file so interrupted transfers never poison the cache.
function(geokernel_acquire_sample_data archive sha256 destination)
    set(_cache "${CMAKE_CURRENT_SOURCE_DIR}/.sample-data")
    file(MAKE_DIRECTORY "${_cache}")
    set(_zip "${_cache}/${archive}")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/${archive}")
        set(_zip "${CMAKE_CURRENT_SOURCE_DIR}/data/${archive}")
    endif()
    set(_valid FALSE)
    if(EXISTS "${_zip}")
        file(SHA256 "${_zip}" _actual)
        if(_actual STREQUAL sha256)
            set(_valid TRUE)
        endif()
    endif()
    if(NOT _valid)
        set(_zip "${_cache}/${archive}")
        file(DOWNLOAD
            "https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/${archive}"
            "${_zip}.part" EXPECTED_HASH "SHA256=${sha256}" TLS_VERIFY ON
            TIMEOUT 900 INACTIVITY_TIMEOUT 60 SHOW_PROGRESS STATUS _status)
        list(GET _status 0 _code)
        if(NOT _code EQUAL 0)
            message(FATAL_ERROR "Sample data download failed (${archive}): ${_status}")
        endif()
        file(RENAME "${_zip}.part" "${_zip}")
    endif()
    file(MAKE_DIRECTORY "${destination}")
    file(ARCHIVE_EXTRACT INPUT "${_zip}" DESTINATION "${destination}")
endfunction()

function(geokernel_deploy_sample_data target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/data" "$<TARGET_FILE_DIR:${target}>/data"
        VERBATIM)
endfunction()
