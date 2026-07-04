if(NOT DEFINED GEOKERNEL_ROOT)
    message(FATAL_ERROR "GEOKERNEL_ROOT was not provided.")
endif()

if(NOT DEFINED DESTINATION)
    message(FATAL_ERROR "DESTINATION was not provided.")
endif()

if(NOT DEFINED CONFIG OR CONFIG STREQUAL "")
    set(CONFIG "Release")
endif()

if(CONFIG STREQUAL "Debug")
    set(_geokernel_sdk_config "Debug")
else()
    set(_geokernel_sdk_config "Release")
endif()

set(_geokernel_runtime_dir "${GEOKERNEL_ROOT}/outputs/${_geokernel_sdk_config}")

if(NOT EXISTS "${_geokernel_runtime_dir}")
    message(FATAL_ERROR "GeoKernel runtime output directory was not found: ${_geokernel_runtime_dir}")
endif()

file(MAKE_DIRECTORY "${DESTINATION}")

if(WIN32)
    file(GLOB _geokernel_runtime_files
        "${_geokernel_runtime_dir}/*.dll")
else()
    file(GLOB _geokernel_runtime_files
        "${_geokernel_runtime_dir}/libGeoKernel*.so*"
        "${_geokernel_runtime_dir}/libGeoKernel*.dylib*")
endif()

foreach(_geokernel_runtime_file IN LISTS _geokernel_runtime_files)
    file(COPY "${_geokernel_runtime_file}" DESTINATION "${DESTINATION}")
endforeach()

foreach(_geokernel_runtime_folder IN ITEMS
    gdal-data
    gdalplugins
    proj-data
    platforms
    tls
    imageformats
    sqldrivers)
    if(EXISTS "${_geokernel_runtime_dir}/${_geokernel_runtime_folder}")
        file(COPY "${_geokernel_runtime_dir}/${_geokernel_runtime_folder}" DESTINATION "${DESTINATION}")
    endif()
endforeach()

if(WIN32 AND DEFINED QT_BIN_DIR AND NOT QT_BIN_DIR STREQUAL "")
    get_filename_component(_qt_root_dir "${QT_BIN_DIR}/.." ABSOLUTE)
    set(_qt_plugin_dir "${_qt_root_dir}/plugins")

    foreach(_qt_plugin_folder IN ITEMS
        platforms
        imageformats
        iconengines
        sqldrivers
        styles
        tls)
        if(EXISTS "${_qt_plugin_dir}/${_qt_plugin_folder}")
            file(COPY "${_qt_plugin_dir}/${_qt_plugin_folder}" DESTINATION "${DESTINATION}")
        endif()
    endforeach()

    if(EXISTS "${QT_BIN_DIR}/opengl32sw.dll")
        file(COPY "${QT_BIN_DIR}/opengl32sw.dll" DESTINATION "${DESTINATION}")
    endif()
endif()

unset(_geokernel_runtime_file)
unset(_geokernel_runtime_files)
unset(_geokernel_runtime_folder)
unset(_geokernel_runtime_dir)
unset(_geokernel_sdk_config)
unset(_qt_plugin_folder)
unset(_qt_plugin_dir)
unset(_qt_root_dir)
