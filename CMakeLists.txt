cmake_minimum_required(VERSION 3.23)

project(GeoKernelExamples
    VERSION 0.1.0
    DESCRIPTION "GeoKernel Qt sample applications"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(GEOKERNEL_SAMPLES_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(GEOKERNEL_SAMPLES_OUTPUT_DIR "${GEOKERNEL_SAMPLES_ROOT_DIR}/outputs")
if(WIN32)
    set(GEOKERNEL_SAMPLES_PLATFORM_NAME "windows")
elseif(APPLE)
    set(GEOKERNEL_SAMPLES_PLATFORM_NAME "macos")
else()
    set(GEOKERNEL_SAMPLES_PLATFORM_NAME "linux")
endif()
set(GEOKERNEL_SAMPLES_PLATFORM_OUTPUT_DIR "${GEOKERNEL_SAMPLES_OUTPUT_DIR}/${GEOKERNEL_SAMPLES_PLATFORM_NAME}")
set(GEOKERNEL_SAMPLES_BIN_DIR "${GEOKERNEL_SAMPLES_PLATFORM_OUTPUT_DIR}/$<CONFIG>/bin")
option(GEOKERNEL_USE_DEBUG_SDK_FOR_DEBUG "Link Debug samples against GeoKernel Debug binaries" OFF)

get_filename_component(_geokernel_default_root "${GEOKERNEL_SAMPLES_ROOT_DIR}/../GeoKernel" ABSOLUTE)
set(GEOKERNEL_ROOT "${_geokernel_default_root}" CACHE PATH "GeoKernel native SDK repository or package root")
unset(_geokernel_default_root)

if(WIN32)
    enable_language(RC)

    if(DEFINED ENV{QTDIR})
        list(PREPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
    endif()
    if(EXISTS "C:/Qt/6.11.0/msvc2022_64")
        list(PREPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.0/msvc2022_64")
    endif()

    set(GEOKERNEL_SAMPLE_APP_ICON "${GEOKERNEL_SAMPLES_ROOT_DIR}/GeoKernelAppIcon.ico")
    set(GEOKERNEL_SAMPLE_APP_ICON_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/GeoKernelSampleAppIcon.rc")
    if(NOT EXISTS "${GEOKERNEL_SAMPLE_APP_ICON}")
        message(FATAL_ERROR "GeoKernel sample app icon was not found: ${GEOKERNEL_SAMPLE_APP_ICON}")
    endif()
    configure_file(
        "${GEOKERNEL_SAMPLES_ROOT_DIR}/cmake/GeoKernelSampleAppIcon.rc.in"
        "${GEOKERNEL_SAMPLE_APP_ICON_RESOURCE}"
        @ONLY)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network Sql Svg)

set(_geokernel_include_dirs
    "${GEOKERNEL_ROOT}/src/GeoKernel.Core"
    "${GEOKERNEL_ROOT}/src/GeoKernel.Formats"
    "${GEOKERNEL_ROOT}/src/GeoKernel.Viewer")

foreach(_geokernel_include_dir IN LISTS _geokernel_include_dirs)
    if(NOT EXISTS "${_geokernel_include_dir}")
        message(FATAL_ERROR "GeoKernel include directory was not found: ${_geokernel_include_dir}")
    endif()
endforeach()

function(geokernel_import_library target_name library_base_name)
    add_library(${target_name} SHARED IMPORTED GLOBAL)
    set_target_properties(${target_name} PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_geokernel_include_dirs}")

    if(WIN32)
        foreach(_config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            if(_config STREQUAL "DEBUG" AND GEOKERNEL_USE_DEBUG_SDK_FOR_DEBUG)
                set(_sdk_config "Debug")
            else()
                set(_sdk_config "Release")
            endif()

            set_target_properties(${target_name} PROPERTIES
                "IMPORTED_IMPLIB_${_config}" "${GEOKERNEL_ROOT}/outputs/build/${_sdk_config}/${library_base_name}.lib"
                "IMPORTED_LOCATION_${_config}" "${GEOKERNEL_ROOT}/outputs/build/${_sdk_config}/${library_base_name}.dll")
        endforeach()
    elseif(APPLE)
        set_target_properties(${target_name} PROPERTIES
            IMPORTED_LOCATION "${GEOKERNEL_ROOT}/outputs/build/Release/lib${library_base_name}.dylib")
    else()
        set_target_properties(${target_name} PROPERTIES
            IMPORTED_LOCATION "${GEOKERNEL_ROOT}/outputs/build/Release/lib${library_base_name}.so")
    endif()
endfunction()

geokernel_import_library(GeoKernel::Core GeoKernel.Core)
geokernel_import_library(GeoKernel::Formats GeoKernel.Formats)
geokernel_import_library(GeoKernel::Viewer GeoKernel.Viewer)

add_custom_target(GeoKernelSampleRuntime ALL
    COMMAND ${CMAKE_COMMAND}
        -D "GEOKERNEL_ROOT=${GEOKERNEL_ROOT}"
        -D "QT_BIN_DIR=$<TARGET_FILE_DIR:Qt6::Core>"
        -D "QT_RUNTIME_FILES=$<TARGET_FILE:Qt6::Core>;$<TARGET_FILE:Qt6::Gui>;$<TARGET_FILE:Qt6::Widgets>;$<TARGET_FILE:Qt6::OpenGL>;$<TARGET_FILE:Qt6::OpenGLWidgets>;$<TARGET_FILE:Qt6::Network>;$<TARGET_FILE:Qt6::Sql>;$<TARGET_FILE:Qt6::Svg>"
        -D "CONFIG=$<CONFIG>"
        -D "USE_DEBUG_SDK_FOR_DEBUG=${GEOKERNEL_USE_DEBUG_SDK_FOR_DEBUG}"
        -D "DESTINATION=${GEOKERNEL_SAMPLES_BIN_DIR}"
        -P "${GEOKERNEL_SAMPLES_ROOT_DIR}/cmake/CopyGeoKernelRuntime.cmake"
    COMMENT "Copying GeoKernel runtime")

function(geokernel_add_sample sample_relative_dir)
    get_filename_component(sample_name "${sample_relative_dir}" NAME)
    get_filename_component(sample_group "${sample_relative_dir}" DIRECTORY)
    set(_sample_dir "${GEOKERNEL_SAMPLES_ROOT_DIR}/examples/${sample_relative_dir}")
    set(_sample_sources
        "${_sample_dir}/main.cpp")

    if(WIN32)
        list(APPEND _sample_sources "${GEOKERNEL_SAMPLE_APP_ICON_RESOURCE}")
    endif()

    add_executable(${sample_name} ${_sample_sources})

    add_dependencies(${sample_name} GeoKernelSampleRuntime)

    if(WIN32)
        set_target_properties(${sample_name} PROPERTIES WIN32_EXECUTABLE TRUE)
    elseif(APPLE)
        set_target_properties(${sample_name} PROPERTIES
            BUILD_RPATH "@loader_path")
    else()
        set_target_properties(${sample_name} PROPERTIES
            BUILD_RPATH "$ORIGIN")
    endif()

    set_target_properties(${sample_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${GEOKERNEL_SAMPLES_BIN_DIR}"
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${sample_name}>")

    if(sample_group)
        set_property(TARGET ${sample_name} PROPERTY FOLDER "${sample_group}")
    endif()

    target_compile_definitions(${sample_name} PRIVATE
        QT_NO_DEPRECATED_WARNINGS
        QT_SQL_LIB)

    target_include_directories(${sample_name} PRIVATE
        "${GEOKERNEL_SAMPLES_ROOT_DIR}/examples/Common")

    target_link_libraries(${sample_name} PRIVATE
        GeoKernel::Viewer
        GeoKernel::Formats
        GeoKernel::Core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::Network
        Qt6::Sql
        Qt6::Svg)

    if(MSVC)
        target_compile_options(${sample_name} PRIVATE /W3 /permissive- /FS)
    else()
        target_compile_options(${sample_name} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()

endfunction()

file(GLOB _geokernel_sample_main_files
    LIST_DIRECTORIES false
    RELATIVE "${GEOKERNEL_SAMPLES_ROOT_DIR}/examples"
    "${GEOKERNEL_SAMPLES_ROOT_DIR}/examples/*/main.cpp"
    "${GEOKERNEL_SAMPLES_ROOT_DIR}/examples/*/*/main.cpp")
list(SORT _geokernel_sample_main_files)

foreach(_geokernel_sample_main_file IN LISTS _geokernel_sample_main_files)
    get_filename_component(_geokernel_sample_dir "${_geokernel_sample_main_file}" DIRECTORY)
    geokernel_add_sample("${_geokernel_sample_dir}")
endforeach()

if(TARGET HelloMap)
    set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT HelloMap)
endif()

unset(_geokernel_sample_dir)
unset(_geokernel_sample_main_file)
unset(_geokernel_sample_main_files)
unset(_geokernel_include_dir)
unset(_geokernel_include_dirs)
